#include "common/router/retry_state_impl.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/codes.h"
#include "common/http/utility.h"

namespace Router {

// These are defined in envoy/server/router.h, however during certain cases the compiler is
// refusing to use the header version so allocate space here.
const uint32_t RetryPolicy::RETRY_ON_5XX;
const uint32_t RetryPolicy::RETRY_ON_CONNECT_FAILURE;
const uint32_t RetryPolicy::RETRY_ON_RETRIABLE_4XX;

RetryStatePtr RetryStateImpl::create(const RetryPolicy& route_policy,
                                     Http::HeaderMap& request_headers,
                                     const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                                     Runtime::RandomGenerator& random,
                                     Event::Dispatcher& dispatcher,
                                     Upstream::ResourcePriority priority) {
  RetryStatePtr ret;

  // We short circuit here and do not both with an allocation if there is no chance we will retry.
  if (request_headers.EnvoyRetryOn() || route_policy.retryOn()) {
    ret.reset(new RetryStateImpl(route_policy, request_headers, cluster, runtime, random,
                                 dispatcher, priority));
  }

  request_headers.removeEnvoyRetryOn();
  request_headers.removeEnvoyMaxRetries();
  return ret;
}

RetryStateImpl::RetryStateImpl(const RetryPolicy& route_policy, Http::HeaderMap& request_headers,
                               const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                               Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                               Upstream::ResourcePriority priority)
    : cluster_(cluster), runtime_(runtime), random_(random), dispatcher_(dispatcher),
      priority_(priority) {

  if (request_headers.EnvoyRetryOn()) {
    retry_on_ = parseRetryOn(request_headers.EnvoyRetryOn()->value().c_str());
    if (retry_on_ != 0 && request_headers.EnvoyMaxRetries()) {
      const char* max_retries = request_headers.EnvoyMaxRetries()->value().c_str();
      uint64_t temp;
      if (StringUtil::atoul(max_retries, temp)) {
        retries_remaining_ = temp;
      }
    }
  }

  // Merge in the route policy.
  retry_on_ |= route_policy.retryOn();
  retries_remaining_ = std::max(retries_remaining_, route_policy.numRetries());
}

RetryStateImpl::~RetryStateImpl() { resetRetry(); }

void RetryStateImpl::enableBackoffTimer() {
  // We use a fully jittered exponential backoff algorithm.
  current_retry_++;
  uint32_t multiplier = (1 << current_retry_) - 1;
  uint64_t base = runtime_.snapshot().getInteger("upstream.base_retry_backoff_ms", 25);
  uint64_t timeout = random_.random() % (base * multiplier);

  if (!retry_timer_) {
    retry_timer_ = dispatcher_.createTimer([this]() -> void { callback_(); });
  }

  retry_timer_->enableTimer(std::chrono::milliseconds(timeout));
}

uint32_t RetryStateImpl::parseRetryOn(const std::string& config) {
  uint32_t ret = 0;
  std::vector<std::string> retry_on_list = StringUtil::split(config, ',');
  for (const std::string& retry_on : retry_on_list) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnValues._5xx) {
      ret |= RetryPolicy::RETRY_ON_5XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.ConnectFailure) {
      ret |= RetryPolicy::RETRY_ON_CONNECT_FAILURE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Retriable4xx) {
      ret |= RetryPolicy::RETRY_ON_RETRIABLE_4XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RefusedStream) {
      ret |= RetryPolicy::RETRY_ON_REFUSED_STREAM;
    }
  }

  return ret;
}

void RetryStateImpl::resetRetry() {
  if (callback_) {
    cluster_.resourceManager(priority_).retries().dec();
    callback_ = nullptr;
  }
}

bool RetryStateImpl::shouldRetry(const Http::HeaderMap* response_headers,
                                 const Optional<Http::StreamResetReason>& reset_reason,
                                 DoRetryCallback callback) {
  ASSERT((response_headers != nullptr) ^ reset_reason.valid());

  if (callback_ && !wouldRetry(response_headers, reset_reason)) {
    cluster_.stats().upstream_rq_retry_success_.inc();
  }

  resetRetry();

  if (retries_remaining_ == 0) {
    return false;
  }

  if (!runtime_.snapshot().featureEnabled("upstream.use_retry", 100)) {
    return false;
  }

  retries_remaining_--;
  if (!wouldRetry(response_headers, reset_reason)) {
    return false;
  }

  if (!cluster_.resourceManager(priority_).retries().canCreate()) {
    cluster_.stats().upstream_rq_retry_overflow_.inc();
    return false;
  }

  ASSERT(!callback_);
  callback_ = callback;
  cluster_.resourceManager(priority_).retries().inc();
  cluster_.stats().upstream_rq_retry_.inc();
  enableBackoffTimer();
  return true;
}

bool RetryStateImpl::wouldRetry(const Http::HeaderMap* response_headers,
                                const Optional<Http::StreamResetReason>& reset_reason) {
  if (retry_on_ & RetryPolicy::RETRY_ON_5XX) {
    // wouldRetry() is passed null headers when there was an upstream reset. Currently we count an
    // upstream reset as a "5xx" (since it will result in one). We may eventually split this out
    // into its own type. I.e., RETRY_ON_RESET.
    if (!response_headers ||
        Http::CodeUtility::is5xx(Http::Utility::getResponseStatus(*response_headers))) {
      return true;
    }
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_REFUSED_STREAM) && reset_reason.valid() &&
      reset_reason.value() == Http::StreamResetReason::RemoteRefusedStreamReset) {
    return true;
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_CONNECT_FAILURE) && reset_reason.valid() &&
      reset_reason.value() == Http::StreamResetReason::ConnectionFailure) {
    return true;
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_RETRIABLE_4XX) && response_headers) {
    Http::Code code = static_cast<Http::Code>(Http::Utility::getResponseStatus(*response_headers));
    if (code == Http::Code::Conflict) {
      return true;
    }
  }

  return false;
}

} // Router

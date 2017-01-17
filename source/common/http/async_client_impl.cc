#include "async_client_impl.h"
#include "headers.h"

namespace Http {

const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
    AsyncRequestImpl::NullRateLimitPolicy::rate_limit_policy_entry_;
const AsyncRequestImpl::NullRateLimitPolicy AsyncRequestImpl::RouteEntryImpl::rate_limit_policy_;
const AsyncRequestImpl::NullRetryPolicy AsyncRequestImpl::RouteEntryImpl::retry_policy_;
const AsyncRequestImpl::NullShadowPolicy AsyncRequestImpl::RouteEntryImpl::shadow_policy_;
const AsyncRequestImpl::NullVirtualHost AsyncRequestImpl::RouteEntryImpl::virtual_host_;
const AsyncRequestImpl::NullRateLimitPolicy AsyncRequestImpl::NullVirtualHost::rate_limit_policy_;

AsyncClientImpl::AsyncClientImpl(const Upstream::ClusterInfo& cluster, Stats::Store& stats_store,
                                 Event::Dispatcher& dispatcher,
                                 const LocalInfo::LocalInfo& local_info,
                                 Upstream::ClusterManager& cm, Runtime::Loader& runtime,
                                 Runtime::RandomGenerator& random,
                                 Router::ShadowWriterPtr&& shadow_writer)
    : cluster_(cluster), config_("http.async-client.", local_info, stats_store, cm, runtime, random,
                                 std::move(shadow_writer), true),
      dispatcher_(dispatcher) {}

AsyncClientImpl::~AsyncClientImpl() {
  while (!active_requests_.empty()) {
    active_requests_.front()->failDueToClientDestroy();
  }
}

AsyncClient::Request* AsyncClientImpl::send(MessagePtr&& request, AsyncClient::Callbacks& callbacks,
                                            const Optional<std::chrono::milliseconds>& timeout) {
  std::unique_ptr<AsyncRequestImpl> new_request{
      new AsyncRequestImpl(std::move(request), *this, callbacks, timeout)};

  // The request may get immediately failed. If so, we will return nullptr.
  if (!new_request->complete_) {
    new_request->moveIntoList(std::move(new_request), active_requests_);
    return active_requests_.front().get();
  } else {
    return nullptr;
  }
}

AsyncRequestImpl::AsyncRequestImpl(MessagePtr&& request, AsyncClientImpl& parent,
                                   AsyncClient::Callbacks& callbacks,
                                   const Optional<std::chrono::milliseconds>& timeout)
    : request_(std::move(request)), parent_(parent), callbacks_(callbacks),
      stream_id_(parent.config_.random_.random()), router_(parent.config_),
      request_info_(Protocol::Http11), route_(parent_.cluster_.name(), timeout) {

  router_.setDecoderFilterCallbacks(*this);
  request_->headers().insertEnvoyInternalRequest().value(
      Headers::get().EnvoyInternalRequestValues.True);
  request_->headers().insertForwardedFor().value(parent_.config_.local_info_.address());
  router_.decodeHeaders(request_->headers(), !request_->body());
  if (!complete_ && request_->body()) {
    router_.decodeData(*request_->body(), true);
  }

  // TODO: Support request trailers.
  // TODO: Correctly set protocol in request info when we support access logging.
}

AsyncRequestImpl::~AsyncRequestImpl() { ASSERT(!reset_callback_); }

void AsyncRequestImpl::encodeHeaders(HeaderMapPtr&& headers, bool end_stream) {
  response_.reset(new ResponseMessageImpl(std::move(headers)));
#ifndef NDEBUG
  log_debug("async http request response headers (end_stream={}):", end_stream);
  response_->headers().iterate([](const HeaderEntry& header, void*) -> void {
    log_debug("  '{}':'{}'", header.key().c_str(), header.value().c_str());
  }, nullptr);
#endif

  if (end_stream) {
    onComplete();
  }
}

void AsyncRequestImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  log_trace("async http request response data (length={} end_stream={})", data.length(),
            end_stream);
  if (!response_->body()) {
    response_->body(Buffer::InstancePtr{new Buffer::OwnedImpl()});
  }
  response_->body()->move(data);

  if (end_stream) {
    onComplete();
  }
}

void AsyncRequestImpl::encodeTrailers(HeaderMapPtr&& trailers) {
  response_->trailers(std::move(trailers));
#ifndef NDEBUG
  log_debug("async http request response trailers:");
  response_->trailers()->iterate([](const HeaderEntry& header, void*) -> void {
    log_debug("  '{}':'{}'", header.key().c_str(), header.value().c_str());
  }, nullptr);
#endif

  onComplete();
}

void AsyncRequestImpl::cancel() {
  reset_callback_();
  cleanup();
}

void AsyncRequestImpl::onComplete() {
  complete_ = true;
  callbacks_.onSuccess(std::move(response_));
  cleanup();
}

void AsyncRequestImpl::cleanup() {
  response_.reset();
  reset_callback_ = nullptr;

  // This will destroy us, but only do so if we are actually in a list. This does not happen in
  // the immediate failure case.
  if (inserted()) {
    removeFromList(parent_.active_requests_);
  }
}

void AsyncRequestImpl::resetStream() {
  // In this case we don't have a valid response so we do need to raise a failure.
  callbacks_.onFailure(AsyncClient::FailureReason::Reset);
  cleanup();
}

void AsyncRequestImpl::failDueToClientDestroy() {
  // In this case we are going away because the client is being destroyed. We need to both reset
  // the stream as well as raise a failure callback.
  reset_callback_();
  callbacks_.onFailure(AsyncClient::FailureReason::Reset);
  cleanup();
}

} // Http

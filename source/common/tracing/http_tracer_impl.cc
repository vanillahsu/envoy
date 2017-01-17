#include "http_tracer_impl.h"

#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/header_map_impl.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/runtime/uuid_util.h"

namespace Tracing {

void HttpTracerUtility::mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime) {
  if (!request_headers.RequestId()) {
    return;
  }

  // TODO PERF: Avoid copy.
  std::string x_request_id = request_headers.RequestId()->value().c_str();

  uint16_t result;
  // Skip if x-request-id is corrupted.
  if (!UuidUtils::uuidModBy(x_request_id, result, 10000)) {
    return;
  }

  // Do not apply tracing transformations if we are currently tracing.
  if (UuidTraceStatus::NoTrace == UuidUtils::isTraceableUuid(x_request_id)) {
    if (request_headers.ClientTraceId() &&
        runtime.snapshot().featureEnabled("tracing.client_enabled", 100)) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Client);
    } else if (request_headers.EnvoyForceTrace()) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Forced);
    } else if (runtime.snapshot().featureEnabled("tracing.random_sampling", 0, result, 10000)) {
      UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::Sampled);
    }
  }

  if (!runtime.snapshot().featureEnabled("tracing.global_enabled", 100, result)) {
    UuidUtils::setTraceableUuid(x_request_id, UuidTraceStatus::NoTrace);
  }

  request_headers.RequestId()->value(x_request_id);
}

Decision HttpTracerUtility::isTracing(const Http::AccessLog::RequestInfo& request_info,
                                      const Http::HeaderMap& request_headers) {
  // Exclude HC requests immediately.
  if (request_info.healthCheck()) {
    return {Reason::HealthCheck, false};
  }

  if (!request_headers.RequestId()) {
    return {Reason::NotTraceableRequestId, false};
  }

  // TODO PERF: Avoid copy.
  UuidTraceStatus trace_status =
      UuidUtils::isTraceableUuid(request_headers.RequestId()->value().c_str());

  switch (trace_status) {
  case UuidTraceStatus::Client:
    return {Reason::ClientForced, true};
  case UuidTraceStatus::Forced:
    return {Reason::ServiceForced, true};
  case UuidTraceStatus::Sampled:
    return {Reason::Sampling, true};
  case UuidTraceStatus::NoTrace:
    return {Reason::NotTraceableRequestId, false};
  }

  throw std::invalid_argument("Unknown trace_status");
}

HttpTracerImpl::HttpTracerImpl(Runtime::Loader& runtime, Stats::Store& stats)
    : runtime_(runtime),
      stats_{HTTP_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.http_tracer."))} {}

void HttpTracerImpl::addSink(HttpSinkPtr&& sink) { sinks_.push_back(std::move(sink)); }

void HttpTracerImpl::trace(const Http::HeaderMap* request_headers,
                           const Http::HeaderMap* response_headers,
                           const Http::AccessLog::RequestInfo& request_info,
                           const TracingContext& tracing_context) {
  static const Http::HeaderMapImpl empty_headers;
  if (!request_headers) {
    request_headers = &empty_headers;
  }
  if (!response_headers) {
    response_headers = &empty_headers;
  }

  stats_.flush_.inc();

  Decision decision = HttpTracerUtility::isTracing(request_info, *request_headers);
  populateStats(decision);

  if (decision.is_tracing) {
    stats_.doing_tracing_.inc();

    for (HttpSinkPtr& sink : sinks_) {
      sink->flushTrace(*request_headers, *response_headers, request_info, tracing_context);
    }
  }
}

void HttpTracerImpl::populateStats(const Decision& decision) {
  switch (decision.reason) {
  case Reason::ClientForced:
    stats_.client_enabled_.inc();
    break;
  case Reason::HealthCheck:
    stats_.health_check_.inc();
    break;
  case Reason::NotTraceableRequestId:
    stats_.not_traceable_.inc();
    break;
  case Reason::Sampling:
    stats_.random_sampling_.inc();
    break;
  case Reason::ServiceForced:
    stats_.service_forced_.inc();
    break;
  }
}

LightStepRecorder::LightStepRecorder(const lightstep::TracerImpl& tracer, LightStepSink& sink,
                                     Event::Dispatcher& dispatcher)
    : builder_(tracer), sink_(sink) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    sink_.tracerStats().timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });

  enableTimer();
}

void LightStepRecorder::RecordSpan(lightstep::collector::Span&& span) {
  builder_.addSpan(std::move(span));

  uint64_t min_flush_spans =
      sink_.runtime().snapshot().getInteger("tracing.lightstep.min_flush_spans", 5U);
  if (builder_.pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

bool LightStepRecorder::FlushWithTimeout(lightstep::Duration) {
  // Note: We don't expect this to be called, since the Tracer
  // reference is private to its LightStepSink.
  return true;
}

std::unique_ptr<lightstep::Recorder>
LightStepRecorder::NewInstance(LightStepSink& sink, Event::Dispatcher& dispatcher,
                               const lightstep::TracerImpl& tracer) {
  return std::unique_ptr<lightstep::Recorder>(new LightStepRecorder(tracer, sink, dispatcher));
}

void LightStepRecorder::enableTimer() {
  uint64_t flush_interval =
      sink_.runtime().snapshot().getInteger("tracing.lightstep.flush_interval_ms", 1000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void LightStepRecorder::flushSpans() {
  if (builder_.pendingSpans() != 0) {
    sink_.tracerStats().spans_sent_.add(builder_.pendingSpans());
    lightstep::collector::ReportRequest request;
    std::swap(request, builder_.pending());

    Http::MessagePtr message =
        Grpc::Common::prepareHeaders(sink_.cluster()->name(), lightstep::CollectorServiceFullName(),
                                     lightstep::CollectorMethodName());

    message->body(Grpc::Common::serializeBody(std::move(request)));

    uint64_t timeout =
        sink_.runtime().snapshot().getInteger("tracing.lightstep.request_timeout", 5000U);
    sink_.clusterManager()
        .httpAsyncClientForCluster(sink_.cluster()->name())
        .send(std::move(message), *this, std::chrono::milliseconds(timeout));
  }
}

LightStepSink::TlsLightStepTracer::TlsLightStepTracer(lightstep::Tracer tracer, LightStepSink& sink)
    : tracer_(tracer), sink_(sink) {}

LightStepSink::LightStepSink(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                             Stats::Store& stats, const LocalInfo::LocalInfo& local_info,
                             ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                             std::unique_ptr<lightstep::TracerOptions> options)
    : cm_(cluster_manager), cluster_(cm_.get(config.getString("collector_cluster"))),
      tracer_stats_{LIGHTSTEP_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.lightstep."))},
      local_info_(local_info), tls_(tls), runtime_(runtime), options_(std::move(options)),
      tls_slot_(tls.allocateSlot()) {
  if (!cluster_) {
    throw EnvoyException(fmt::format("{} collector cluster is not defined on cluster manager level",
                                     config.getString("collector_cluster")));
  }

  if (!(cluster_->features() & Upstream::ClusterInfo::Features::HTTP2)) {
    throw EnvoyException(
        fmt::format("{} collector cluster must support http2 for gRPC calls", cluster_->name()));
  }

  tls_.set(tls_slot_, [this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectPtr {
    lightstep::Tracer tracer(lightstep::NewUserDefinedTransportLightStepTracer(
        *options_, std::bind(&LightStepRecorder::NewInstance, std::ref(*this), std::ref(dispatcher),
                             std::placeholders::_1)));

    return ThreadLocal::ThreadLocalObjectPtr{new TlsLightStepTracer(std::move(tracer), *this)};
  });
}

std::string LightStepSink::buildRequestLine(const Http::HeaderMap& request_headers,
                                            const Http::AccessLog::RequestInfo& info) {
  std::string path = request_headers.EnvoyOriginalPath()
                         ? request_headers.EnvoyOriginalPath()->value().c_str()
                         : request_headers.Path()->value().c_str();
  static const size_t max_path_length = 256;

  if (path.length() > max_path_length) {
    path = path.substr(0, max_path_length);
  }

  return fmt::format("{} {} {}", request_headers.Method()->value().c_str(), path,
                     Http::AccessLog::AccessLogFormatUtils::protocolToString(info.protocol()));
}

std::string LightStepSink::buildResponseCode(const Http::AccessLog::RequestInfo& info) {
  return info.responseCode().valid() ? std::to_string(info.responseCode().value()) : "0";
}

static std::string valueOrDefault(const Http::HeaderEntry* header, const char* default_value) {
  return header ? header->value().c_str() : default_value;
}

void LightStepSink::flushTrace(const Http::HeaderMap& request_headers, const Http::HeaderMap&,
                               const Http::AccessLog::RequestInfo& request_info,
                               const TracingContext& tracing_context) {
  lightstep::Span span = tls_.getTyped<TlsLightStepTracer>(tls_slot_).tracer_.StartSpan(
      tracing_context.operationName(),
      {lightstep::StartTimestamp(request_info.startTime()),
       lightstep::SetTag("guid:x-request-id",
                         std::string(request_headers.RequestId()->value().c_str())),
       lightstep::SetTag("request line", buildRequestLine(request_headers, request_info)),
       lightstep::SetTag("response code", buildResponseCode(request_info)),
       lightstep::SetTag("request size", request_info.bytesReceived()),
       lightstep::SetTag("response size", request_info.bytesSent()),
       lightstep::SetTag("host header", valueOrDefault(request_headers.Host(), "-")),
       lightstep::SetTag("downstream cluster",
                         valueOrDefault(request_headers.EnvoyDownstreamServiceCluster(), "-")),
       lightstep::SetTag("user agent", valueOrDefault(request_headers.UserAgent(), "-")),
       lightstep::SetTag("node id", local_info_.nodeName())});

  if (request_info.responseCode().valid() &&
      Http::CodeUtility::is5xx(request_info.responseCode().value())) {
    span.SetTag("error", "true");
  }

  span.SetTag("response flags", Http::AccessLog::ResponseFlagUtils::toShortString(request_info));

  if (request_headers.ClientTraceId()) {
    span.SetTag("guid:x-client-trace-id",
                std::string(request_headers.ClientTraceId()->value().c_str()));
  }

  span.Finish();
}

void LightStepRecorder::onFailure(Http::AsyncClient::FailureReason) {
  Grpc::Common::chargeStat(*sink_.cluster(), lightstep::CollectorServiceFullName(),
                           lightstep::CollectorMethodName(), false);
}

void LightStepRecorder::onSuccess(Http::MessagePtr&& msg) {
  try {
    Grpc::Common::validateResponse(*msg);

    Grpc::Common::chargeStat(*sink_.cluster(), lightstep::CollectorServiceFullName(),
                             lightstep::CollectorMethodName(), true);
  } catch (const Grpc::Exception& ex) {
    Grpc::Common::chargeStat(*sink_.cluster(), lightstep::CollectorServiceFullName(),
                             lightstep::CollectorMethodName(), false);
  }
}

} // Tracing

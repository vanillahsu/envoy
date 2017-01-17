#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

#include "lightstep/tracer.h"

namespace Tracing {

#define HTTP_TRACER_STATS(COUNTER)                                                                 \
  COUNTER(global_switch_off)                                                                       \
  COUNTER(invalid_request_id)                                                                      \
  COUNTER(random_sampling)                                                                         \
  COUNTER(service_forced)                                                                          \
  COUNTER(client_enabled)                                                                          \
  COUNTER(doing_tracing)                                                                           \
  COUNTER(flush)                                                                                   \
  COUNTER(not_traceable)                                                                           \
  COUNTER(health_check)                                                                            \
  COUNTER(traceable)

struct HttpTracerStats {
  HTTP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

#define LIGHTSTEP_TRACER_STATS(COUNTER)                                                            \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)

struct LightstepTracerStats {
  LIGHTSTEP_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  void addSink(HttpSinkPtr&&) override {}
  void trace(const Http::HeaderMap*, const Http::HeaderMap*, const Http::AccessLog::RequestInfo&,
             const TracingContext&) override {}
};

enum class Reason {
  NotTraceableRequestId,
  HealthCheck,
  Sampling,
  ServiceForced,
  ClientForced,
};

struct Decision {
  Reason reason;
  bool is_tracing;
};

class HttpTracerUtility {
public:
  /**
   * Request might be traceable if x-request-id is traceable uuid or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision isTracing(const Http::AccessLog::RequestInfo& request_info,
                            const Http::HeaderMap& request_headers);

  /**
   * Mutate request headers if request needs to be traced.
   */
  static void mutateHeaders(Http::HeaderMap& request_headers, Runtime::Loader& runtime);
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(Runtime::Loader& runtime, Stats::Store& stats);

  // Tracing::HttpTracer
  void addSink(HttpSinkPtr&& sink) override;
  void trace(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
             const Http::AccessLog::RequestInfo& request_info,
             const TracingContext& tracing_context) override;

private:
  void populateStats(const Decision& decision);

  Runtime::Loader& runtime_;
  HttpTracerStats stats_;
  std::vector<HttpSinkPtr> sinks_;
};

/**
 * LightStep (http://lightstep.com/) provides tracing capabilities, aggregation, visualization of
 * application trace data.
 *
 * LightStepSink is for flushing data to LightStep collectors.
 */
class LightStepSink : public HttpSink {
public:
  LightStepSink(const Json::Object& config, Upstream::ClusterManager& cluster_manager,
                Stats::Store& stats, const LocalInfo::LocalInfo& local_info,
                ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                std::unique_ptr<lightstep::TracerOptions> options);

  // Tracer::HttpSink
  void flushTrace(const Http::HeaderMap& request_headers, const Http::HeaderMap& response_headers,
                  const Http::AccessLog::RequestInfo& request_info,
                  const TracingContext& tracing_context) override;

  Upstream::ClusterManager& clusterManager() { return cm_; }
  Upstream::ClusterInfoPtr cluster() { return cluster_; }
  Runtime::Loader& runtime() { return runtime_; }
  LightstepTracerStats& tracerStats() { return tracer_stats_; }

private:
  struct TlsLightStepTracer : ThreadLocal::ThreadLocalObject {
    TlsLightStepTracer(lightstep::Tracer tracer, LightStepSink& sink);

    void shutdown() override {}

    lightstep::Tracer tracer_;
    LightStepSink& sink_;
  };

  std::string buildRequestLine(const Http::HeaderMap& request_headers,
                               const Http::AccessLog::RequestInfo& info);
  std::string buildResponseCode(const Http::AccessLog::RequestInfo& info);

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoPtr cluster_;
  LightstepTracerStats tracer_stats_;
  const LocalInfo::LocalInfo& local_info_;
  ThreadLocal::Instance& tls_;
  Runtime::Loader& runtime_;
  std::unique_ptr<lightstep::TracerOptions> options_;
  uint32_t tls_slot_;
};

class LightStepRecorder : public lightstep::Recorder, Http::AsyncClient::Callbacks {
public:
  LightStepRecorder(const lightstep::TracerImpl& tracer, LightStepSink& sink,
                    Event::Dispatcher& dispatcher);

  // lightstep::Recorder
  void RecordSpan(lightstep::collector::Span&& span) override;
  bool FlushWithTimeout(lightstep::Duration) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&&) override;
  void onFailure(Http::AsyncClient::FailureReason) override;

  static std::unique_ptr<lightstep::Recorder> NewInstance(LightStepSink& sink,
                                                          Event::Dispatcher& dispatcher,
                                                          const lightstep::TracerImpl& tracer);

private:
  void enableTimer();
  void flushSpans();

  lightstep::ReportBuilder builder_;
  LightStepSink& sink_;
  Event::TimerPtr flush_timer_;
};

} // Tracing

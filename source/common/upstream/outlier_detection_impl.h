#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/common/time.h"
#include "envoy/event/timer.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "common/json/json_loader.h"

namespace Upstream {
namespace Outlier {

/**
 * Null host sink implementation.
 */
class DetectorHostSinkNullImpl : public DetectorHostSink {
public:
  // Upstream::Outlier::DetectorHostSink
  uint32_t numEjections() override { return 0; }
  void putHttpResponseCode(uint64_t) override {}
  void putResponseTime(std::chrono::milliseconds) override {}
};

/**
 * Factory for creating a detector from a JSON configuration.
 */
class DetectorImplFactory {
public:
  static DetectorPtr createForCluster(Cluster& cluster, const Json::Object& cluster_config,
                                      Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                                      EventLoggerPtr event_logger);
};

class DetectorImpl;

/**
 * Implementation of DetectorHostSink for the generic detector.
 */
class DetectorHostSinkImpl : public DetectorHostSink {
public:
  DetectorHostSinkImpl(std::shared_ptr<DetectorImpl> detector, HostPtr host)
      : detector_(detector), host_(host) {}

  void eject(SystemTime ejection_time);
  SystemTime ejectionTime() { return ejection_time_; }

  // Upstream::Outlier::DetectorHostSink
  uint32_t numEjections() override { return num_ejections_; }
  void putHttpResponseCode(uint64_t response_code) override;
  void putResponseTime(std::chrono::milliseconds) override {}

private:
  std::weak_ptr<DetectorImpl> detector_;
  std::weak_ptr<Host> host_;
  std::atomic<uint32_t> consecutive_5xx_{0};
  SystemTime ejection_time_;
  uint32_t num_ejections_{};
};

/**
 * All outlier detection stats. @see stats_macros.h
 */
// clang-format off
#define ALL_OUTLIER_DETECTION_STATS(COUNTER, GAUGE)                                                \
  COUNTER(ejections_total)                                                                         \
  GAUGE  (ejections_active)                                                                        \
  COUNTER(ejections_overflow)                                                                      \
  COUNTER(ejections_consecutive_5xx)
// clang-format on

/**
 * Struct definition for all outlier detection stats. @see stats_macros.h
 */
struct DetectionStats {
  ALL_OUTLIER_DETECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * An implementation of an outlier detector. In the future we may support multiple outlier detection
 * implementations with different configuration. For now, as we iterate everything is contained
 * within this implementation.
 */
class DetectorImpl : public Detector, public std::enable_shared_from_this<DetectorImpl> {
public:
  static std::shared_ptr<DetectorImpl> create(const Cluster& cluster, Event::Dispatcher& dispatcher,
                                              Runtime::Loader& runtime,
                                              SystemTimeSource& time_source,
                                              EventLoggerPtr event_logger);
  ~DetectorImpl();

  void onConsecutive5xx(HostPtr host);
  Runtime::Loader& runtime() { return runtime_; }

  // Upstream::Outlier::Detector
  void addChangedStateCb(ChangeStateCb cb) override { callbacks_.push_back(cb); }

private:
  DetectorImpl(const Cluster& cluster, Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
               SystemTimeSource& time_source, EventLoggerPtr event_logger);

  void addHostSink(HostPtr host);
  void armIntervalTimer();
  void checkHostForUneject(HostPtr host, DetectorHostSinkImpl* sink, SystemTime now);
  void ejectHost(HostPtr host, EjectionType type);
  static DetectionStats generateStats(Stats::Scope& scope);
  void initialize(const Cluster& cluster);
  void onConsecutive5xxWorker(HostPtr host);
  void onIntervalTimer();
  void runCallbacks(HostPtr host);

  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  SystemTimeSource& time_source_;
  DetectionStats stats_;
  Event::TimerPtr interval_timer_;
  std::list<ChangeStateCb> callbacks_;
  std::unordered_map<HostPtr, DetectorHostSinkImpl*> host_sinks_;
  EventLoggerPtr event_logger_;
};

class EventLoggerImpl : public EventLogger {
public:
  EventLoggerImpl(AccessLog::AccessLogManager& log_manager, const std::string& file_name,
                  SystemTimeSource& time_source)
      : file_(log_manager.createAccessLog(file_name)), time_source_(time_source) {}

  // Upstream::Outlier::EventLogger
  void logEject(HostDescriptionPtr host, EjectionType type) override;
  void logUneject(HostDescriptionPtr host) override;

private:
  std::string typeToString(EjectionType type);

  Filesystem::FilePtr file_;
  SystemTimeSource& time_source_;
};

} // Outlier
} // Upstream

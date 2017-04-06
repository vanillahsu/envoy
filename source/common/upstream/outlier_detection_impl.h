#pragma once

#include "envoy/access_log/access_log.h"
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
  const Optional<SystemTime>& lastEjectionTime() override { return time_; }
  const Optional<SystemTime>& lastUnejectionTime() override { return time_; }
  double successRate() const override { return -1; }

private:
  const Optional<SystemTime> time_;
};

/**
 * Factory for creating a detector from a JSON configuration.
 */
class DetectorImplFactory {
public:
  static DetectorSharedPtr createForCluster(Cluster& cluster, const Json::Object& cluster_config,
                                            Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                                            EventLoggerSharedPtr event_logger);
};

/**
 * Thin struct to facilitate calculations for success rate outlier detection.
 */
struct HostSuccessRatePair {
  HostSuccessRatePair(HostSharedPtr host, double success_rate)
      : host_(host), success_rate_(success_rate) {}
  HostSharedPtr host_;
  double success_rate_;
};

struct SuccessRateAccumulatorBucket {
  std::atomic<uint64_t> success_request_counter_;
  std::atomic<uint64_t> total_request_counter_;
};

/**
 * The SuccessRateAccumulator uses the SuccessRateAccumulatorBucket to get per host success rate
 * stats. This implementation has a fixed window size of time, and thus only needs a
 * bucket to write to, and a bucket to accumulate/run stats over.
 */
class SuccessRateAccumulator {
public:
  SuccessRateAccumulator()
      : current_success_rate_bucket_(new SuccessRateAccumulatorBucket()),
        backup_success_rate_bucket_(new SuccessRateAccumulatorBucket()) {}

  /**
   * This function updates the bucket to write data to.
   * @return a pointer to the SuccessRateAccumulatorBucket.
   */
  SuccessRateAccumulatorBucket* updateCurrentWriter();
  /**
   * This function returns the success rate of a host over a window of time if the request volume is
   * high enough. The underlying window of time could be dynamically adjusted. In the current
   * implementation it is a fixed time window.
   * @param success_rate_request_volume the threshold of requests an accumulator has to have in
   *                                    order to be able to return a significant success rate value.
   * @return a valid Optional<double> with the success rate. If there were not enough requests, an
   *         invalid Optional<double> is returned.
   */
  Optional<double> getSuccessRate(uint64_t success_rate_request_volume);

private:
  std::unique_ptr<SuccessRateAccumulatorBucket> current_success_rate_bucket_;
  std::unique_ptr<SuccessRateAccumulatorBucket> backup_success_rate_bucket_;
};

class DetectorImpl;

/**
 * Implementation of DetectorHostSink for the generic detector.
 */
class DetectorHostSinkImpl : public DetectorHostSink {
public:
  DetectorHostSinkImpl(std::shared_ptr<DetectorImpl> detector, HostSharedPtr host)
      : detector_(detector), host_(host), success_rate_(-1) {
    // Point the success_rate_accumulator_bucket_ pointer to a bucket.
    updateCurrentSuccessRateBucket();
  }

  void eject(SystemTime ejection_time);
  void uneject(SystemTime ejection_time);
  void updateCurrentSuccessRateBucket();
  SuccessRateAccumulator& successRateAccumulator() { return success_rate_accumulator_; }
  void successRate(double new_success_rate) { success_rate_ = new_success_rate; }

  // Upstream::Outlier::DetectorHostSink
  uint32_t numEjections() override { return num_ejections_; }
  void putHttpResponseCode(uint64_t response_code) override;
  void putResponseTime(std::chrono::milliseconds) override {}
  const Optional<SystemTime>& lastEjectionTime() override { return last_ejection_time_; }
  const Optional<SystemTime>& lastUnejectionTime() override { return last_unejection_time_; }
  double successRate() const override { return success_rate_; }

private:
  std::weak_ptr<DetectorImpl> detector_;
  std::weak_ptr<Host> host_;
  std::atomic<uint32_t> consecutive_5xx_{0};
  Optional<SystemTime> last_ejection_time_;
  Optional<SystemTime> last_unejection_time_;
  uint32_t num_ejections_{};
  SuccessRateAccumulator success_rate_accumulator_;
  std::atomic<SuccessRateAccumulatorBucket*> success_rate_accumulator_bucket_;
  double success_rate_;
};

/**
 * All outlier detection stats. @see stats_macros.h
 */
// clang-format off
#define ALL_OUTLIER_DETECTION_STATS(COUNTER, GAUGE)                                                \
  COUNTER(ejections_total)                                                                         \
  GAUGE  (ejections_active)                                                                        \
  COUNTER(ejections_overflow)                                                                      \
  COUNTER(ejections_consecutive_5xx)                                                               \
  COUNTER(ejections_success_rate)
// clang-format on

/**
 * Struct definition for all outlier detection stats. @see stats_macros.h
 */
struct DetectionStats {
  ALL_OUTLIER_DETECTION_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the outlier detection.
 */
class DetectorConfig {
public:
  DetectorConfig(const Json::Object& json_config);

  uint64_t intervalMs() { return interval_ms_; }
  uint64_t baseEjectionTimeMs() { return base_ejection_time_ms_; }
  uint64_t consecutive5xx() { return consecutive_5xx_; }
  uint64_t maxEjectionPercent() { return max_ejection_percent_; }
  uint64_t successRateMinimumHosts() { return success_rate_minimum_hosts_; }
  uint64_t successRateRequestVolume() { return success_rate_request_volume_; }
  uint64_t enforcingConsecutive5xx() { return enforcing_consecutive_5xx_; }
  uint64_t enforcingSuccessRate() { return enforcing_success_rate_; }

private:
  const uint64_t interval_ms_;
  const uint64_t base_ejection_time_ms_;
  const uint64_t consecutive_5xx_;
  const uint64_t max_ejection_percent_;
  const uint64_t success_rate_minimum_hosts_;
  const uint64_t success_rate_request_volume_;
  const uint64_t enforcing_consecutive_5xx_;
  const uint64_t enforcing_success_rate_;
};

/**
 * An implementation of an outlier detector. In the future we may support multiple outlier detection
 * implementations with different configuration. For now, as we iterate everything is contained
 * within this implementation.
 */
class DetectorImpl : public Detector, public std::enable_shared_from_this<DetectorImpl> {
public:
  static std::shared_ptr<DetectorImpl>
  create(const Cluster& cluster, const Json::Object& json_config, Event::Dispatcher& dispatcher,
         Runtime::Loader& runtime, SystemTimeSource& time_source,
         EventLoggerSharedPtr event_logger);
  ~DetectorImpl();

  void onConsecutive5xx(HostSharedPtr host);
  Runtime::Loader& runtime() { return runtime_; }
  DetectorConfig& config() { return config_; }

  // Upstream::Outlier::Detector
  void addChangedStateCb(ChangeStateCb cb) override { callbacks_.push_back(cb); }
  double successRateAverage() const override { return success_rate_average_; }
  double successRateEjectionThreshold() const override { return success_rate_ejection_threshold_; }

private:
  DetectorImpl(const Cluster& cluster, const Json::Object& json_config,
               Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
               SystemTimeSource& time_source, EventLoggerSharedPtr event_logger);

  void addHostSink(HostSharedPtr host);
  void armIntervalTimer();
  void checkHostForUneject(HostSharedPtr host, DetectorHostSinkImpl* sink, SystemTime now);
  void ejectHost(HostSharedPtr host, EjectionType type);
  static DetectionStats generateStats(Stats::Scope& scope);
  void initialize(const Cluster& cluster);
  void onConsecutive5xxWorker(HostSharedPtr host);
  void onIntervalTimer();
  void runCallbacks(HostSharedPtr host);
  bool enforceEjection(EjectionType type);
  void processSuccessRateEjections();

  DetectorConfig config_;
  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  SystemTimeSource& time_source_;
  DetectionStats stats_;
  Event::TimerPtr interval_timer_;
  std::list<ChangeStateCb> callbacks_;
  std::unordered_map<HostSharedPtr, DetectorHostSinkImpl*> host_sinks_;
  EventLoggerSharedPtr event_logger_;
  double success_rate_average_;
  double success_rate_ejection_threshold_;
};

class EventLoggerImpl : public EventLogger {
public:
  EventLoggerImpl(AccessLog::AccessLogManager& log_manager, const std::string& file_name,
                  SystemTimeSource& time_source)
      : file_(log_manager.createAccessLog(file_name)), time_source_(time_source) {}

  // Upstream::Outlier::EventLogger
  void logEject(HostDescriptionConstSharedPtr host, Detector& detector, EjectionType type,
                bool enforced) override;
  void logUneject(HostDescriptionConstSharedPtr host) override;

private:
  std::string typeToString(EjectionType type);
  int secsSinceLastAction(const Optional<SystemTime>& lastActionTime, SystemTime now);

  Filesystem::FileSharedPtr file_;
  SystemTimeSource& time_source_;
};

/**
 * Utilities for Outlier Detection.
 */
class Utility {
public:
  struct EjectionPair {
    double success_rate_average_;
    double ejection_threshold_;
  };

  /**
   * This function returns an EjectionPair for success rate outlier detection. The pair contains
   * the average success rate of all valid hosts in the cluster and the ejection threshold.
   * If a host's success rate is under this threshold, the host is an outlier.
   * @param success_rate_sum is the sum of the data in the success_rate_data vector.
   * @param valid_success_rate_hosts is the vector containing the individual success rate data
   *        points.
   * @return EjectionPair.
   */
  static EjectionPair
  successRateEjectionThreshold(double success_rate_sum,
                               const std::vector<HostSuccessRatePair>& valid_success_rate_hosts);

private:
  // Factor to multiply the stdev of a cluster's success rate for success rate outlier ejection.
  static const double SUCCESS_RATE_STDEV_FACTOR;
};

} // Outlier
} // Upstream

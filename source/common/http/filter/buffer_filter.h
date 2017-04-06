#pragma once

#include "envoy/http/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"

namespace Http {

/**
 * All stats for the buffer filter. @see stats_macros.h
 */
// clang-format off
#define ALL_BUFFER_FILTER_STATS(COUNTER)                                                           \
  COUNTER(rq_timeout)                                                                              \
  COUNTER(rq_too_large)
// clang-format on

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct BufferFilterStats {
  ALL_BUFFER_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the buffer filter.
 */
struct BufferFilterConfig {
  BufferFilterStats stats_;
  uint64_t max_request_bytes_;
  std::chrono::seconds max_request_time_;
};

typedef std::shared_ptr<const BufferFilterConfig> BufferFilterConfigConstSharedPtr;

/**
 * A filter that is capable of buffering an entire request before dispatching it upstream.
 */
class BufferFilter : public StreamDecoderFilter {
public:
  BufferFilter(BufferFilterConfigConstSharedPtr config);
  ~BufferFilter();

  static BufferFilterStats generateStats(const std::string& prefix, Stats::Store& store);

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

private:
  void onResetStream();
  void onRequestTimeout();
  void resetInternalState();

  BufferFilterConfigConstSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  Event::TimerPtr request_timeout_;
};

} // Http

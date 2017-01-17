#pragma once

#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"

namespace Http {
namespace RateLimit {

/**
 * Global configuration for the HTTP rate limit filter.
 */
class FilterConfig {
public:
  FilterConfig(const Json::Object& config, const LocalInfo::LocalInfo& local_info,
               Stats::Store& global_store, Runtime::Loader& runtime, Upstream::ClusterManager& cm)
      : domain_(config.getString("domain")), stage_(config.getInteger("stage", 0)),
        local_info_(local_info), global_store_(global_store), runtime_(runtime), cm_(cm) {}

  const std::string& domain() const { return domain_; }
  const LocalInfo::LocalInfo& localInfo() const { return local_info_; }
  int64_t stage() const { return stage_; }
  Runtime::Loader& runtime() { return runtime_; }
  Stats::Store& globalStore() { return global_store_; }
  Upstream::ClusterManager& cm() { return cm_; }

private:
  const std::string domain_;
  int64_t stage_;
  const LocalInfo::LocalInfo& local_info_;
  Stats::Store& global_store_;
  Runtime::Loader& runtime_;
  Upstream::ClusterManager& cm_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigPtr;

/**
 * HTTP rate limit filter. Depending on the route configuration, this filter calls the global
 * rate limiting service before allowing further filter iteration.
 */
class Filter : public StreamDecoderFilter, public ::RateLimit::RequestCallbacks {
public:
  Filter(FilterConfigPtr config, ::RateLimit::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // RateLimit::RequestCallbacks
  void complete(::RateLimit::LimitStatus status) override;

private:
  void populateRateLimitDescriptors(const Router::RateLimitPolicy& rate_limit_policy,
                                    std::vector<::RateLimit::Descriptor>& descriptors,
                                    const Router::RouteEntry* route_entry,
                                    const HeaderMap& headers) const;

  enum class State { NotStarted, Calling, Complete, Responded };

  static const Http::HeaderMapPtr TOO_MANY_REQUESTS_HEADER;

  FilterConfigPtr config_;
  ::RateLimit::ClientPtr client_;
  StreamDecoderFilterCallbacks* callbacks_{};
  bool initiating_call_{};
  State state_{State::NotStarted};
  Upstream::ClusterInfoPtr cluster_;
};

} // RateLimit
} // Http

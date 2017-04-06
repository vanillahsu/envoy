#pragma once

#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/rest_api_fetcher.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"

namespace Filter {
namespace Auth {
namespace ClientSsl {

/**
 * All client SSL auth stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CLIENT_SSL_AUTH_STATS(COUNTER, GAUGE)                                                  \
  COUNTER(update_success)                                                                          \
  COUNTER(update_failure)                                                                          \
  COUNTER(auth_no_ssl)                                                                             \
  COUNTER(auth_ip_white_list)                                                                      \
  COUNTER(auth_digest_match)                                                                       \
  COUNTER(auth_digest_no_match)                                                                    \
  GAUGE  (total_principals)
// clang-format on

/**
 * Struct definition for all client SSL auth stats. @see stats_macros.h
 */
struct GlobalStats {
  ALL_CLIENT_SSL_AUTH_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Wraps the principals currently allowed to authenticate.
 */
class AllowedPrincipals : public ThreadLocal::ThreadLocalObject {
public:
  void add(const std::string& sha256_digest) {
    if (!sha256_digest.empty()) {
      allowed_sha256_digests_.emplace(sha256_digest);
    }
  }
  bool allowed(const std::string& sha256_digest) const {
    return allowed_sha256_digests_.count(sha256_digest) != 0;
  }
  size_t size() const { return allowed_sha256_digests_.size(); }

  // ThreadLocal::ThreadLocalObject
  void shutdown() override {}

private:
  std::unordered_set<std::string> allowed_sha256_digests_;
};

typedef std::shared_ptr<AllowedPrincipals> AllowedPrincipalsSharedPtr;

class Config;
typedef std::shared_ptr<Config> ConfigSharedPtr;

/**
 * Global configuration for client SSL authentication. The config contacts a JSON API to fetch the
 * list of allowed principals, caches it, then makes auth decisions on it and any associated IP
 * white list.
 */
class Config : public Http::RestApiFetcher {
public:
  static ConfigSharedPtr create(const Json::Object& config, ThreadLocal::Instance& tls,
                                Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                                Stats::Store& stats_store, Runtime::RandomGenerator& random);

  const AllowedPrincipals& allowedPrincipals();
  const Network::IpList& ipWhiteList() { return ip_white_list_; }
  GlobalStats& stats() { return stats_; }

private:
  Config(const Json::Object& config, ThreadLocal::Instance& tls, Upstream::ClusterManager& cm,
         Event::Dispatcher& dispatcher, Stats::Store& stats_store,
         Runtime::RandomGenerator& random);

  static GlobalStats generateStats(Stats::Store& store, const std::string& prefix);

  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override {}
  void onFetchFailure(EnvoyException* e) override;

  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  Network::IpList ip_white_list_;
  GlobalStats stats_;
};

/**
 * A client SSL auth filter instance. One per connection.
 */
class Instance : public Network::ReadFilter, public Network::ConnectionCallbacks {
public:
  Instance(ConfigSharedPtr config) : config_(config) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
    read_callbacks_->connection().addConnectionCallbacks(*this);
  }

  // Network::ConnectionCallbacks
  void onEvent(uint32_t events) override;

private:
  ConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{};
};

} // ClientSsl
} // Auth
} // Filter

#pragma once

#include "envoy/http/filter.h"
#include "envoy/server/instance.h"

#include "common/common/logger.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/json/json_loader.h"
#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

enum class HttpFilterType { Decoder, Encoder, Both };

/**
 * Config registration for the HTTP connection manager filter. @see NetworkFilterConfigFactory.
 */
class HttpConnectionManagerFilterConfigFactory : Logger::Loggable<Logger::Id::config>,
                                                 public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& config,
                                                Server::Instance& server);
};

/**
 * Callback lambda used for dynamic HTTP filter chain construction.
 */
typedef std::function<void(Http::FilterChainFactoryCallbacks&)> HttpFilterFactoryCb;

/**
 * Implemented by each HTTP filter and registered via registerHttpFilterConfigFactory() or the
 * convenience class RegisterHttpFilterConfigFactory.
 */
class HttpFilterConfigFactory {
public:
  virtual ~HttpFilterConfigFactory() {}

  virtual HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                                     const Json::Object& config,
                                                     const std::string& stat_prefix,
                                                     Server::Instance& server) PURE;
};

/**
 * Utilities for the HTTP connection manager that facilitate testing.
 */
class HttpConnectionManagerConfigUtility {
public:
  /**
   * Determine the next protocol to used based both on ALPN as well as protocol inspection.
   * @param connection supplies the connection to determine a protocol for.
   * @param data supplies the currently available read data on the connection.
   */
  static std::string determineNextProtocol(Network::Connection& connection,
                                           const Buffer::Instance& data);
};

/**
 * Maps JSON config to runtime config for an HTTP connection manager network filter.
 */
class HttpConnectionManagerConfig : Logger::Loggable<Logger::Id::config>,
                                    public Http::FilterChainFactory,
                                    public Http::ConnectionManagerConfig {
public:
  HttpConnectionManagerConfig(const Json::Object& config, Server::Instance& server);

  // Http::FilterChainFactory
  void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;

  // Http::ConnectionManagerConfig
  const std::list<Http::AccessLog::InstanceSharedPtr>& accessLogs() override {
    return access_logs_;
  }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  Http::DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() override { return drain_timeout_; }
  FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() override { return generate_request_id_; }
  const Optional<std::chrono::milliseconds>& idleTimeout() override { return idle_timeout_; }
  Router::RouteConfigProvider& routeConfigProvider() override { return *route_config_provider_; }
  const std::string& serverName() override { return server_name_; }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  const Http::TracingConnectionManagerConfig* tracingConfig() override {
    return tracing_config_.get();
  }
  const Network::Address::Instance& localAddress() override;
  const Optional<std::string>& userAgent() override { return user_agent_; }

  static void registerHttpFilterConfigFactory(HttpFilterConfigFactory& factory) {
    filterConfigFactories().push_back(&factory);
  }

  static const std::string DEFAULT_SERVER_STRING;

private:
  enum class CodecType { HTTP1, HTTP2, AUTO };

  static std::list<HttpFilterConfigFactory*>& filterConfigFactories() {
    static std::list<HttpFilterConfigFactory*> filter_config_factories;
    return filter_config_factories;
  }

  HttpFilterType stringToType(const std::string& type);

  Server::Instance& server_;
  std::list<HttpFilterFactoryCb> filter_factories_;
  std::list<Http::AccessLog::InstanceSharedPtr> access_logs_;
  const std::string stats_prefix_;
  Http::ConnectionManagerStats stats_;
  Http::ConnectionManagerTracingStats tracing_stats_;
  bool use_remote_address_{};
  CodecType codec_type_;
  const uint64_t codec_options_;
  std::string server_name_;
  Http::TracingConnectionManagerConfigPtr tracing_config_;
  Optional<std::string> user_agent_;
  Optional<std::chrono::milliseconds> idle_timeout_;
  Router::RouteConfigProviderPtr route_config_provider_;
  std::chrono::milliseconds drain_timeout_;
  bool generate_request_id_;
  Http::TlsCachingDateProviderImpl date_provider_;
};

/**
 * @see HttpFilterConfigFactory.
 */
template <class T> class RegisterHttpFilterConfigFactory {
public:
  RegisterHttpFilterConfigFactory() {
    static T instance;
    HttpConnectionManagerConfig::registerHttpFilterConfigFactory(instance);
  }
};

} // Configuration
} // Server

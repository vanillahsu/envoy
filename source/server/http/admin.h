#pragma once

#include "envoy/http/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/admin.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

#include "common/common/logger.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/utility.h"
#include "server/config/network/http_connection_manager.h"

namespace Server {

/**
 * Implementation of Server::admin.
 */
class AdminImpl : public Admin,
                  public Network::FilterChainFactory,
                  public Http::FilterChainFactory,
                  public Http::ConnectionManagerConfig,
                  Logger::Loggable<Logger::Id::admin> {
public:
  AdminImpl(const std::string& access_log_path, const std::string& profiler_path,
            Network::Address::InstanceConstSharedPtr address, Server::Instance& server);

  Http::Code runCallback(const std::string& path, Buffer::Instance& response);
  const Network::ListenSocket& socket() override { return *socket_; }
  Network::ListenSocket& mutable_socket() { return *socket_; }

  // Server::Admin
  void addHandler(const std::string& prefix, const std::string& help_text,
                  HandlerCb callback) override {
    handlers_.push_back({prefix, help_text, callback});
  }

  // Network::FilterChainFactory
  bool createFilterChain(Network::Connection& connection) override;

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
  std::chrono::milliseconds drainTimeout() override { return std::chrono::milliseconds(100); }
  Http::FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() override { return false; }
  const Optional<std::chrono::milliseconds>& idleTimeout() override { return idle_timeout_; }
  Router::RouteConfigProvider& routeConfigProvider() override { return route_config_provider_; }
  const std::string& serverName() override {
    return Server::Configuration::HttpConnectionManagerConfig::DEFAULT_SERVER_STRING;
  }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return true; }
  const Network::Address::Instance& localAddress() override;
  const Optional<std::string>& userAgent() override { return user_agent_; }
  const Http::TracingConnectionManagerConfig* tracingConfig() override { return nullptr; }

private:
  /**
   * Individual admin handler including prefix, help text, and callback.
   */
  struct UrlHandler {
    const std::string prefix_;
    const std::string help_text_;
    const HandlerCb handler_;
  };

  /**
   * Implementation of RouteConfigProvider that returns a static null route config.
   */
  struct NullRouteConfigProvider : public Router::RouteConfigProvider {
    NullRouteConfigProvider();

    // Router::RouteConfigProvider
    Router::ConfigConstSharedPtr config() override { return config_; }

    Router::ConfigConstSharedPtr config_;
  };

  /**
   * Attempt to change the log level of a logger or all loggers
   * @param params supplies the incoming endpoint query params.
   * @return TRUE if level change succeeded, FALSE otherwise.
   */
  bool changeLogLevel(const Http::Utility::QueryParams& params);
  void addCircuitSettings(const std::string& cluster_name, const std::string& priority_str,
                          Upstream::ResourceManager& resource_manager, Buffer::Instance& response);
  void addOutlierInfo(const std::string& cluster_name,
                      const Upstream::Outlier::Detector* outlier_detector,
                      Buffer::Instance& response);

  /**
   * URL handlers.
   */
  Http::Code handlerCerts(const std::string& url, Buffer::Instance& response);
  Http::Code handlerClusters(const std::string& url, Buffer::Instance& response);
  Http::Code handlerCpuProfiler(const std::string& url, Buffer::Instance& response);
  Http::Code handlerHealthcheckFail(const std::string& url, Buffer::Instance& response);
  Http::Code handlerHealthcheckOk(const std::string& url, Buffer::Instance& response);
  Http::Code handlerHotRestartVersion(const std::string& url, Buffer::Instance& response);
  Http::Code handlerLogging(const std::string& url, Buffer::Instance& response);
  Http::Code handlerResetCounters(const std::string& url, Buffer::Instance& response);
  Http::Code handlerServerInfo(const std::string& url, Buffer::Instance& response);
  Http::Code handlerStats(const std::string& url, Buffer::Instance& response);
  Http::Code handlerQuitQuitQuit(const std::string& url, Buffer::Instance& response);

  Server::Instance& server_;
  std::list<Http::AccessLog::InstanceSharedPtr> access_logs_;
  const std::string profile_path_;
  Network::ListenSocketPtr socket_;
  Http::ConnectionManagerStats stats_;
  Http::ConnectionManagerTracingStats tracing_stats_;
  NullRouteConfigProvider route_config_provider_;
  std::list<UrlHandler> handlers_;
  Optional<std::chrono::milliseconds> idle_timeout_;
  Optional<std::string> user_agent_;
  Http::SlowDateProviderImpl date_provider_;
};

/**
 * A terminal HTTP filter that implements server admin functionality.
 */
class AdminFilter : public Http::StreamDecoderFilter, Logger::Loggable<Logger::Id::admin> {
public:
  AdminFilter(AdminImpl& parent);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

private:
  /**
   * Called when an admin request has been completely received.
   */
  void onComplete();

  AdminImpl& parent_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::HeaderMap* request_headers_{};
};

} // Server

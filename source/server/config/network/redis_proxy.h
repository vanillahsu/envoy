#pragma once

#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the redis proxy filter. @see NetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& config,
                                                Server::Instance& server);
};

} // Configuration
} // Server

#include "redis_proxy.h"

#include "common/redis/codec_impl.h"
#include "common/redis/conn_pool_impl.h"
#include "common/redis/proxy_filter.h"

namespace Server {
namespace Configuration {

NetworkFilterFactoryCb RedisProxyFilterConfigFactory::tryCreateFilterFactory(
    NetworkFilterType type, const std::string& name, const Json::Object& config,
    Server::Instance& server) {
  if (type != NetworkFilterType::Read || name != "redis_proxy") {
    return nullptr;
  }

  Redis::ProxyFilterConfig filter_config(config, server.clusterManager());
  std::shared_ptr<Redis::ConnPool::Instance> conn_pool(new Redis::ConnPool::InstanceImpl(
      filter_config.clusterName(), server.clusterManager(),
      Redis::ConnPool::ClientFactoryImpl::instance_, server.threadLocal()));
  return [conn_pool](Network::FilterManager& filter_manager) -> void {
    Redis::DecoderFactoryImpl factory;
    filter_manager.addReadFilter(Network::ReadFilterPtr{
        new Redis::ProxyFilter(factory, Redis::EncoderPtr{new Redis::EncoderImpl()}, *conn_pool)});
  };
}

/**
 * Static registration for the redis filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<RedisProxyFilterConfigFactory> registered_;

} // Configuration
} // Server

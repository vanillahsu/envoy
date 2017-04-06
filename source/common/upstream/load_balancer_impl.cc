#include "common/upstream/load_balancer_impl.h"

#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"

namespace Upstream {

static const std::string RuntimeZoneEnabled = "upstream.zone_routing.enabled";
static const std::string RuntimeMinClusterSize = "upstream.zone_routing.min_cluster_size";
static const std::string RuntimePanicThreshold = "upstream.healthy_panic_threshold";

LoadBalancerBase::LoadBalancerBase(const HostSet& host_set, const HostSet* local_host_set,
                                   ClusterStats& stats, Runtime::Loader& runtime,
                                   Runtime::RandomGenerator& random)
    : stats_(stats), runtime_(runtime), random_(random), host_set_(host_set),
      local_host_set_(local_host_set) {
  if (local_host_set_) {
    host_set_.addMemberUpdateCb(
        [this](const std::vector<HostSharedPtr>&, const std::vector<HostSharedPtr>&)
            -> void { regenerateZoneRoutingStructures(); });
    local_host_set_->addMemberUpdateCb(
        [this](const std::vector<HostSharedPtr>&, const std::vector<HostSharedPtr>&)
            -> void { regenerateZoneRoutingStructures(); });
  }
}

void LoadBalancerBase::regenerateZoneRoutingStructures() {
  stats_.lb_recalculate_zone_structures_.inc();

  // Do not perform any calculations if we cannot perform zone routing based on non runtime params.
  if (earlyExitNonZoneRouting()) {
    zone_routing_state_ = ZoneRoutingState::NoZoneRouting;
    return;
  }

  size_t num_zones = host_set_.healthyHostsPerZone().size();

  uint64_t local_percentage[num_zones];
  calculateZonePercentage(local_host_set_->healthyHostsPerZone(), local_percentage);

  uint64_t upstream_percentage[num_zones];
  calculateZonePercentage(host_set_.healthyHostsPerZone(), upstream_percentage);

  // If we have lower percent of hosts in the local cluster in the same zone,
  // we can push all of the requests directly to upstream cluster in the same zone.
  if (upstream_percentage[0] >= local_percentage[0]) {
    zone_routing_state_ = ZoneRoutingState::ZoneDirect;
    return;
  }

  zone_routing_state_ = ZoneRoutingState::ZoneResidual;

  // If we cannot route all requests to the same zone, calculate what percentage can be routed.
  // For example, if local percentage is 20% and upstream is 10%
  // we can route only 50% of requests directly.
  local_percent_to_route_ = upstream_percentage[0] * 10000 / local_percentage[0];

  // Local zone does not have additional capacity (we have already routed what we could).
  // Now we need to figure out how much traffic we can route cross zone and to which exact zone
  // we should route. Percentage of requests routed cross zone to a specific zone needed be
  // proportional to the residual capacity upstream zone has.
  //
  // residual_capacity contains capacity left in a given zone, we keep accumulating residual
  // capacity to make search for sampled value easier.
  // For example, if we have the following upstream and local percentage:
  // local_percentage: 40000 40000 20000
  // upstream_percentage: 25000 50000 25000
  // Residual capacity would look like: 0 10000 5000. Now we need to sample proportionally to
  // bucket sizes (residual capacity). For simplicity of finding where specific
  // sampled value is, we accumulate values in residual capacity. This is what it will look like:
  // residual_capacity: 0 10000 15000
  // Now to find a zone to route (bucket) we could simply iterate over residual_capacity searching
  // where sampled value is placed.
  residual_capacity_.resize(num_zones);

  // Local zone (index 0) does not have residual capacity as we have routed all we could.
  residual_capacity_[0] = 0;
  for (size_t i = 1; i < num_zones; ++i) {
    // Only route to the zones that have additional capacity.
    if (upstream_percentage[i] > local_percentage[i]) {
      residual_capacity_[i] =
          residual_capacity_[i - 1] + upstream_percentage[i] - local_percentage[i];
    } else {
      // Zone with index "i" does not have residual capacity, but we keep accumulating previous
      // values to make search easier on the next step.
      residual_capacity_[i] = residual_capacity_[i - 1];
    }
  }
};

bool LoadBalancerBase::earlyExitNonZoneRouting() {
  if (host_set_.healthyHostsPerZone().size() < 2) {
    return true;
  }

  if (host_set_.healthyHostsPerZone()[0].empty()) {
    return true;
  }

  // Same number of zones should be for local and upstream cluster.
  if (host_set_.healthyHostsPerZone().size() != local_host_set_->healthyHostsPerZone().size()) {
    stats_.lb_zone_number_differs_.inc();
    return true;
  }

  // Do not perform zone routing for small clusters.
  uint64_t min_cluster_size = runtime_.snapshot().getInteger(RuntimeMinClusterSize, 6U);
  if (host_set_.healthyHosts().size() < min_cluster_size) {
    stats_.lb_zone_cluster_too_small_.inc();
    return true;
  }

  return false;
}

bool LoadBalancerUtility::isGlobalPanic(const HostSet& host_set, ClusterStats& stats,
                                        Runtime::Loader& runtime) {
  uint64_t global_panic_threshold =
      std::min<uint64_t>(100, runtime.snapshot().getInteger(RuntimePanicThreshold, 50));
  double healthy_percent = 100.0 * host_set.healthyHosts().size() / host_set.hosts().size();

  // If the % of healthy hosts in the cluster is less than our panic threshold, we use all hosts.
  if (healthy_percent < global_panic_threshold) {
    stats.lb_healthy_panic_.inc();
    return true;
  }

  return false;
}

void LoadBalancerBase::calculateZonePercentage(
    const std::vector<std::vector<HostSharedPtr>>& hosts_per_zone, uint64_t* ret) {
  uint64_t total_hosts = 0;
  for (const auto& zone_hosts : hosts_per_zone) {
    total_hosts += zone_hosts.size();
  }

  if (total_hosts != 0) {
    size_t i = 0;
    for (const auto& zone_hosts : hosts_per_zone) {
      ret[i++] = 10000ULL * zone_hosts.size() / total_hosts;
    }
  }
}

const std::vector<HostSharedPtr>& LoadBalancerBase::tryChooseLocalZoneHosts() {
  ASSERT(zone_routing_state_ != ZoneRoutingState::NoZoneRouting);

  // At this point it's guaranteed to be at least 2 zones.
  size_t number_of_zones = host_set_.healthyHostsPerZone().size();

  ASSERT(number_of_zones >= 2U);
  ASSERT(local_host_set_->healthyHostsPerZone().size() == host_set_.healthyHostsPerZone().size());

  // Try to push all of the requests to the same zone first.
  if (zone_routing_state_ == ZoneRoutingState::ZoneDirect) {
    stats_.lb_zone_routing_all_directly_.inc();
    return host_set_.healthyHostsPerZone()[0];
  }

  ASSERT(zone_routing_state_ == ZoneRoutingState::ZoneResidual);

  // If we cannot route all requests to the same zone, we already calculated how much we can
  // push to the local zone, check if we can push to local zone on current iteration.
  if (random_.random() % 10000 < local_percent_to_route_) {
    stats_.lb_zone_routing_sampled_.inc();
    return host_set_.healthyHostsPerZone()[0];
  }

  // At this point we must route cross zone as we cannot route to the local zone.
  stats_.lb_zone_routing_cross_zone_.inc();

  // This is *extremely* unlikely but possible due to rounding errors when calculating
  // zone percentages. In this case just select random zone.
  if (residual_capacity_[number_of_zones - 1] == 0) {
    stats_.lb_zone_no_capacity_left_.inc();
    return host_set_.healthyHostsPerZone()[random_.random() % number_of_zones];
  }

  // Random sampling to select specific zone for cross zone traffic based on the additional
  // capacity in zones.
  uint64_t threshold = random_.random() % residual_capacity_[number_of_zones - 1];

  // This potentially can be optimized to be O(log(N)) where N is the number of zones.
  // Linear scan should be faster for smaller N, in most of the scenarios N will be small.
  int i = 0;
  while (threshold > residual_capacity_[i]) {
    i++;
  }

  return host_set_.healthyHostsPerZone()[i];
}

const std::vector<HostSharedPtr>& LoadBalancerBase::hostsToUse() {
  ASSERT(host_set_.healthyHosts().size() <= host_set_.hosts().size());

  if (host_set_.hosts().empty() ||
      LoadBalancerUtility::isGlobalPanic(host_set_, stats_, runtime_)) {
    return host_set_.hosts();
  }

  if (zone_routing_state_ == ZoneRoutingState::NoZoneRouting) {
    return host_set_.healthyHosts();
  }

  if (!runtime_.snapshot().featureEnabled(RuntimeZoneEnabled, 100)) {
    return host_set_.healthyHosts();
  }

  if (local_host_set_->hosts().empty() ||
      LoadBalancerUtility::isGlobalPanic(*local_host_set_, stats_, runtime_)) {
    stats_.lb_local_cluster_not_ok_.inc();
    return host_set_.healthyHosts();
  }

  return tryChooseLocalZoneHosts();
}

HostConstSharedPtr RoundRobinLoadBalancer::chooseHost(const LoadBalancerContext*) {
  const std::vector<HostSharedPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[rr_index_++ % hosts_to_use.size()];
}

LeastRequestLoadBalancer::LeastRequestLoadBalancer(const HostSet& host_set,
                                                   const HostSet* local_host_set,
                                                   ClusterStats& stats, Runtime::Loader& runtime,
                                                   Runtime::RandomGenerator& random)
    : LoadBalancerBase(host_set, local_host_set, stats, runtime, random) {
  host_set.addMemberUpdateCb([this](const std::vector<HostSharedPtr>&,
                                    const std::vector<HostSharedPtr>& hosts_removed) -> void {
    if (last_host_) {
      for (const HostSharedPtr& host : hosts_removed) {
        if (host == last_host_) {
          hits_left_ = 0;
          last_host_.reset();

          break;
        }
      }
    }
  });
}

HostConstSharedPtr LeastRequestLoadBalancer::chooseHost(const LoadBalancerContext*) {
  bool is_weight_imbalanced = stats_.max_host_weight_.value() != 1;
  bool is_weight_enabled = runtime_.snapshot().getInteger("upstream.weight_enabled", 1UL) != 0;

  if (is_weight_imbalanced && hits_left_ > 0 && is_weight_enabled) {
    --hits_left_;

    return last_host_;
  } else {
    // To avoid hit stale last_host_ when all hosts become weight balanced.
    hits_left_ = 0;
    last_host_.reset();
  }

  const std::vector<HostSharedPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  // Make weighed random if we have hosts with non 1 weights.
  if (is_weight_imbalanced & is_weight_enabled) {
    last_host_ = hosts_to_use[random_.random() % hosts_to_use.size()];
    hits_left_ = last_host_->weight() - 1;

    return last_host_;
  } else {
    HostSharedPtr host1 = hosts_to_use[random_.random() % hosts_to_use.size()];
    HostSharedPtr host2 = hosts_to_use[random_.random() % hosts_to_use.size()];
    if (host1->stats().rq_active_.value() < host2->stats().rq_active_.value()) {
      return host1;
    } else {
      return host2;
    }
  }
}

HostConstSharedPtr RandomLoadBalancer::chooseHost(const LoadBalancerContext*) {
  const std::vector<HostSharedPtr>& hosts_to_use = hostsToUse();
  if (hosts_to_use.empty()) {
    return nullptr;
  }

  return hosts_to_use[random_.random() % hosts_to_use.size()];
}

} // Upstream

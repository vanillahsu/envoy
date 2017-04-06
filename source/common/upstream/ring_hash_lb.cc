#include "common/upstream/ring_hash_lb.h"

#include "common/common/assert.h"
#include "common/upstream/load_balancer_impl.h"

namespace Upstream {

RingHashLoadBalancer::RingHashLoadBalancer(HostSet& host_set, ClusterStats& stats,
                                           Runtime::Loader& runtime,
                                           Runtime::RandomGenerator& random)
    : host_set_(host_set), stats_(stats), runtime_(runtime), random_(random) {
  host_set_.addMemberUpdateCb([this](const std::vector<HostSharedPtr>&,
                                     const std::vector<HostSharedPtr>&) -> void { refresh(); });

  refresh();
}

HostConstSharedPtr RingHashLoadBalancer::chooseHost(const LoadBalancerContext* context) {
  if (LoadBalancerUtility::isGlobalPanic(host_set_, stats_, runtime_)) {
    return all_hosts_ring_.chooseHost(context, random_);
  } else {
    return healthy_hosts_ring_.chooseHost(context, random_);
  }
}

HostConstSharedPtr RingHashLoadBalancer::Ring::chooseHost(const LoadBalancerContext* context,
                                                          Runtime::RandomGenerator& random) {
  if (ring_.empty()) {
    return nullptr;
  }

  // If there is no hash in the context, just choose a random value (this effectively becomes
  // the random LB but it won't crash if someone configures it this way).
  uint64_t h;
  if (!context || !context->hashKey().valid()) {
    h = random.random();
  } else {
    h = context->hashKey().value();
  }

  // Ported from https://github.com/RJ/ketama/blob/master/libketama/ketama.c (ketama_get_server)
  // I've generally kept the variable names to make the code easier to compare.
  // NOTE: The algorithm depends on using signed integers for lowp, midp, and highp. Do not
  //       change them!
  int64_t lowp = 0;
  int64_t highp = ring_.size();
  while (true) {
    int64_t midp = (lowp + highp) / 2;

    if (midp == static_cast<int64_t>(ring_.size())) {
      return ring_[0].host_;
    }

    uint64_t midval = ring_[midp].hash_;
    uint64_t midval1 = midp == 0 ? 0 : ring_[midp - 1].hash_;

    if (h <= midval && h > midval1) {
      return ring_[midp].host_;
    }

    if (midval < h) {
      lowp = midp + 1;
    } else {
      highp = midp - 1;
    }

    if (lowp > highp) {
      return ring_[0].host_;
    }
  }
}

void RingHashLoadBalancer::Ring::create(Runtime::Loader& runtime,
                                        const std::vector<HostSharedPtr>& hosts) {
  log_trace("ring hash: building ring");
  ring_.clear();
  if (hosts.empty()) {
    return;
  }

  // Currently we specify the minimum size of the ring, and determine the replication factor
  // based on the number of hosts. It's possible we might want to support more sophisticated
  // configuration in the future.
  // NOTE: Currently we keep a ring for healthy hosts and unhealthy hosts, and this is done per
  //       thread. This is the simplest implementation, but it's expensive from a memory
  //       standpoint and duplicates the regeneration computation. In the future we might want
  //       to generate the rings centrally and then just RCU them out to each thread. This is
  //       sufficient for getting started.
  uint64_t min_ring_size = runtime.snapshot().getInteger("upstream.ring_hash.min_ring_size", 1024);

  uint64_t hashes_per_host = 1;
  if (hosts.size() < min_ring_size) {
    hashes_per_host = min_ring_size / hosts.size();
    if ((min_ring_size % hosts.size()) != 0) {
      hashes_per_host++;
    }
  }

  log_trace("ring hash: min_ring_size={} hashes_per_host={}", min_ring_size, hashes_per_host);
  ring_.reserve(hosts.size() * hashes_per_host);
  for (auto host : hosts) {
    for (uint64_t i = 0; i < hashes_per_host; i++) {
      std::string hash_key(host->address()->asString() + "_" + std::to_string(i));
      uint64_t hash = std::hash<std::string>()(hash_key);
      log_trace("ring hash: hash_key={} hash={}", hash_key, hash);
      ring_.push_back({hash, host});
    }
  }

  std::sort(ring_.begin(), ring_.end(), [](const RingEntry& lhs, const RingEntry& rhs)
                                            -> bool { return lhs.hash_ < rhs.hash_; });
#ifndef NDEBUG
  for (auto entry : ring_) {
    log_trace("ring hash: host={} hash={}", entry.host_->address()->asString(), entry.hash_);
  }
#endif
}

void RingHashLoadBalancer::refresh() {
  all_hosts_ring_.create(runtime_, host_set_.hosts());
  healthy_hosts_ring_.create(runtime_, host_set_.healthyHosts());
}

} // Upstream

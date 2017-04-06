#pragma once

namespace Upstream {

/**
 * A thread local cluster instance that can be used for direct load balancing and host set
 * interactions. In general, an instance of ThreadLocalCluster can only be safely used in the
 * direct call context after it is retrieved from the cluster manager. See ClusterManager::get()
 * for more information.
 */
class ThreadLocalCluster {
public:
  virtual ~ThreadLocalCluster() {}

  /**
   * @return const HostSet& the backing host set.
   */
  virtual const HostSet& hostSet() PURE;

  /**
   * @return ClusterInfoConstSharedPtr the info for this cluster. The info is safe to store beyond
   * the
   *         lifetime of the ThreadLocalCluster instance itself.
   */
  virtual ClusterInfoConstSharedPtr info() PURE;

  /**
   * @return LoadBalancer& the backing load balancer.
   */
  virtual LoadBalancer& loadBalancer() PURE;
};

} // Upstream

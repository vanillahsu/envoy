#pragma once

namespace Upstream {

/**
 * Global configuration for any SDS clusters.
 */
struct SdsConfig {
  std::string local_zone_name_;
  std::string sds_cluster_name_;
  std::chrono::milliseconds refresh_delay_;
};

} // Upstream

#pragma once

#include "envoy/common/optional.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/resource_manager.h"

namespace Router {

/**
 * A routing primitive that creates a redirect path.
 */
class RedirectEntry {
public:
  virtual ~RedirectEntry() {}

  /**
   * Returns the redirect path based on the request headers.
   * @param headers supplies the request headers.
   * @return std::string the redirect URL.
   */
  virtual std::string newPath(const Http::HeaderMap& headers) const PURE;
};

/**
 * Route level retry policy.
 */
class RetryPolicy {
public:
  // clang-format off
  static const uint32_t RETRY_ON_5XX             = 0x1;
  static const uint32_t RETRY_ON_CONNECT_FAILURE = 0x2;
  static const uint32_t RETRY_ON_RETRIABLE_4XX   = 0x4;
  static const uint32_t RETRY_ON_REFUSED_STREAM  = 0x8;
  // clang-format on

  virtual ~RetryPolicy() {}

  /**
   * @return uint32_t the number of retries to allow against the route.
   */
  virtual uint32_t numRetries() const PURE;

  /**
   * @return uint32_t a local OR of RETRY_ON values above.
   */
  virtual uint32_t retryOn() const PURE;
};

/**
 * Wraps retry state for an active routed request.
 */
class RetryState {
public:
  typedef std::function<void()> DoRetryCallback;

  virtual ~RetryState() {}

  /**
   * @return true if a policy is in place for the active request that allows retries.
   */
  virtual bool enabled() PURE;

  /**
   * Determine whether a request should be retried based on the response.
   * @param response_headers supplies the response headers if available.
   * @param reset_reason supplies the reset reason if available.
   * @param callback supplies the callback that will be invoked when the retry should take place.
   *                 This is used to add timed backoff, etc. The callback will never be called
   *                 inline.
   * @return TRUE if a retry should take place. @param callback will be called at some point in the
   *         future. Otherwise a retry should not take place and the callback will never be called.
   *         Calling code should proceed with error handling.
   */
  virtual bool shouldRetry(const Http::HeaderMap* response_headers,
                           const Optional<Http::StreamResetReason>& reset_reason,
                           DoRetryCallback callback) PURE;
};

typedef std::unique_ptr<RetryState> RetryStatePtr;

/**
 * Per route policy for request shadowing.
 */
class ShadowPolicy {
public:
  virtual ~ShadowPolicy() {}

  /**
   * @return the name of the cluster that a matching request should be shadowed to. Returns empty
   *         string if no shadowing should take place.
   */
  virtual const std::string& cluster() const PURE;

  /**
   * @return the runtime key that will be used to determine whether an individual request should
   *         be shadowed. The lack of a key means that all requests will be shadowed. If a key is
   *         present it will be used to drive random selection in the range 0-10000 for 0.01%
   *         increments.
   */
  virtual const std::string& runtimeKey() const PURE;
};

/**
 * Virtual cluster definition (allows splitting a virtual host into virtual clusters orthogonal to
 * routes for stat tracking and priority purposes).
 */
class VirtualCluster {
public:
  virtual ~VirtualCluster() {}

  /**
   * @return the name of the virtual cluster.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return the priority of the virtual cluster.
   */
  virtual Upstream::ResourcePriority priority() const PURE;
};

class RateLimitPolicy;

/**
 * Virtual host defintion.
 */
class VirtualHost {
public:
  virtual ~VirtualHost() {}

  /**
   * @return const std::string& the name of the virtual host.
   */
  virtual const std::string& name() const PURE;

  /**
   * @return const RateLimitPolicy& the rate limit policy for the virtual host.
   */
  virtual const RateLimitPolicy& rateLimitPolicy() const PURE;
};

/**
 * Route hash policy. I.e., if using a hashing load balancer, how the route should be hashed onto
 * an upstream host.
 */
class HashPolicy {
public:
  virtual ~HashPolicy() {}

  /**
   * @return Optional<uint64_t> an optional hash value to route on given a set of HTTP headers.
   *         A hash value might not be returned if for example the specified HTTP header does not
   *         exist. In the future we might add additional support for hashing on origin address,
   *         etc.
   */
  virtual Optional<uint64_t> generateHash(const Http::HeaderMap& headers) const PURE;
};

/**
 * An individual resolved route entry.
 */
class RouteEntry {
public:
  virtual ~RouteEntry() {}

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * Do potentially destructive header transforms on request headers prior to forwarding. For
   * example URL prefix rewriting, adding headers, etc. This should only be called ONCE
   * immediately prior to forwarding. It is done this way vs. copying for performance reasons.
   * @param headers supplies the request headers, which may be modified during this call.
   */
  virtual void finalizeRequestHeaders(Http::HeaderMap& headers) const PURE;

  /**
   * @return const HashPolicy* the optional hash policy for the route.
   */
  virtual const HashPolicy* hashPolicy() const PURE;

  /**
   * @return the priority of the route.
   */
  virtual Upstream::ResourcePriority priority() const PURE;

  /**
   * @return const RateLimitPolicy& the rate limit policy for the route.
   */
  virtual const RateLimitPolicy& rateLimitPolicy() const PURE;

  /**
   * @return const RetryPolicy& the retry policy for the route. All routes have a retry policy even
   *         if it is empty and does not allow retries.
   */
  virtual const RetryPolicy& retryPolicy() const PURE;

  /**
   * @return const ShadowPolicy& the shadow policy for the route. All routes have a shadow policy
   *         even if no shadowing takes place.
   */
  virtual const ShadowPolicy& shadowPolicy() const PURE;

  /**
   * @return std::chrono::milliseconds the route's timeout.
   */
  virtual std::chrono::milliseconds timeout() const PURE;

  /**
   * Determine whether a specific request path belongs to a virtual cluster for use in stats, etc.
   * @param headers supplies the request headers.
   * @return the virtual cluster or nullptr if there is no match.
   */
  virtual const VirtualCluster* virtualCluster(const Http::HeaderMap& headers) const PURE;

  /**
   * @return const VirtualHost& the virtual host that owns the route.
   */
  virtual const VirtualHost& virtualHost() const PURE;

  /**
   * @return bool true if the :authority header should be overwritten with the upstream hostname.
   */
  virtual bool autoHostRewrite() const PURE;

  /**
   * @return const std::multimap<std::string, std::string> the opaque configuration associated
   *         with the route
   */
  virtual const std::multimap<std::string, std::string>& opaqueConfig() const PURE;
};

/**
 * An interface that holds a RedirectEntry or a RouteEntry for a request.
 */
class Route {
public:
  virtual ~Route() {}

  /**
   * @return the redirect entry or nullptr if there is no redirect needed for the request.
   */
  virtual const RedirectEntry* redirectEntry() const PURE;

  /**
   * @return the route entry or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeEntry() const PURE;
};

typedef std::shared_ptr<const Route> RouteConstSharedPtr;

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() {}

  /**
   * Based on the incoming HTTP request headers, determine the target route (containing either a
   * route entry or a redirect entry) for the request.
   * @param headers supplies the request headers.
   * @param random_value supplies the random seed to use if a runtime choice is required. This
   *        allows stable choices between calls if desired.
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(const Http::HeaderMap& headers,
                                    uint64_t random_value) const PURE;

  /**
   * Return a list of headers that will be cleaned from any requests that are not from an internal
   * (RFC1918) source.
   */
  virtual const std::list<Http::LowerCaseString>& internalOnlyHeaders() const PURE;

  /**
   * Return a list of header key/value pairs that will be added to every response that transits the
   * router.
   */
  virtual const std::list<std::pair<Http::LowerCaseString, std::string>>&
  responseHeadersToAdd() const PURE;

  /**
   * Return a list of upstream headers that will be stripped from every response that transits the
   * router.
   */
  virtual const std::list<Http::LowerCaseString>& responseHeadersToRemove() const PURE;

  /**
   * Return whether the configuration makes use of runtime or not. Callers can use this to
   * determine whether they should use a fast or slow source of randomness when calling route
   * functions.
   */
  virtual bool usesRuntime() const PURE;
};

typedef std::shared_ptr<const Config> ConfigConstSharedPtr;

} // Router

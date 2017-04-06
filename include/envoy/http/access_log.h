#pragma once

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/upstream/upstream.h"

namespace Http {
namespace AccessLog {

enum ResponseFlag {
  // Local server healthcheck failed.
  FailedLocalHealthCheck = 0x1,
  // No healthy upstream.
  NoHealthyUpstream = 0x2,
  // Request timeout on upstream.
  UpstreamRequestTimeout = 0x4,
  // Local codec level reset was sent on the stream.
  LocalReset = 0x8,
  // Remote codec level reset was received on the stream.
  UpstreamRemoteReset = 0x10,
  // Local reset by a connection pool due to an initial connection failure.
  UpstreamConnectionFailure = 0x20,
  // If the stream was locally reset due to connection termination.
  UpstreamConnectionTermination = 0x40,
  // The stream was reset because of a resource overflow.
  UpstreamOverflow = 0x80,
  // No route found for a given request.
  NoRouteFound = 0x100,
  // Request was delayed before proxying.
  DelayInjected = 0x200,
  // Abort with error code was injected.
  FaultInjected = 0x400,
  // Request was ratelimited locally by rate limit filter.
  RateLimited = 0x800
};

/**
 * Additional information about a completed request for logging.
 */
class RequestInfo {
public:
  virtual ~RequestInfo() {}

  /**
   * Each filter can set independent response flag, flags are accumulated.
   */
  virtual void setResponseFlag(ResponseFlag response_flag) PURE;

  /**
   * Filter can trigger this callback when an upstream host has been selected.
   */
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * @return the time that the first byte of the request was received.
   */
  virtual SystemTime startTime() const PURE;

  /**
   * @return the # of body bytes received in the request.
   */
  virtual uint64_t bytesReceived() const PURE;

  /**
   * @return the protocol of the request.
   */
  virtual Protocol protocol() const PURE;

  /**
   * Set the request's protocol.
   */
  virtual void protocol(Protocol protocol) PURE;

  /**
   * @return the response code.
   */
  virtual const Optional<uint32_t>& responseCode() const PURE;

  /**
   * @return the # of body bytes sent in the response.
   */
  virtual uint64_t bytesSent() const PURE;

  /**
   * @return the milliseconds duration of the first byte received to the last byte sent.
   */
  virtual std::chrono::milliseconds duration() const PURE;

  /**
   * @return whether response flag is set or not.
   */
  virtual bool getResponseFlag(ResponseFlag response_flag) const PURE;

  /**
   * @return upstream host description.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() const PURE;

  /**
   * @return whether the request is a health check request or not.
   */
  virtual bool healthCheck() const PURE;

  /**
   * Set whether the request is a health check request or not.
   */
  virtual void healthCheck(bool is_hc) PURE;
};

/**
 * Interface for access log filters.
 */
class Filter {
public:
  virtual ~Filter() {}

  /**
   * Evaluate whether an access log should be written based on request and response data.
   * @return TRUE if the log should be written.
   */
  virtual bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) PURE;
};

typedef std::unique_ptr<Filter> FilterPtr;

/**
 * Abstract access logger for HTTP requests and responses.
 */
class Instance {
public:
  virtual ~Instance() {}

  /**
   * Log a completed request.
   * @param request_headers supplies the incoming request headers after filtering.
   * @param response_headers supplies response headers.
   * @param request_info supplies additional information about the request not contained in
   *                      the request headers.
   */
  virtual void log(const HeaderMap* request_headers, const HeaderMap* response_headers,
                   const RequestInfo& request_info) PURE;
};

typedef std::shared_ptr<Instance> InstanceSharedPtr;

/**
 * Interface for access log formatter.
 */
class Formatter {
public:
  virtual ~Formatter() {}

  virtual std::string format(const Http::HeaderMap& request_headers,
                             const Http::HeaderMap& response_headers,
                             const RequestInfo& request_info) const PURE;
};

typedef std::unique_ptr<Formatter> FormatterPtr;

} // AccessLog
} // Http

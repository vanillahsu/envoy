#pragma once

#include "envoy/router/router.h"
#include "envoy/router/router_ratelimit.h"

#include "common/http/filter/ratelimit.h"
#include "common/json/json_validator.h"
#include "common/router/config_utility.h"

namespace Router {

/**
 * Action for source cluster rate limiting.
 */
class SourceClusterAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;
};

/**
 * Action for destination cluster rate limiting.
 */
class DestinationClusterAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;
};

/**
* Action for request headers rate limiting.
*/
class RequestHeadersAction : public RateLimitAction {
public:
  RequestHeadersAction(const Json::Object& action)
      : header_name_(action.getString("header_name")),
        descriptor_key_(action.getString("descriptor_key")) {}

  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;

private:
  const Http::LowerCaseString header_name_;
  const std::string descriptor_key_;
};

/**
 * Action for remote address rate limiting.
 */
class RemoteAddressAction : public RateLimitAction {
public:
  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;
};

/**
 * Action for generic key rate limiting.
 */
class GenericKeyAction : public RateLimitAction {
public:
  GenericKeyAction(const Json::Object& action)
      : descriptor_value_(action.getString("descriptor_value")) {}

  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;

private:
  const std::string descriptor_value_;
};

/**
 * Action for header value match rate limiting.
 */
class HeaderValueMatchAction : public RateLimitAction {
public:
  HeaderValueMatchAction(const Json::Object& action);

  // Router::RateLimitAction
  bool populateDescriptor(const Router::RouteEntry& route, ::RateLimit::Descriptor& descriptor,
                          const std::string& local_service_cluster, const Http::HeaderMap& headers,
                          const std::string& remote_address) const override;

private:
  const std::string descriptor_value_;
  std::vector<Router::ConfigUtility::HeaderData> action_headers_;
};

/*
 * Implementation of RateLimitPolicyEntry that holds the action for the configuration.
 */
class RateLimitPolicyEntryImpl : public RateLimitPolicyEntry, Json::Validator {
public:
  RateLimitPolicyEntryImpl(const Json::Object& config);

  // Router::RateLimitPolicyEntry
  uint64_t stage() const override { return stage_; }
  const std::string& disableKey() const override { return disable_key_; }
  void populateDescriptors(const Router::RouteEntry& route,
                           std::vector<::RateLimit::Descriptor>& descriptors,
                           const std::string& local_service_cluster, const Http::HeaderMap&,
                           const std::string& remote_address) const override;

private:
  const std::string disable_key_;
  uint64_t stage_;
  std::vector<RateLimitActionPtr> actions_;
};

/**
 * Implementation of RateLimitPolicy that reads from the JSON route config.
 */
class RateLimitPolicyImpl : public RateLimitPolicy {
public:
  RateLimitPolicyImpl(const Json::Object& config);

  // Router::RateLimitPolicy
  const std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>&
  getApplicableRateLimit(uint64_t stage = 0) const override;

private:
  std::vector<std::unique_ptr<RateLimitPolicyEntry>> rate_limit_entries_;
  std::vector<std::vector<std::reference_wrapper<const RateLimitPolicyEntry>>>
      rate_limit_entries_reference_;
  // The maximum stage number supported. This value should match the maximum stage number in
  // Json::Schema::HTTP_RATE_LIMITS_CONFIGURATION_SCHEMA and
  // Json::Schema::RATE_LIMIT_HTTP_FILTER_SCHEMA from common/json/config_schemas.cc.
  static const uint64_t MAX_STAGE_NUMBER;
};

} // Router

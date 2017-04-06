#pragma once

#include "envoy/ratelimit/ratelimit.h"
#include "envoy/tracing/context.h"

namespace RateLimit {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient();

  // RateLimit::Client
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD4(limit, void(RequestCallbacks& callbacks, const std::string& domain,
                           const std::vector<Descriptor>& descriptors,
                           const Tracing::TransportContext& context));
};

inline bool operator==(const DescriptorEntry& lhs, const DescriptorEntry& rhs) {
  return lhs.key_ == rhs.key_ && lhs.value_ == rhs.value_;
}

inline bool operator==(const Descriptor& lhs, const Descriptor& rhs) {
  return lhs.entries_ == rhs.entries_;
}

} // RateLimit
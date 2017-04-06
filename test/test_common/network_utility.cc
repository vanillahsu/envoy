#include "test/test_common/network_utility.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"
#include "test/test_common/utility.h"

namespace Network {
namespace Test {

Address::InstanceConstSharedPtr findOrCheckFreePort(Address::InstanceConstSharedPtr addr_port,
                                                    Address::SocketType type) {
  if (addr_port == nullptr || addr_port->type() != Address::Type::Ip) {
    ADD_FAILURE() << "Not an internet address: " << (addr_port == nullptr ? "nullptr"
                                                                          : addr_port->asString());
    return nullptr;
  }
  const int fd = addr_port->socket(type);
  if (fd < 0) {
    const int err = errno;
    ADD_FAILURE() << "socket failed for '" << addr_port->asString()
                  << "' with error: " << strerror(err) << " (" << err << ")";
    return nullptr;
  }
  ScopedFdCloser closer(fd);
  // Not setting REUSEADDR, therefore if the address has been recently used we won't reuse it here.
  // However, because we're going to use the address while checking if it is available, we'll need
  // to set REUSEADDR on listener sockets created by tests using an address validated by this means.
  int rc = addr_port->bind(fd);
  const char* failing_fn = nullptr;
  if (rc != 0) {
    failing_fn = "bind";
  } else if (type == Address::SocketType::Stream) {
    // Try listening on the port also, if the type is TCP.
    rc = ::listen(fd, 1);
    if (rc != 0) {
      failing_fn = "listen";
    }
  }
  if (failing_fn != nullptr) {
    const int err = errno;
    if (err == EADDRINUSE) {
      // The port is already in use. Perfectly normal.
      return nullptr;
    } else if (err == EACCES) {
      // A privileged port, and we don't have privileges. Might want to log this.
      return nullptr;
    }
    // Unexpected failure.
    ADD_FAILURE() << failing_fn << " failed for '" << addr_port->asString()
                  << "' with error: " << strerror(err) << " (" << err << ")";
    return nullptr;
  }
  // If the port we bind is zero, then the OS will pick a free port for us (assuming there are
  // any), and we need to find out the port number that the OS picked so we can return it.
  if (addr_port->ip()->port() == 0) {
    return Address::addressFromFd(fd);
  }
  return addr_port;
}

Address::InstanceConstSharedPtr findOrCheckFreePort(const std::string& addr_port,
                                                    Address::SocketType type) {
  auto instance = Address::parseInternetAddressAndPort(addr_port);
  if (instance != nullptr) {
    instance = findOrCheckFreePort(instance, type);
  } else {
    ADD_FAILURE() << "Unable to parse as an address and port: " << addr_port;
  }
  return instance;
}

Address::InstanceConstSharedPtr getSomeLoopbackAddress(Address::IpVersion version) {
  if (version == Address::IpVersion::v4) {
    // Pick a random address in 127.0.0.0/8.
    // TODO(jamessynge): Consider how to use $GTEST_RANDOM_SEED for seeding the rng.
    // Perhaps we need a TestRuntime::getRandomGenerator() or similar.
    Runtime::RandomGeneratorImpl rng;
    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = 0;
    sin.sin_addr.s_addr = static_cast<uint32_t>(rng.random() % 0xffffffff);
    uint8_t* address_bytes = reinterpret_cast<uint8_t*>(&sin.sin_addr.s_addr);
    address_bytes[0] = 127;
    return std::make_shared<Address::Ipv4Instance>(&sin);
  } else {
    // There is only one IPv6 loopback address.
    return Network::Utility::getIpv6LoopbackAddress();
  }
}

std::pair<Address::InstanceConstSharedPtr, int> bindFreeLoopbackPort(Address::IpVersion version,
                                                                     Address::SocketType type) {
  Address::InstanceConstSharedPtr addr = getSomeLoopbackAddress(version);
  const char* failing_fn = nullptr;
  const int fd = addr->socket(type);
  if (fd < 0) {
    failing_fn = "socket";
  } else if (0 != addr->bind(fd)) {
    failing_fn = "bind";
  } else {
    return std::make_pair(Address::addressFromFd(fd), fd);
  }
  const int err = errno;
  if (fd >= 0) {
    close(fd);
  }
  std::string msg = fmt::format("{} failed for address {} with error: {} ({})", failing_fn,
                                addr->asString(), strerror(err), err);
  ADD_FAILURE() << msg;
  throw EnvoyException(msg);
}

} // Test
} // Network

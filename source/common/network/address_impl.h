#pragma once

#include "envoy/network/address.h"

#include <sys/un.h>

namespace Network {
namespace Address {

/**
 * Convert an address in the form of the socket address struct defined by Posix, Linux, etc. into
 * a Network::Address::Instance and return a pointer to it.  Raises an EnvoyException on failure.
 * @param ss a valid address with family AF_INET, AF_INET6 or AF_UNIX.
 * @param len length of the address (e.g. from accept, getsockname or getpeername). If len > 0,
 *        it is used to validate the structure contents; else if len == 0, it is ignored.
 * @return InstanceConstSharedPtr the address.
 */
Address::InstanceConstSharedPtr addressFromSockAddr(const sockaddr_storage& ss, socklen_t len);

/**
 * Obtain an address from a bound file descriptor. Raises an EnvoyException on failure.
 * @param fd file descriptor.
 * @return InstanceConstSharedPtr for bound address.
 */
InstanceConstSharedPtr addressFromFd(int fd);

/**
 * Obtain the address of the peer of the socket with the specified file descriptor.
 * Raises an EnvoyException on failure.
 * @param fd file descriptor.
 * @return InstanceConstSharedPtr for peer address.
 */
InstanceConstSharedPtr peerAddressFromFd(int fd);

/**
 * Base class for all address types.
 */
class InstanceBase : public Instance {
public:
  // Network::Address::Instance
  bool operator==(const Instance& rhs) const override { return asString() == rhs.asString(); }
  const std::string& asString() const override { return friendly_name_; }
  Type type() const override { return type_; }

protected:
  InstanceBase(Type type) : type_(type) {}
  int flagsFromSocketType(SocketType type) const;

  std::string friendly_name_;

private:
  const Type type_;
};

/**
 * Implementation of an IPv4 address.
 */
class Ipv4Instance : public InstanceBase {
public:
  /**
   * Construct from an existing unix IPv4 socket address (IP v4 address and port).
   */
  explicit Ipv4Instance(const sockaddr_in* address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4". Port will be unset/0.
   */
  explicit Ipv4Instance(const std::string& address);

  /**
   * Construct from a string IPv4 address such as "1.2.3.4" as well as a port.
   */
  Ipv4Instance(const std::string& address, uint32_t port);

  /**
   * Construct from a port. The IPv4 address will be set to "any" and is suitable for binding
   * a port to any available address.
   */
  explicit Ipv4Instance(uint32_t port);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return &ip_; }
  int socket(SocketType type) const override;

private:
  struct Ipv4Helper : public Ipv4 {
    uint32_t address() const override { return address_.sin_addr.s_addr; }

    sockaddr_in address_;
  };

  struct IpHelper : public Ip {
    const std::string& addressAsString() const override { return friendly_address_; }
    bool isAnyAddress() const override { return ipv4_.address_.sin_addr.s_addr == INADDR_ANY; }
    const Ipv4* ipv4() const override { return &ipv4_; }
    const Ipv6* ipv6() const override { return nullptr; }
    uint32_t port() const override { return ntohs(ipv4_.address_.sin_port); }
    IpVersion version() const override { return IpVersion::v4; }

    Ipv4Helper ipv4_;
    std::string friendly_address_;
  };

  IpHelper ip_;
};

/**
 * Implementation of an IPv6 address.
 */
class Ipv6Instance : public InstanceBase {
public:
  /**
   * Construct from an existing unix IPv6 socket address (IP v6 address and port).
   */
  explicit Ipv6Instance(const sockaddr_in6& address);

  /**
   * Construct from a string IPv6 address such as "12:34::5". Port will be unset/0.
   */
  explicit Ipv6Instance(const std::string& address);

  /**
   * Construct from a string IPv6 address such as "12:34::5" as well as a port.
   */
  Ipv6Instance(const std::string& address, uint32_t port);

  /**
   * Construct from a port. The IPv6 address will be set to "any" and is suitable for binding
   * a port to any available address.
   */
  explicit Ipv6Instance(uint32_t port);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return &ip_; }
  int socket(SocketType type) const override;

private:
  struct Ipv6Helper : public Ipv6 {
    std::array<uint8_t, 16> address() const override;
    uint32_t port() const;

    std::string makeFriendlyAddress() const;

    sockaddr_in6 address_;
  };

  struct IpHelper : public Ip {
    const std::string& addressAsString() const override { return friendly_address_; }
    bool isAnyAddress() const override {
      return 0 == memcmp(&ipv6_.address_.sin6_addr, &in6addr_any, sizeof(struct in6_addr));
    }
    const Ipv4* ipv4() const override { return nullptr; }
    const Ipv6* ipv6() const override { return &ipv6_; }
    uint32_t port() const override { return ipv6_.port(); }
    IpVersion version() const override { return IpVersion::v6; }

    Ipv6Helper ipv6_;
    std::string friendly_address_;
  };

  IpHelper ip_;
};

/*
 * Parse an internet host address (IPv4 or IPv6) and create an Instance from it.
 * The address must not include a port number.
 * @param ip_addr string to be parsed as an internet address.
 * @return pointer to the Instance, or nullptr if unable to parse the address.
 */
InstanceConstSharedPtr parseInternetAddress(const std::string& ip_addr);

/*
 * Parse an internet host address (IPv4 or IPv6) AND port, and create an Instance from it.
 * @param ip_addr string to be parsed as an internet address and port. Examples:
 *        - "1.2.3.4:80"
 *        - "[1234:5678::9]:443"
 * @return pointer to the Instance, or nullptr if unable to parse the address.
 */
InstanceConstSharedPtr parseInternetAddressAndPort(const std::string& ip_addr);

/**
 * Implementation of a pipe address (unix domain socket on unix).
 */
class PipeInstance : public InstanceBase {
public:
  /**
   * Construct from an existing unix address.
   */
  explicit PipeInstance(const sockaddr_un* address);

  /**
   * Construct from a string pipe path.
   */
  explicit PipeInstance(const std::string& pipe_path);

  // Network::Address::Instance
  int bind(int fd) const override;
  int connect(int fd) const override;
  const Ip* ip() const override { return nullptr; }
  int socket(SocketType type) const override;

private:
  sockaddr_un address_;
};

} // Address
} // Network

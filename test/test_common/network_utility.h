#pragma once

#include "envoy/network/address.h"

namespace Network {
namespace Test {

/**
 * Determines if the passed in address and port is available for binding. If the port is zero,
 * the OS should pick an unused port for the supplied address (e.g. for the loopback address).
 * NOTE: this is racy, as it does not provide a means to keep the port reserved for the
 * caller's use.
 * @param addr_port a valid host address (e.g. an address of one of the network interfaces
 *        of this host, or the any address or the loopback address) and port (zero to indicate
 *        that the OS should pick an unused address.
 * @param type the type of socket to be tested.
 * @returns the address and port (selected if zero was the passed in port) that can be used for
 *          listening, else nullptr if the address and port are not free.
 */
Address::InstanceConstSharedPtr findOrCheckFreePort(Address::InstanceConstSharedPtr addr_port,
                                                    Address::SocketType type);

/**
 * As above, but addr_port is specified as a string. For example:
 *    - 127.0.0.1:32000  Check whether a specific port on the IPv4 loopback address is free.
 *    - [::1]:0          Pick a free port on the IPv6 loopback address.
 *    - 0.0.0.0:0        Pick a free port on all local addresses of all local interfaces.
 *    - [::]:45678       Check whether a specific port on all local IPv6 addresses is free.
 */
Address::InstanceConstSharedPtr findOrCheckFreePort(const std::string& addr_port,
                                                    Address::SocketType type);

/**
 * Returns a loopback address for the specified IP version. For IPv6 this is always the same,
 * but for IPv4 it is anywhere in the range 127.0.0.0/8.
 * @param version the IP version of the loopback address.
 * @returns a loopback address for the specified IP version.
 */
Address::InstanceConstSharedPtr getSomeLoopbackAddress(Address::IpVersion version);

/**
 * Bind a socket to a free port on a loopback address, and return the socket's fd and bound address.
 * Enables a test server to reliably "select" a port to listen on. Note that the socket option
 * SO_REUSEADDR has NOT been set on the socket.
 * @param version the IP version of the loopback address.
 * @param type the type of socket to be bound.
 * @returns the address and the fd of the socket bound to that address.
 */
std::pair<Address::InstanceConstSharedPtr, int> bindFreeLoopbackPort(Address::IpVersion version,
                                                                     Address::SocketType type);

} // Test
} // Network

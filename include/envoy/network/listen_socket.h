#pragma once

#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Network {

/**
 * An abstract listen socket.
 */
class ListenSocket {
public:
  virtual ~ListenSocket() {}

  /**
   * @return the address that the socket is listening on.
   */
  virtual Address::InstanceConstSharedPtr localAddress() const PURE;

  /**
   * @return fd the listen socket's file descriptor.
   */
  virtual int fd() PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;
};

typedef std::unique_ptr<ListenSocket> ListenSocketPtr;

} // Network

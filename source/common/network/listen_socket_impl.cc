#include "common/network/listen_socket_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Network {

void ListenSocketImpl::doBind() {
  int rc = local_address_->bind(fd_);
  if (rc == -1) {
    close();
    throw EnvoyException(
        fmt::format("cannot bind '{}': {}", local_address_->asString(), strerror(errno)));
  }
  if (local_address_->type() == Address::Type::Ip && local_address_->ip()->port() == 0) {
    // If the port we bind is zero, then the OS will pick a free port for us (assuming there are
    // any), and we need to find out the port number that the OS picked.
    local_address_ = Address::addressFromFd(fd_);
  }
}

TcpListenSocket::TcpListenSocket(Address::InstanceConstSharedPtr address, bool bind_to_port) {
  local_address_ = address;
  fd_ = local_address_->socket(Address::SocketType::Stream);
  RELEASE_ASSERT(fd_ != -1);

  int on = 1;
  int rc = setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  RELEASE_ASSERT(rc != -1);

  if (bind_to_port) {
    doBind();
  }
}

TcpListenSocket::TcpListenSocket(int fd, Address::InstanceConstSharedPtr address) {
  fd_ = fd;
  local_address_ = address;
}

UdsListenSocket::UdsListenSocket(const std::string& uds_path) {
  remove(uds_path.c_str());
  local_address_.reset(new Address::PipeInstance(uds_path));
  fd_ = local_address_->socket(Address::SocketType::Stream);
  RELEASE_ASSERT(fd_ != -1);
  doBind();
}

} // Network

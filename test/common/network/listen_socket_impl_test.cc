#include "envoy/common/exception.h"

#include "common/network/utility.h"
#include "common/network/listen_socket_impl.h"

#include "test/test_common/network_utility.h"

namespace Network {

class ListenSocketImplTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  ListenSocketImplTest() : version_(GetParam()) {}
  const Address::IpVersion version_;
};
INSTANTIATE_TEST_CASE_P(IpVersions, ListenSocketImplTest,
                        testing::Values(Address::IpVersion::v4, Address::IpVersion::v6));

TEST_P(ListenSocketImplTest, BindSpecificPort) {
  // Pick a free port.
  auto addr_fd = Network::Test::bindFreeLoopbackPort(version_, Address::SocketType::Stream);
  auto addr = addr_fd.first;
  EXPECT_LE(0, addr_fd.second);

  // Confirm that we got a reasonable address and port.
  ASSERT_EQ(Address::Type::Ip, addr->type());
  ASSERT_EQ(version_, addr->ip()->version());
  ASSERT_LT(0U, addr->ip()->port());

  // Release the socket and re-bind it.
  // WARNING: This test has a small but real risk of flaky behavior if another thread or process
  // should bind to our assigned port during the interval between closing the fd and re-binding.
  // TODO(jamessynge): Consider adding a loop or other such approach to this test so that a
  // bind failure (in the TcpListenSocket ctor) once isn't considered an error.
  EXPECT_EQ(0, close(addr_fd.second));
  TcpListenSocket socket1(addr, true);
  EXPECT_EQ(0, listen(socket1.fd(), 0));
  EXPECT_EQ(addr->ip()->port(), socket1.localAddress()->ip()->port());
  EXPECT_EQ(addr->ip()->addressAsString(), socket1.localAddress()->ip()->addressAsString());

  // The address and port are bound already, should throw exception.
  EXPECT_THROW(Network::TcpListenSocket socket2(addr, true), EnvoyException);

  // Test the case of a socket with fd and given address and port.
  TcpListenSocket socket3(dup(socket1.fd()), addr);
  EXPECT_EQ(addr->asString(), socket3.localAddress()->asString());
}

// Validate that we get port allocation when binding to port zero.
TEST_P(ListenSocketImplTest, BindPortZero) {
  auto loopback = Network::Test::getSomeLoopbackAddress(version_);
  TcpListenSocket socket(loopback, true);
  EXPECT_EQ(Address::Type::Ip, socket.localAddress()->type());
  EXPECT_EQ(version_, socket.localAddress()->ip()->version());
  EXPECT_EQ(loopback->ip()->addressAsString(), socket.localAddress()->ip()->addressAsString());
  EXPECT_GT(socket.localAddress()->ip()->port(), 0U);
}

} // Network

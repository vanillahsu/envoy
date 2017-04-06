#include "envoy/common/exception.h"

#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Network {

TEST(IpListTest, Errors) {
  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["foo"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpList wl(*loader, "ip_white_list"); }, EnvoyException);
  }

  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["foo/bar"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpList wl(*loader, "ip_white_list"); }, EnvoyException);
  }

  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["192.168.1.1/33"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpList wl(*loader, "ip_white_list"); }, EnvoyException);
  }

  {
    std::string json = R"EOF(
    {
      "ip_white_list": ["192.168.1.1/24"]
    }
    )EOF";

    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    EXPECT_THROW({ IpList wl(*loader, "ip_white_list"); }, EnvoyException);
  }
}

TEST(IpListTest, Normal) {
  std::string json = R"EOF(
  {
    "ip_white_list": [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16"
     ]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  IpList wl(*loader, "ip_white_list");

  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.168.3.0")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.168.3.3")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.168.3.255")));
  EXPECT_FALSE(wl.contains(Address::Ipv4Instance("192.168.2.255")));
  EXPECT_FALSE(wl.contains(Address::Ipv4Instance("192.168.4.0")));

  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("50.1.2.3")));
  EXPECT_FALSE(wl.contains(Address::Ipv4Instance("50.1.2.2")));
  EXPECT_FALSE(wl.contains(Address::Ipv4Instance("50.1.2.4")));

  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("10.15.0.0")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("10.15.90.90")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("10.15.255.255")));
  EXPECT_FALSE(wl.contains(Address::Ipv4Instance("10.14.255.255")));
  EXPECT_FALSE(wl.contains(Address::Ipv4Instance("10.16.0.0")));

  EXPECT_FALSE(wl.contains(Address::PipeInstance("foo")));
}

TEST(IpListTest, MatchAny) {
  std::string json = R"EOF(
  {
    "ip_white_list": [
      "0.0.0.0/0"
     ]
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  IpList wl(*loader, "ip_white_list");

  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.168.3.3")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.168.3.0")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.168.3.255")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.168.0.0")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("192.0.0.0")));
  EXPECT_TRUE(wl.contains(Address::Ipv4Instance("1.1.1.1")));

  EXPECT_FALSE(wl.contains(Address::PipeInstance("foo")));
}

TEST(NetworkUtility, Url) {
  EXPECT_EQ("foo", Utility::hostFromTcpUrl("tcp://foo:1234"));
  EXPECT_EQ(1234U, Utility::portFromTcpUrl("tcp://foo:1234"));
  EXPECT_THROW(Utility::hostFromTcpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://foo:bar"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl(""), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("foo"), EnvoyException);
}

TEST(NetworkUtility, getLocalAddress) { EXPECT_NE(nullptr, Utility::getLocalAddress()); }

TEST(NetworkUtility, getOriginalDst) { EXPECT_EQ(nullptr, Utility::getOriginalDst(-1)); }

TEST(NetworkUtility, loopbackAddress) {
  {
    Address::Ipv4Instance address("127.0.0.1");
    EXPECT_TRUE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv4Instance address("10.0.0.1");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  {
    Address::PipeInstance address("/foo");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  EXPECT_EQ("127.0.0.1:0", Utility::getCanonicalIpv4LoopbackAddress()->asString());
  EXPECT_EQ("[::1]:0", Utility::getIpv6LoopbackAddress()->asString());
}

TEST(NetworkUtility, AnyAddress) {
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv4AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v4);
    EXPECT_EQ(any->asString(), "0.0.0.0:0");
    EXPECT_EQ(any, Utility::getIpv4AnyAddress());
  }
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv6AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v6);
    EXPECT_EQ(any->asString(), "[::]:0");
    EXPECT_EQ(any, Utility::getIpv6AnyAddress());
  }
}

TEST(PortRangeListTest, Errors) {
  {
    std::string port_range_str = "a1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1A";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1_1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1,1X1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1,1*1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }
}

static Address::Ipv4Instance makeFromPort(uint32_t port) {
  return Address::Ipv4Instance("0.0.0.0", port);
}

TEST(PortRangeListTest, Normal) {
  {
    std::string port_range_str = "1";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(Address::PipeInstance("/foo"), port_range_list));
  }

  {
    std::string port_range_str = "1024-2048";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1024), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(2048), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1536), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(1023), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2049), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(0), port_range_list));
  }

  {
    std::string port_range_str = "1,10-100,1000-10000,65535";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(50), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(5000), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(65535), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(200), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(20000), port_range_list));
  }
}

} // Network

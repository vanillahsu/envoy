#include "common/common/utility.h"
#include "server/options_impl.h"

// Do the ugly work of turning a std::string into a char** and create an OptionsImpl. Args are
// separated by a single space: no fancy quoting or escaping.
std::unique_ptr<OptionsImpl> createOptionsImpl(const std::string& args) {
  std::vector<std::string> words = StringUtil::split(args, ' ');
  std::vector<const char*> argv;
  for (const std::string& s : words) {
    argv.push_back(s.c_str());
  }
  return std::unique_ptr<OptionsImpl>(
      new OptionsImpl(argv.size(), const_cast<char**>(&argv[0]), "1", spdlog::level::warn));
}

TEST(OptionsImplDeathTest, HotRestartVersion) {
  EXPECT_EXIT(createOptionsImpl("envoy --hot-restart-version"), testing::ExitedWithCode(0), "");
}

TEST(OptionsImplTest, All) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl(
      "envoy --concurrency 2 -c hello --restart-epoch 1 -l info "
      "--service-cluster cluster --service-node node --service-zone zone "
      "--file-flush-interval-msec 9000 --drain-time-s 60 --parent-shutdown-time-s 90");
  EXPECT_EQ(2U, options->concurrency());
  EXPECT_EQ("hello", options->configPath());
  EXPECT_EQ(1U, options->restartEpoch());
  EXPECT_EQ(spdlog::level::info, options->logLevel());
  EXPECT_EQ("cluster", options->serviceClusterName());
  EXPECT_EQ("node", options->serviceNodeName());
  EXPECT_EQ("zone", options->serviceZone());
  EXPECT_EQ(std::chrono::milliseconds(9000), options->fileFlushIntervalMsec());
  EXPECT_EQ(std::chrono::seconds(60), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(90), options->parentShutdownTime());
}

TEST(OptionsImplTest, DefaultParams) {
  std::unique_ptr<OptionsImpl> options = createOptionsImpl("envoy -c hello");
  EXPECT_EQ(std::chrono::seconds(600), options->drainTime());
  EXPECT_EQ(std::chrono::seconds(900), options->parentShutdownTime());
}

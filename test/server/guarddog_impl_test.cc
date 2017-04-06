#include "envoy/common/time.h"

#include "common/common/utility.h"
#include "common/stats/stats_impl.h"

#include "server/guarddog_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"

using testing::InSequence;
using testing::NiceMock;

namespace Server {

/**
 * Death test caveat: Because of the way we die gcov doesn't receive coverage
 * information from the forked process that is checked for succesful death.
 * This means that the lines dealing with the calls to PANIC are not seen as
 * green in the coverage report. However, rest assured from the results of the
 * test: these lines are in fact covered.
 */
class GuardDogDeathTest : public testing::Test {
protected:
  GuardDogDeathTest()
      : config_kill_(1000, 1000, 100, 1000), config_multikill_(1000, 1000, 1000, 500),
        time_point_(std::chrono::system_clock::now()) {}

  /**
   * This does everything but the final forceCheckForTest() that should cause
   * death for the single kill case.
   */
  void SetupForDeath() {
    InSequence s;
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    guard_dog_.reset(new GuardDogImpl(fakestats_, config_kill_, time_source_));
    unpet_dog_ = guard_dog_->createWatchDog(0);
    guard_dog_->forceCheckForTest();
    time_point_ += std::chrono::milliseconds(500);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  }

  /**
   * This does everything but the final forceCheckForTest() that should cause
   * death for the multiple kill case.
   */
  void SetupForMultiDeath() {
    InSequence s;
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    guard_dog_.reset(new GuardDogImpl(fakestats_, config_multikill_, time_source_));
    auto unpet_dog_ = guard_dog_->createWatchDog(0);
    guard_dog_->forceCheckForTest();
    auto second_dog_ = guard_dog_->createWatchDog(1);
    guard_dog_->forceCheckForTest();
    time_point_ += std::chrono::milliseconds(501);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  }

  NiceMock<Configuration::MockMain> config_kill_;
  NiceMock<Configuration::MockMain> config_multikill_;
  NiceMock<Stats::MockStore> fakestats_;
  MockSystemTimeSource time_source_;
  std::chrono::system_clock::time_point time_point_;
  std::unique_ptr<GuardDogImpl> guard_dog_;
  WatchDogSharedPtr unpet_dog_;
  WatchDogSharedPtr second_dog_;
};

TEST_F(GuardDogDeathTest, KillDeathTest) {
  // Is it German for "The Function"? Almost...
  auto die_function = [&]() -> void {
    SetupForDeath();
    guard_dog_->forceCheckForTest();
  };
  // Why do it this way? Any threads must be started inside the death test
  // statement and this is the easiest way to accomplish that.
  EXPECT_DEATH(die_function(), "");
}

TEST_F(GuardDogDeathTest, KillNoFinalCheckTest) {
  // This does everything the death test does except the final force check that
  // should actually result in dying. The death test does not verify that there
  // was not a crash *before* the expected line, so this test checks that.
  SetupForDeath();
}

TEST_F(GuardDogDeathTest, MultiKillDeathTest) {
  auto die_function = [&]() -> void {
    SetupForMultiDeath();
    guard_dog_->forceCheckForTest();
  };
  EXPECT_DEATH(die_function(), "");
}

TEST_F(GuardDogDeathTest, MultiKillNoFinalCheckTest) {
  // This does everything the death test does except the final force check that
  // should actually result in dying. The death test does not verify that there
  // was not a crash *before* the expected line, so this test checks that.
  SetupForMultiDeath();
}

TEST_F(GuardDogDeathTest, NearDeathTest) {
  // This ensures that if only one thread surpasses the multiple kill threshold
  // there is no death.  The positive case is covered in MultiKillDeathTest.
  InSequence s;
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  GuardDogImpl gd(fakestats_, config_multikill_, time_source_);
  auto unpet_dog = gd.createWatchDog(0);
  auto pet_dog = gd.createWatchDog(1);
  // This part "waits" 600 milliseconds while one dog is touched every 100, and
  // the other is not.  600ms is over the threshold of 500ms for multi-kill but
  // only one is nonresponsive, so there should be no kill (single kill
  // threshold of 1s is not reached).
  for (int i = 0; i < 6; i++) {
    time_point_ += std::chrono::milliseconds(100);
    EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
    pet_dog->touch();
    gd.forceCheckForTest();
  }
}

class GuardDogMissTest : public testing::Test {
protected:
  GuardDogMissTest() : config_miss_(500, 1000, 0, 0), config_mega_(1000, 500, 0, 0) {}

  NiceMock<Configuration::MockMain> config_miss_;
  NiceMock<Configuration::MockMain> config_mega_;
  Stats::IsolatedStoreImpl stats_store_;
  MockSystemTimeSource time_source_;
  std::chrono::system_clock::time_point time_point_;
};

TEST_F(GuardDogMissTest, MissTest) {
  // This test checks the actual collected statistics after doing some timer
  // advances that should and shouldn't increment the counters.
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  GuardDogImpl gd(stats_store_, config_miss_, time_source_);
  // We'd better start at 0:
  EXPECT_EQ(0UL, stats_store_.counter("server.watchdog_miss").value());
  auto unpet_dog = gd.createWatchDog(0);
  // At 300ms we shouldn't have hit the timeout yet:
  time_point_ += std::chrono::milliseconds(300);
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  gd.forceCheckForTest();
  EXPECT_EQ(0UL, stats_store_.counter("server.watchdog_miss").value());
  // This should push it past the 500ms limit:
  time_point_ += std::chrono::milliseconds(250);
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  gd.forceCheckForTest();
  EXPECT_EQ(1UL, stats_store_.counter("server.watchdog_miss").value());
  gd.stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST_F(GuardDogMissTest, MegaMissTest) {
  // This test checks the actual collected statistics after doing some timer
  // advances that should and shouldn't increment the counters.
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  GuardDogImpl gd(stats_store_, config_mega_, time_source_);
  auto unpet_dog = gd.createWatchDog(0);
  // We'd better start at 0:
  EXPECT_EQ(0UL, stats_store_.counter("server.watchdog_mega_miss").value());
  // This shouldn't be enough to increment the stat:
  time_point_ += std::chrono::milliseconds(499);
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  gd.forceCheckForTest();
  EXPECT_EQ(0UL, stats_store_.counter("server.watchdog_mega_miss").value());
  // Just 2ms more will make it greater than 500ms timeout:
  time_point_ += std::chrono::milliseconds(2);
  EXPECT_CALL(time_source_, currentSystemTime()).WillRepeatedly(testing::Return(time_point_));
  gd.forceCheckForTest();
  EXPECT_EQ(1UL, stats_store_.counter("server.watchdog_mega_miss").value());
  gd.stopWatching(unpet_dog);
  unpet_dog = nullptr;
}

TEST(GuardDogBasicTest, StartStopTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(0, 0, 0, 0);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
}

TEST(GuardDogBasicTest, LoopIntervalNoKillTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(40, 50, 0, 0);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  EXPECT_EQ(gd.loopIntervalForTest(), 40);
}

TEST(GuardDogBasicTest, LoopIntervalTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(100, 90, 1000, 500);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  EXPECT_EQ(gd.loopIntervalForTest(), 90);
}

TEST(WatchDogBasicTest, ThreadIdTest) {
  NiceMock<Stats::MockStore> stats;
  NiceMock<Configuration::MockMain> config(100, 90, 1000, 500);
  NiceMock<MockSystemTimeSource> time_source;
  GuardDogImpl gd(stats, config, time_source);
  auto watched_dog = gd.createWatchDog(123);
  EXPECT_EQ(watched_dog->threadId(), 123);
  gd.stopWatching(watched_dog);
}

// If this test fails it is because the std::chrono::system_clock::duration type has become
// nontrivial or we are compiling under a compiler and library combo that makes
// std::chrono::system_clock::duration require a lock to be atomicly modified.
//
// The WatchDog/GuardDog relies on this being a lock free atomic for perf reasons so some workaround
// will be required if this test starts failing.
TEST(WatchDogTimeTest, AtomicIsAtomicTest) {
  ProdSystemTimeSource time_source;
  std::atomic<std::chrono::system_clock::duration> atomic_time;
  ASSERT_EQ(atomic_time.is_lock_free(), true);
}

} // Server

#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/optional.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/common/thread.h"

#include <dirent.h>

namespace Runtime {

/**
 * Implementation of RandomGenerator that uses per-thread RANLUX generators seeded with current
 * time.
 */
class RandomGeneratorImpl : public RandomGenerator {
public:
  // Runtime::RandomGenerator
  uint64_t random() override { return threadLocalGenerator()(); }
  std::string uuid() override;

  static const size_t UUID_LENGTH;

private:
  static std::ranlux48& threadLocalGenerator() {
    std::chrono::nanoseconds now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    static thread_local std::ranlux48 generator(now.count() ^ Thread::Thread::currentThreadId());

    return generator;
  }
};

/**
 * All runtime stats. @see stats_macros.h
 */
// clang-format off
#define ALL_RUNTIME_STATS(COUNTER, GAUGE)                                                          \
  COUNTER(load_error)                                                                              \
  COUNTER(override_dir_not_exists)                                                                 \
  COUNTER(override_dir_exists)                                                                     \
  COUNTER(load_success)                                                                            \
  GAUGE  (num_keys)
// clang-format on

/**
 * Struct definition for all runtime stats. @see stats_macros.h
 */
struct RuntimeStats {
  ALL_RUNTIME_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Implementation of Snapshot that reads from disk.
 */
class SnapshotImpl : public Snapshot,
                     public ThreadLocal::ThreadLocalObject,
                     Logger::Loggable<Logger::Id::runtime> {
public:
  SnapshotImpl(const std::string& root_path, const std::string& override_path, RuntimeStats& stats,
               RandomGenerator& generator);

  // Runtime::Snapshot
  bool featureEnabled(const std::string& key, uint64_t default_value, uint64_t random_value,
                      uint16_t num_buckets) const override {
    return random_value % static_cast<uint64_t>(num_buckets) <
           std::min(getInteger(key, default_value), static_cast<uint64_t>(num_buckets));
  }

  bool featureEnabled(const std::string& key, uint64_t default_value) const override {
    // Avoid PNRG if we know we don't need it.
    uint64_t cutoff = std::min(getInteger(key, default_value), 100UL);
    if (cutoff == 0) {
      return false;
    } else if (cutoff == 100) {
      return true;
    } else {
      return generator_.random() % 100 < cutoff;
    }
  }

  bool featureEnabled(const std::string& key, uint64_t default_value,
                      uint64_t random_value) const override {
    return featureEnabled(key, default_value, random_value, 100);
  }

  const std::string& get(const std::string& key) const override;
  uint64_t getInteger(const std::string&, uint64_t default_value) const override;

  // ThreadLocal::ThreadLocalObject
  void shutdown() override {}

private:
  struct Directory {
    Directory(const std::string& path) {
      dir_ = opendir(path.c_str());
      if (!dir_) {
        throw EnvoyException(fmt::format("unable to open directory: {}", path));
      }
    }

    ~Directory() { closedir(dir_); }

    DIR* dir_;
  };

  struct Entry {
    std::string string_value_;
    Optional<uint64_t> uint_value_;
  };

  void walkDirectory(const std::string& path, const std::string& prefix);

  std::unordered_map<std::string, Entry> values_;
  RandomGenerator& generator_;
};

/**
 * Implementation of Loader that watches a symlink for swapping and loads a specified subdirectory
 * from disk. A single snapshot is shared among all threads and referenced by shared_ptr such that
 * a new runtime can be swapped in by the main thread while workers are still using the previous
 * version.
 */
class LoaderImpl : public Loader {
public:
  LoaderImpl(Event::Dispatcher& dispatcher, ThreadLocal::Instance& tls,
             const std::string& root_symlink_path, const std::string& subdir,
             const std::string& override_dir, Stats::Store& store, RandomGenerator& generator);

  // Runtime::Loader
  Snapshot& snapshot() override;

private:
  RuntimeStats generateStats(Stats::Store& store);
  void onSymlinkSwap();

  Filesystem::WatcherPtr watcher_;
  ThreadLocal::Instance& tls_;
  uint32_t tls_slot_;
  RandomGenerator& generator_;
  std::string root_path_;
  std::string override_path_;
  std::shared_ptr<SnapshotImpl> current_snapshot_;
  RuntimeStats stats_;
};

/**
 * Null implementation of runtime if another runtime source is not configured.
 */
class NullLoaderImpl : public Loader {
public:
  NullLoaderImpl(RandomGenerator& generator) : snapshot_(generator) {}

  // Runtime::Loader
  Snapshot& snapshot() override { return snapshot_; }

private:
  struct NullSnapshotImpl : public Snapshot {
    NullSnapshotImpl(RandomGenerator& generator) : generator_(generator) {}

    // Runtime::Snapshot
    bool featureEnabled(const std::string&, uint64_t default_value, uint64_t random_value,
                        uint16_t num_buckets) const override {
      return random_value % static_cast<uint64_t>(num_buckets) <
             std::min(default_value, static_cast<uint64_t>(num_buckets));
    }

    bool featureEnabled(const std::string& key, uint64_t default_value) const override {
      if (default_value == 0) {
        return false;
      } else if (default_value == 100) {
        return true;
      } else {
        return featureEnabled(key, default_value, generator_.random());
      }
    }

    bool featureEnabled(const std::string& key, uint64_t default_value,
                        uint64_t random_value) const override {
      return featureEnabled(key, default_value, random_value, 100);
    }

    const std::string& get(const std::string&) const override { return EMPTY_STRING; }

    uint64_t getInteger(const std::string&, uint64_t default_value) const override {
      return default_value;
    }

    RandomGenerator& generator_;
  };

  NullSnapshotImpl snapshot_;
};

} // Runtime

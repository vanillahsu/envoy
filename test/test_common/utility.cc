#include "utility.h"

#include "envoy/buffer/buffer.h"

#include "common/common/empty_string.h"
#include "common/network/address_impl.h"

bool TestUtility::buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs) {
  if (lhs.length() != rhs.length()) {
    return false;
  }

  uint64_t lhs_num_slices = lhs.getRawSlices(nullptr, 0);
  uint64_t rhs_num_slices = rhs.getRawSlices(nullptr, 0);
  if (lhs_num_slices != rhs_num_slices) {
    return false;
  }

  Buffer::RawSlice lhs_slices[lhs_num_slices];
  lhs.getRawSlices(lhs_slices, lhs_num_slices);
  Buffer::RawSlice rhs_slices[rhs_num_slices];
  rhs.getRawSlices(rhs_slices, rhs_num_slices);
  for (size_t i = 0; i < lhs_num_slices; i++) {
    if (lhs_slices[i].len_ != rhs_slices[i].len_) {
      return false;
    }

    if (0 != memcmp(lhs_slices[i].mem_, rhs_slices[i].mem_, lhs_slices[i].len_)) {
      return false;
    }
  }

  return true;
}

std::string TestUtility::bufferToString(const Buffer::Instance& buffer) {
  std::string output;
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  buffer.getRawSlices(slices, num_slices);
  for (Buffer::RawSlice& slice : slices) {
    output.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return output;
}

std::list<Network::Address::InstanceConstSharedPtr>
TestUtility::makeDnsResponse(const std::list<std::string>& addresses) {
  std::list<Network::Address::InstanceConstSharedPtr> ret;
  for (auto address : addresses) {
    ret.emplace_back(new Network::Address::Ipv4Instance(address));
  }
  return ret;
}

void ConditionalInitializer::setReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  EXPECT_FALSE(ready_);
  ready_ = true;
  cv_.notify_all();
}

void ConditionalInitializer::waitReady() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (ready_) {
    return;
  }

  cv_.wait(lock);
  EXPECT_TRUE(ready_);
}

ScopedFdCloser::ScopedFdCloser(int fd) : fd_(fd) {}
ScopedFdCloser::~ScopedFdCloser() { ::close(fd_); }

namespace Http {

TestHeaderMapImpl::TestHeaderMapImpl() : HeaderMapImpl() {}

TestHeaderMapImpl::TestHeaderMapImpl(
    const std::initializer_list<std::pair<std::string, std::string>>& values)
    : HeaderMapImpl() {
  for (auto& value : values) {
    addViaCopy(value.first, value.second);
  }
}

TestHeaderMapImpl::TestHeaderMapImpl(const HeaderMap& rhs) : HeaderMapImpl(rhs) {}

void TestHeaderMapImpl::addViaCopy(const std::string& key, const std::string& value) {
  addViaCopy(LowerCaseString(key), value);
}

void TestHeaderMapImpl::addViaCopy(const LowerCaseString& key, const std::string& value) {
  HeaderString key_string;
  key_string.setCopy(key.get().c_str(), key.get().size());
  HeaderString value_string;
  value_string.setCopy(value.c_str(), value.size());
  addViaMove(std::move(key_string), std::move(value_string));
}

std::string TestHeaderMapImpl::get_(const std::string& key) { return get_(LowerCaseString(key)); }

std::string TestHeaderMapImpl::get_(const LowerCaseString& key) {
  const HeaderEntry* header = get(key);
  if (!header) {
    return EMPTY_STRING;
  } else {
    return header->value().c_str();
  }
}

bool TestHeaderMapImpl::has(const std::string& key) { return get(LowerCaseString(key)) != nullptr; }

bool TestHeaderMapImpl::has(const LowerCaseString& key) { return get(key) != nullptr; }

} // Http

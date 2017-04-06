#pragma once

#include "envoy/common/pure.h"

namespace Buffer {

/**
 * A raw memory data slice including location and length.
 */
struct RawSlice {
  void* mem_;
  uint64_t len_;
};

/**
 * A basic buffer abstraction.
 */
class Instance {
public:
  virtual ~Instance() {}

  /**
   * Copy data into the buffer.
   * @param data supplies the data address.
   * @param size supplies the data size.
   */
  virtual void add(const void* data, uint64_t size) PURE;

  /**
   * Copy a string into the buffer.
   * @param data supplies the string to copy.
   */
  virtual void add(const std::string& data) PURE;

  /**
   * Copy another buffer into this buffer.
   * @param data supplies the buffer to copy.
   */
  virtual void add(const Instance& data) PURE;

  /**
   * Commit a set of slices originally obtained from reserve(). The number of slices can be
   * different from the number obtained from reserve(). The size of each slice can also be altered.
   * @param iovecs supplies the array of slices to commit.
   * @param num_iovecs supplies the size of the slices array.
   */
  virtual void commit(RawSlice* iovecs, uint64_t num_iovecs) PURE;

  /**
   * Drain data from the buffer.
   * @param size supplies the length of data to drain.
   */
  virtual void drain(uint64_t size) PURE;

  /**
   * Fetch the raw buffer slices. This routine is optimized for performance.
   * @param out supplies an array of RawSlice objects to fill.
   * @param out_size supplies the size of out.
   * @return the actual number of slices needed, which may be greater than out_size. Passing
   *         nullptr for out and 0 for out_size will just return the size of the array needed
   *         to capture all of the slice data.
   * TODO(mattklein123): WARNING: The underlying implementation of this function currently uses
   * libevent's evbuffer. It has the infuriating property where calling getRawSlices(nullptr, 0)
   * will return the slices that include all of the buffer data, but not any empty slices at the
   * end. However, calling getRawSlices(iovec, SOME_CONST), WILL return potentially empty slices
   * beyond the end of the buffer. Code that is trying to avoid stack overflow by limiting the
   * number of returned slices needs to deal with this. When we get rid of evbuffer we can rework
   * all of this.
   */
  virtual uint64_t getRawSlices(RawSlice* out, uint64_t out_size) const PURE;

  /**
   * @return uint64_t the total length of the buffer (not necessarily contiguous in memory).
   */
  virtual uint64_t length() const PURE;

  /**
   * @return a pointer to the first byte of data that has been linearized out to size bytes.
   */
  virtual void* linearize(uint32_t size) PURE;

  /**
   * Move a buffer into this buffer. As little copying is done as possible.
   * @param rhs supplies the buffer to move.
   */
  virtual void move(Instance& rhs) PURE;

  /**
   * Move a portion of a buffer into this buffer. As little copying is done as possible.
   * @param rhs supplies the buffer to move.
   * @param length supplies the amount of data to move.
   */
  virtual void move(Instance& rhs, uint64_t length) PURE;

  /**
   * Read from a file descriptor directly into the buffer.
   * @param fd supplies the descriptor to read from.
   * @param max_length supplies the maximum length to read.
   * @return the number of bytes read or -1 if there was an error.
   */
  virtual int read(int fd, uint64_t max_length) PURE;

  /**
   * Reserve space in the buffer.
   * @param length supplies the amount of space to reserve.
   * @param iovecs supplies the slices to fill with reserved memory.
   * @param num_iovecs supplies the size of the slices array.
   * @return the number of iovecs used to reserve the space.
   */
  virtual uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) PURE;

  /**
   * Search for an occurence of a buffer within the larger buffer.
   * @param data supplies the data to search for.
   * @param size supplies the length of the data to search for.
   * @param start supplies the starting index to search from.
   * @return the index where the match starts or -1 if there is no match.
   */
  virtual ssize_t search(const void* data, uint64_t size, size_t start) const PURE;

  /**
   * Write the buffer out to a file descriptor.
   * @param fd supplies the descriptor to write to.
   * @return the number of bytes written or -1 if there was an error.
   */
  virtual int write(int fd) PURE;
};

typedef std::unique_ptr<Instance> InstancePtr;

} // Buffer

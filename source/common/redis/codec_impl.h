#pragma once

#include "envoy/redis/codec.h"

#include "common/common/logger.h"

namespace Redis {

/**
 * Decoder implementation of https://redis.io/topics/protocol
 *
 * This implementation buffers when needed and will always consume all bytes passed for decoding.
 */
class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::redis> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // Redis::Decoder
  void decode(Buffer::Instance& data) override;

private:
  enum class State {
    ValueRootStart,
    ValueStart,
    IntegerStart,
    Integer,
    IntegerLF,
    BulkStringBody,
    CR,
    LF,
    SimpleString,
    ValueComplete
  };

  struct PendingInteger {
    void reset() {
      integer_ = 0;
      negative_ = false;
    }

    int64_t integer_;
    bool negative_;
  };

  struct PendingValue {
    RespValue* value_;
    uint64_t current_array_element_;
  };

  void parseSlice(const Buffer::RawSlice& slice);

  DecoderCallbacks& callbacks_;
  State state_{State::ValueRootStart};
  PendingInteger pending_integer_;
  RespValuePtr pending_value_root_;
  std::forward_list<PendingValue> pending_value_stack_;
};

/**
 * A factory implementation that returns a real decoder.
 */
class DecoderFactoryImpl : public DecoderFactory {
public:
  // Redis::DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    return DecoderPtr{new DecoderImpl(callbacks)};
  }
};

/**
 * Encoder implementation of https://redis.io/topics/protocol
 */
class EncoderImpl : public Encoder {
public:
  // Redis::Encoder
  void encode(const RespValue& value, Buffer::Instance& out) override;

private:
  void encodeArray(const std::vector<RespValue>& array, Buffer::Instance& out);
  void encodeBulkString(const std::string& string, Buffer::Instance& out);
  void encodeError(const std::string& string, Buffer::Instance& out);
  void encodeInteger(int64_t integer, Buffer::Instance& out);
  void encodeSimpleString(const std::string& string, Buffer::Instance& out);
};

} // Redis

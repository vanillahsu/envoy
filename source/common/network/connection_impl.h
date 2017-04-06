#pragma once

#include "envoy/network/connection.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"
#include "common/network/filter_manager_impl.h"

namespace Network {

/**
 * Utility functions for the connection implementation.
 */
class ConnectionImplUtility {
public:
  /**
   * Update the buffer stats for a connection.
   * @param delta supplies the data read/written.
   * @param new_total supplies the final total buffer size.
   * @param previous_total supplies the previous final total buffer size. previous_total will be
   *        updated to new_total when the call is complete.
   * @param stat_total supplies the counter to increment with the delta.
   * @param stat_current supplies the guage that should be updated with the delta of previous_total
   *        and new_total.
   */
  static void updateBufferStats(uint64_t delta, uint64_t new_total, uint64_t& previous_total,
                                Stats::Counter& stat_total, Stats::Gauge& stat_current);
};

/**
 * Implementation of Network::Connection.
 */
class ConnectionImpl : public virtual Connection,
                       public BufferSource,
                       protected Logger::Loggable<Logger::Id::connection> {
public:
  ConnectionImpl(Event::DispatcherImpl& dispatcher, int fd,
                 Address::InstanceConstSharedPtr remote_address,
                 Address::InstanceConstSharedPtr local_address);

  ~ConnectionImpl();

  // Network::FilterManager
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  void close(ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override;
  uint64_t id() override;
  std::string nextProtocol() override { return ""; }
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  bool readEnabled() override;
  const Address::Instance& remoteAddress() override { return *remote_address_; }
  const Address::Instance& localAddress() override { return *local_address_; }
  void setBufferStats(const BufferStats& stats) override;
  Ssl::Connection* ssl() override { return nullptr; }
  State state() override;
  void write(Buffer::Instance& data) override;
  void setReadBufferLimit(uint32_t limit) override { read_buffer_limit_ = limit; }
  uint32_t readBufferLimit() const override { return read_buffer_limit_; }

  // Network::BufferSource
  Buffer::Instance& getReadBuffer() override { return read_buffer_; }
  Buffer::Instance& getWriteBuffer() override { return *current_write_buffer_; }

protected:
  enum class PostIoAction { Close, KeepOpen };

  struct IoResult {
    PostIoAction action_;
    uint64_t bytes_processed_;
  };

  virtual void closeSocket(uint32_t close_type);
  void doConnect();
  void raiseEvents(uint32_t events);
  // Should the read buffer be drained?
  bool shouldDrainReadBuffer() {
    return read_buffer_limit_ > 0 && read_buffer_.length() >= read_buffer_limit_;
  }
  // Mark read buffer ready to read in the event loop. This is used when yielding following
  // shouldDrainReadBuffer().
  // TODO(htuch): While this is the basis for also yielding to other connections to provide some
  // fair sharing of CPU resources, the underlying event loop does not make any fairness guarantees.
  // Reconsider how to make fairness happen.
  void setReadBufferReady() { file_event_->activate(Event::FileReadyType::Read); }

  static const Address::InstanceConstSharedPtr null_local_address_;

  FilterManagerImpl filter_manager_;
  Address::InstanceConstSharedPtr remote_address_;
  Address::InstanceConstSharedPtr local_address_;
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
  uint32_t read_buffer_limit_ = 0;

private:
  // clang-format off
  struct InternalState {
    static const uint32_t ReadEnabled              = 0x1;
    static const uint32_t Connecting               = 0x2;
    static const uint32_t CloseWithFlush           = 0x4;
    static const uint32_t ImmediateConnectionError = 0x8;
  };
  // clang-format on

  virtual IoResult doReadFromSocket();
  virtual IoResult doWriteToSocket();
  virtual void onConnected();
  void onFileEvent(uint32_t events);
  void onRead(uint64_t read_buffer_size);
  void onReadReady();
  void onWriteReady();
  void updateReadBufferStats(uint64_t num_read, uint64_t new_size);
  void updateWriteBufferStats(uint64_t num_written, uint64_t new_size);

  static std::atomic<uint64_t> next_global_id_;

  Event::DispatcherImpl& dispatcher_;
  int fd_{-1};
  Event::FileEventPtr file_event_;
  const uint64_t id_;
  std::list<ConnectionCallbacks*> callbacks_;
  uint32_t state_{InternalState::ReadEnabled};
  Buffer::Instance* current_write_buffer_{};
  uint64_t last_read_buffer_size_{};
  uint64_t last_write_buffer_size_{};
  std::unique_ptr<BufferStats> buffer_stats_;
};

/**
 * libevent implementation of Network::ClientConnection.
 */
class ClientConnectionImpl : public ConnectionImpl, virtual public ClientConnection {
public:
  ClientConnectionImpl(Event::DispatcherImpl& dispatcher, Address::InstanceConstSharedPtr address);

  // Network::ClientConnection
  void connect() override { doConnect(); }
};

} // Network

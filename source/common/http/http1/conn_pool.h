#pragma once

#include "envoy/common/optional.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/event/timer.h"
#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"

#include "common/common/linked_object.h"
#include "common/http/codec_client.h"
#include "common/http/codec_wrappers.h"

namespace Http {
namespace Http1 {

/**
 * A connection pool implementation for HTTP/1.1 connections.
 * NOTE: The connection pool does NOT do DNS resolution. It assumes it is being given a numeric IP
 *       address. Higher layer code should handle resolving DNS on error and creating a new pool
 *       bound to a different IP address.
 */
class ConnPoolImpl : Logger::Loggable<Logger::Id::pool>, public ConnectionPool::Instance {
public:
  ConnPoolImpl(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority)
      : dispatcher_(dispatcher), host_(host), priority_(priority) {}

  ~ConnPoolImpl();

  // ConnectionPool::Instance
  void addDrainedCallback(DrainedCb cb) override;
  ConnectionPool::Cancellable* newStream(StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;

protected:
  struct ActiveClient;

  struct StreamWrapper : public StreamEncoderWrapper,
                         public StreamDecoderWrapper,
                         public StreamCallbacks {
    StreamWrapper(StreamDecoder& response_decoder, ActiveClient& parent);
    ~StreamWrapper();

    // StreamEncoderWrapper
    void onEncodeComplete() override;

    // StreamDecoderWrapper
    void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override;
    void onPreDecodeComplete() override {}
    void onDecodeComplete() override;

    // Http::StreamCallbacks
    void onResetStream(StreamResetReason) override { parent_.parent_.onDownstreamReset(parent_); }

    ActiveClient& parent_;
    bool encode_complete_{};
    bool saw_close_header_{};
    bool decode_complete_{};
  };

  typedef std::unique_ptr<StreamWrapper> StreamWrapperPtr;

  struct ActiveClient : LinkedObject<ActiveClient>,
                        public Network::ConnectionCallbacks,
                        public Event::DeferredDeletable {
    ActiveClient(ConnPoolImpl& parent);
    ~ActiveClient();

    void onConnectTimeout();

    // Network::ConnectionCallbacks
    void onEvent(uint32_t events) override { parent_.onConnectionEvent(*this, events); }

    ConnPoolImpl& parent_;
    CodecClientPtr codec_client_;
    Upstream::HostDescriptionConstSharedPtr real_host_description_;
    StreamWrapperPtr stream_wrapper_;
    Event::TimerPtr connect_timer_;
    Stats::TimespanPtr conn_length_;
    uint64_t remaining_requests_;
  };

  typedef std::unique_ptr<ActiveClient> ActiveClientPtr;

  struct PendingRequest : LinkedObject<PendingRequest>, public ConnectionPool::Cancellable {
    PendingRequest(ConnPoolImpl& parent, StreamDecoder& decoder,
                   ConnectionPool::Callbacks& callbacks);
    ~PendingRequest();

    // Cancellable
    void cancel() override { parent_.onPendingRequestCancel(*this); }

    ConnPoolImpl& parent_;
    StreamDecoder& decoder_;
    ConnectionPool::Callbacks& callbacks_;
  };

  typedef std::unique_ptr<PendingRequest> PendingRequestPtr;

  void attachRequestToClient(ActiveClient& client, StreamDecoder& response_decoder,
                             ConnectionPool::Callbacks& callbacks);
  virtual CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) PURE;
  void checkForDrained();
  void createNewConnection();
  void onConnectionEvent(ActiveClient& client, uint32_t events);
  void onDownstreamReset(ActiveClient& client);
  void onPendingRequestCancel(PendingRequest& request);
  void onResponseComplete(ActiveClient& client);
  void processIdleClient(ActiveClient& client);

  Stats::TimespanPtr conn_connect_ms_;
  Event::Dispatcher& dispatcher_;
  Upstream::HostConstSharedPtr host_;
  std::list<ActiveClientPtr> ready_clients_;
  std::list<ActiveClientPtr> busy_clients_;
  std::list<PendingRequestPtr> pending_requests_;
  std::list<DrainedCb> drained_callbacks_;
  Upstream::ResourcePriority priority_;
};

/**
 * Production implementation of the ConnPoolImpl.
 */
class ConnPoolImplProd : public ConnPoolImpl {
public:
  ConnPoolImplProd(Event::Dispatcher& dispatcher, Upstream::HostConstSharedPtr host,
                   Upstream::ResourcePriority priority)
      : ConnPoolImpl(dispatcher, host, priority) {}

  // ConnPoolImpl
  CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& data) override;
};

} // Http1
} // Http

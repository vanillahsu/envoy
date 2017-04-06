#pragma once

#include "envoy/http/codec.h"
#include "envoy/network/connection.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/http/codec_helper.h"
#include "common/http/header_map_impl.h"

#include "http_parser.h"

namespace Http {
namespace Http1 {

class ConnectionImpl;

/**
 * Base class for HTTP/1.1 request and response encoders.
 */
class StreamEncoderImpl : public StreamEncoder,
                          public Stream,
                          Logger::Loggable<Logger::Id::http>,
                          public StreamCallbackHelper {
public:
  // Http::StreamEncoder
  void encodeHeaders(const HeaderMap& headers, bool end_stream) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeTrailers(const HeaderMap& trailers) override;
  Stream& getStream() override { return *this; }

  // Http::Stream
  void addCallbacks(StreamCallbacks& callbacks) override { addCallbacks_(callbacks); }
  void removeCallbacks(StreamCallbacks& callbacks) override { removeCallbacks_(callbacks); }
  void resetStream(StreamResetReason reason) override;

protected:
  StreamEncoderImpl(ConnectionImpl& connection) : connection_(connection) {}

  static const std::string CRLF;
  static const std::string LAST_CHUNK;

  ConnectionImpl& connection_;

private:
  /**
   * Called to encode an individual header.
   * @param key supplies the header to encode.
   * @param key_size supplies the byte size of the key.
   * @param value supplies the value to encode.
   * @param value_size supplies the byte size of the value.
   */
  void encodeHeader(const char* key, uint32_t key_size, const char* value, uint32_t value_size);

  /**
   * Called to finalize a stream encode.
   */
  void endEncode();

  bool chunk_encoding_{true};
};

/**
 * HTTP/1.1 response encoder.
 */
class ResponseStreamEncoderImpl : public StreamEncoderImpl {
public:
  ResponseStreamEncoderImpl(ConnectionImpl& connection) : StreamEncoderImpl(connection) {}

  bool startedResponse() { return started_response_; }

  // Http::StreamEncoder
  void encodeHeaders(const HeaderMap& headers, bool end_stream) override;

private:
  bool started_response_{};
};

/**
 * HTTP/1.1 request encoder.
 */
class RequestStreamEncoderImpl : public StreamEncoderImpl {
public:
  RequestStreamEncoderImpl(ConnectionImpl& connection) : StreamEncoderImpl(connection) {}

  bool headRequest() { return head_request_; }

  // Http::StreamEncoder
  void encodeHeaders(const HeaderMap& headers, bool end_stream) override;

private:
  bool head_request_{};
};

/**
 * Base class for HTTP/1.1 client and server connections.
 */
class ConnectionImpl : public virtual Connection, protected Logger::Loggable<Logger::Id::http> {
public:
  /**
   * @return Network::Connection& the backing network connection.
   */
  Network::Connection& connection() { return connection_; }

  /**
   * Called when the active encoder has completed encoding the outbound half of the stream.
   */
  virtual void onEncodeComplete() PURE;

  /**
   * Called when resetStream() has been called on an active stream. In HTTP/1.1 the only
   * valid operation after this point is for the connection to get blown away, but we will not
   * fire any more callbacks in case some stack has to unwind.
   */
  void onResetStreamBase(StreamResetReason reason);

  /**
  * Flush all pending output from encoding.
  */
  void flushOutput();

  void addCharToBuffer(char c);
  void addIntToBuffer(uint64_t i);
  Buffer::OwnedImpl& buffer() { return output_buffer_; }
  uint64_t bufferRemainingSize();
  void copyToBuffer(const char* data, uint64_t length);
  void reserveBuffer(uint64_t size);

  // Http::Connection
  void dispatch(Buffer::Instance& data) override;
  void goAway() override {} // Called during connection manager drain flow
  Protocol protocol() override { return protocol_; }
  void shutdownNotice() override {} // Called during connection manager drain flow
  bool wantsToWrite() override { return false; }

protected:
  ConnectionImpl(Network::Connection& connection, http_parser_type type);

  bool resetStreamCalled() { return reset_stream_called_; }

  Network::Connection& connection_;
  http_parser parser_;
  HeaderMapPtr deferred_end_stream_headers_;

private:
  enum class HeaderParsingState { Field, Value, Done };

  struct ToLowerTable {
    ToLowerTable();
    void toLowerCase(HeaderString& text) const;

    std::array<uint8_t, 256> table_;
  };

  /**
   * Called in order to complete an in progress header decode.
   */
  void completeLastHeader();

  /**
   * Dispatch a memory span.
   * @param slice supplies the start address.
   * @len supplies the lenght of the span.
   */
  size_t dispatchSlice(const char* slice, size_t len);

  /**
   * Called when a request/response is beginning. A base routine happens first then a virtual
   * dispatch is invoked.
   */
  void onMessageBeginBase();
  virtual void onMessageBegin() PURE;

  /**
   * Called when URL data is received.
   * @param data supplies the start address.
   * @param lenth supplies the length.
   */
  virtual void onUrl(const char* data, size_t length) PURE;

  /**
   * Called when header field data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  void onHeaderField(const char* data, size_t length);

  /**
   * Called when header value data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  void onHeaderValue(const char* data, size_t length);

  /**
   * Called when headers are complete. A base routine happens first then a virtual disaptch is
   * invoked.
   * @return 0 if no error, 1 if there should be no body.
   */
  int onHeadersCompleteBase();
  virtual int onHeadersComplete(HeaderMapImplPtr&& headers) PURE;

  /**
   * Called when body data is received.
   * @param data supplies the start address.
   * @param length supplies the length.
   */
  virtual void onBody(const char* data, size_t length) PURE;

  /**
   * Called when the request/response is complete.
   */
  virtual void onMessageComplete() PURE;

  /**
   * @see onResetStreamBase().
   */
  virtual void onResetStream(StreamResetReason reason) PURE;

  /**
   * Send a protocol error response to remote.
   */
  virtual void sendProtocolError() PURE;

  static http_parser_settings settings_;
  static const ToLowerTable to_lower_table_;

  HeaderMapImplPtr current_header_map_;
  HeaderParsingState header_parsing_state_{HeaderParsingState::Field};
  HeaderString current_header_field_;
  HeaderString current_header_value_;
  bool reset_stream_called_{};
  Buffer::OwnedImpl output_buffer_;
  Buffer::RawSlice reserved_iovec_;
  char* reserved_current_{};
  Protocol protocol_{Protocol::Http11};
};

/**
 * Implementation of Http::ServerConnection for HTTP/1.1.
 */
class ServerConnectionImpl : public ServerConnection, public ConnectionImpl {
public:
  ServerConnectionImpl(Network::Connection& connection, ServerConnectionCallbacks& callbacks);

private:
  /**
   * An active HTTP/1.1 request.
   */
  struct ActiveRequest {
    ActiveRequest(ConnectionImpl& connection) : response_encoder_(connection) {}

    HeaderString request_url_;
    StreamDecoder* request_decoder_{};
    ResponseStreamEncoderImpl response_encoder_;
    bool remote_complete_{};
  };

  // ConnectionImpl
  void onEncodeComplete() override;
  void onMessageBegin() override;
  void onUrl(const char* data, size_t length) override;
  int onHeadersComplete(HeaderMapImplPtr&& headers) override;
  void onBody(const char* data, size_t length) override;
  void onMessageComplete() override;
  void onResetStream(StreamResetReason reason) override;
  void sendProtocolError() override;

  ServerConnectionCallbacks& callbacks_;
  std::unique_ptr<ActiveRequest> active_request_;
};

/**
 * Implementation of Http::ClientConnection for HTTP/1.1.
 */
class ClientConnectionImpl : public ClientConnection, public ConnectionImpl {
public:
  ClientConnectionImpl(Network::Connection& connection, ConnectionCallbacks& callbacks);

  // Http::ClientConnection
  StreamEncoder& newStream(StreamDecoder& response_decoder) override;

private:
  struct PendingResponse {
    PendingResponse(StreamDecoder* decoder) : decoder_(decoder) {}

    StreamDecoder* decoder_;
    bool head_request_{};
  };

  bool cannotHaveBody();

  // ConnectionImpl
  void onEncodeComplete() override;
  void onMessageBegin() override {}
  void onUrl(const char*, size_t) override { NOT_IMPLEMENTED; }
  int onHeadersComplete(HeaderMapImplPtr&& headers) override;
  void onBody(const char* data, size_t length) override;
  void onMessageComplete() override;
  void onResetStream(StreamResetReason reason) override;
  void sendProtocolError() override {}

  std::unique_ptr<RequestStreamEncoderImpl> request_encoder_;
  std::list<PendingResponse> pending_responses_;
};

} // Http1
} // Http

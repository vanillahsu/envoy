#pragma once

#include "envoy/common/optional.h"
#include "envoy/http/message.h"

namespace Http {

/**
 * Supports sending an HTTP request message and receiving a response asynchronously.
 */
class AsyncClient {
public:
  /**
   * Async Client failure reasons.
   */
  enum class FailureReason {
    // The stream has been reset.
    Reset
  };

  /**
   * Notifies caller of async HTTP request status.
   */
  class Callbacks {
  public:
    virtual ~Callbacks() {}

    /**
     * Called when the async HTTP request succeeds.
     * @param response the HTTP response
     */
    virtual void onSuccess(MessagePtr&& response) PURE;

    /**
     * Called when the async HTTP request fails.
     */
    virtual void onFailure(FailureReason reason) PURE;
  };

  /**
   * Notifies caller of async HTTP stream status.
   * Note the HTTP stream is full-duplex, even if the local to remote stream has been ended
   * by Stream.sendHeaders/sendData with end_stream=true or sendTrailers,
   * StreamCallbacks can continue to receive events until the remote to local stream is closed,
   * and vice versa.
   */
  class StreamCallbacks {
  public:
    virtual ~StreamCallbacks() {}

    /**
     * Called when all headers get received on the async HTTP stream.
     * @param headers the headers received
     * @param end_stream whether the response is header only
     */
    virtual void onHeaders(HeaderMapPtr&& headers, bool end_stream) PURE;

    /**
     * Called when a data frame get received on the async HTTP stream.
     * This can be invoked multiple times if the data get streamed.
     * @param data the data received
     * @param end_stream whether the data is the last data frame
     */
    virtual void onData(Buffer::Instance& data, bool end_stream) PURE;

    /**
     * Called when all trailers get received on the async HTTP stream.
     * @param trailers the trailers received.
     */
    virtual void onTrailers(HeaderMapPtr&& trailers) PURE;

    /**
     * Called when the async HTTP stream is reset.
     */
    virtual void onReset() PURE;
  };

  /**
   * An in-flight HTTP request.
   */
  class Request {
  public:
    virtual ~Request() {}

    /**
     * Signals that the request should be cancelled.
     */
    virtual void cancel() PURE;
  };

  /**
   * An in-flight HTTP stream.
   */
  class Stream {
  public:
    virtual ~Stream() {}

    /***
     * Send headers to the stream. This method cannot be invoked more than once and
     * need to be called before sendData.
     * @param headers supplies the headers to send.
     * @param end_stream supplies whether this is a header only request.
     */
    virtual void sendHeaders(HeaderMap& headers, bool end_stream) PURE;

    /***
     * Send data to the stream. This method can be invoked multiple times if it get streamed.
     * To end the stream without data, call this method with empty buffer.
     * @param data supplies the data to send.
     * @param end_stream supplies whether this is the last data.
     */
    virtual void sendData(Buffer::Instance& data, bool end_stream) PURE;

    /***
     * Send trailers. This method cannot be invoked more than once, and implicitly ends the stream.
     * @param trailers supplies the trailers to send.
     */
    virtual void sendTrailers(HeaderMap& trailers) PURE;

    /***
     * Reset the stream.
     */
    virtual void reset() PURE;
  };

  virtual ~AsyncClient() {}

  /**
   * Send an HTTP request asynchronously
   * @param request the request to send.
   * @param callbacks the callbacks to be notified of request status.
   * @param timeout supplies the request timeout
   * @return a request handle or nullptr if no request could be created. NOTE: In this case
   *         onFailure() has already been called inline. The client owns the request and the
   *         handle should just be used to cancel.
   */
  virtual Request* send(MessagePtr&& request, Callbacks& callbacks,
                        const Optional<std::chrono::milliseconds>& timeout) PURE;

  /**
   * Start an HTTP stream asynchronously.
   * @param callbacks the callbacks to be notified of stream status.
   * @param timeout supplies the stream timeout, measured since when the frame with end_stream
   *        flag is sent until when the first frame is received.
   * @return a stream handle or nullptr if no stream could be started. NOTE: In this case
   *         onResetStream() has already been called inline. The client owns the stream and
   *         the handle can be used to send more messages or close the stream.
   */
  virtual Stream* start(StreamCallbacks& callbacks,
                        const Optional<std::chrono::milliseconds>& timeout) PURE;
};

typedef std::unique_ptr<AsyncClient> AsyncClientPtr;

} // Http

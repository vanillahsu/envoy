#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/upstream/host_description.h"

namespace Network {

class Connection;

/**
 * Status codes returned by filters that can cause future filters to not get iterated to.
 */
enum class FilterStatus {
  // Continue to further filters.
  Continue,
  // Stop executing further filters.
  StopIteration
};

/**
 * A write path binary connection filter.
 */
class WriteFilter {
public:
  virtual ~WriteFilter() {}

  /**
   * Called when data is to be written on the connection.
   * @param data supplies the buffer to be written which may be modified.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onWrite(Buffer::Instance& data) PURE;
};

typedef std::shared_ptr<WriteFilter> WriteFilterSharedPtr;

/**
 * Callbacks used by individual read filter instances to communicate with the filter manager.
 */
class ReadFilterCallbacks {
public:
  virtual ~ReadFilterCallbacks() {}

  /**
   * @return the connection that owns this read filter.
   */
  virtual Connection& connection() PURE;

  /**
   * If a read filter stopped filter iteration, continueReading() can be called to continue the
   * filter chain. The next filter will be called with all currently available data in the read
   * buffer (it will also have onNewConnection() called on it if it was not previously called).
   */
  virtual void continueReading() PURE;

  /**
   * Return the currently selected upstream host, if any. This can be used for communication
   * between multiple network level filters, for example the TCP proxy filter communicating its
   * selection to another filter for logging.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() PURE;

  /**
   * Set the currently selected upstream host for the connection.
   */
  virtual void upstreamHost(Upstream::HostDescriptionConstSharedPtr host) PURE;
};

/**
 * A read path binary connection filter.
 */
class ReadFilter {
public:
  virtual ~ReadFilter() {}

  /**
   * Called when data is read on the connection.
   * @param data supplies the read data which may be modified.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onData(Buffer::Instance& data) PURE;

  /**
   * Called when a connection is first established. Filters should do one time long term processing
   * that needs to be done when a connection is established. Filter chain iteration can be stopped
   * if needed.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onNewConnection() PURE;

  /**
   * Initializes the read filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered. Thus, any
   * construction that requires the backing connection should take place in the context of this
   * function.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of onNewConnection() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  virtual void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) PURE;
};

typedef std::shared_ptr<ReadFilter> ReadFilterSharedPtr;

/**
 * A combination read and write filter. This allows a single filter instance to cover
 * both the read and write paths.
 */
class Filter : public WriteFilter, public ReadFilter {};
typedef std::shared_ptr<Filter> FilterSharedPtr;

/**
 * Interface for adding individual network filters to a manager.
 */
class FilterManager {
public:
  virtual ~FilterManager() {}

  /**
   * Add a write filter to the connection. Filters are invoked in LIFO order (the last added
   * filter is called first).
   */
  virtual void addWriteFilter(WriteFilterSharedPtr filter) PURE;

  /**
   * Add a combination filter to the connection. Equivalent to calling both addWriteFilter()
   * and addReadFilter() with the same filter instance.
   */
  virtual void addFilter(FilterSharedPtr filter) PURE;

  /**
   * Add a read filter to the connection. Filters are invoked in FIFO order (the filter added
   * first is called first).
   */
  virtual void addReadFilter(ReadFilterSharedPtr filter) PURE;

  /**
   * Initialize all of the installed read filters. This effectively calls onNewConnection() on
   * each of them.
   * @return true if read filters were initialized successfully, otherwise false.
   */
  virtual bool initializeReadFilters() PURE;
};

/**
 * Creates a chain of network filters for a new connection.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() {}

  /**
   * Called to create the filter chain.
   * @param connection supplies the connection to create the chain on.
   * @return true if filter chain was created successfully. Otherwise
   *   false, e.g. filter chain is empty.
   */
  virtual bool createFilterChain(Connection& connection) PURE;
};

} // Network

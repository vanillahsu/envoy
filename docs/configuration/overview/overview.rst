.. _config_overview:

Overview
========

The Envoy configuration format is written in JSON and is validated against a JSON schema. The
schema can be found in :repo:`source/common/json/config_schemas.cc`. The main configuration for the
server is contained within the listeners and cluster manager sections. The other top level elements
specify miscellaneous configuration.

.. code-block:: json

  {
    "listeners": [],
    "admin": "{...}",
    "cluster_manager": "{...}",
    "flags_path": "...",
    "statsd_local_udp_port": "...",
    "statsd_tcp_cluster_name": "...",
    "stats_flush_interval_ms": "...",
    "watchdog_miss_timeout_ms": "...",
    "watchdog_megamiss_timeout_ms": "...",
    "watchdog_kill_timeout_ms": "...",
    "watchdog_multikill_timeout_ms": "...",
    "tracing": "{...}",
    "rate_limit_service": "{...}",
    "runtime": "{...}",
  }

:ref:`listeners <config_listeners>`
  *(required, array)* An array of :ref:`listeners <arch_overview_listeners>` that will be
  instantiated by the server. A single Envoy process can contain any number of listeners.

:ref:`admin <config_admin>`
  *(required, object)* Configuration for the :ref:`local administration HTTP server
  <operations_admin_interface>`.

:ref:`cluster_manager <config_cluster_manager>`
  *(required, object)* Configuration for the :ref:`cluster manager <arch_overview_cluster_manager>`
  which owns all upstream clusters within the server.

.. _config_overview_flags_path:

flags_path
  *(optional, string)* The file system path to search for :ref:`startup flag files
  <operations_file_system_flags>`.

statsd_local_udp_port
  *(optional, integer)* The UDP port of a locally running statsd compliant listener. If specified,
  :ref:`statistics <arch_overview_statistics>` will be flushed to this port.

statsd_tcp_cluster_name
  *(optional, string)* The name of a cluster manager cluster that is running a TCP statsd compliant
  listener. If specified, Envoy will connect to this cluster to flush :ref:`statistics
  <arch_overview_statistics>`.

.. _config_overview_stats_flush_interval_ms:

stats_flush_interval_ms
  *(optional, integer)* The time in milliseconds between flushes to configured stats sinks. For
  performance reasons Envoy latches counters and only flushes counters and gauges at a periodic
  interval. If not specified the default is 5000ms (5 seconds).

watchdog_miss_timeout_ms
  *(optional, integer)* The time in milliseconds after which Envoy counts a nonresponsive thread in the
  "server.watchdog_miss" statistic. If not specified the default is 200ms.

watchdog_megamiss_timeout_ms
  *(optional, integer)* The time in milliseconds after which Envoy counts a nonresponsive thread in the
  "server.watchdog_mega_miss" statistic. If not specified the default is 1000ms.

watchdog_kill_timeout_ms
  *(optional, integer)* If a watched thread has been nonresponsive for this many milliseconds assume
  a programming error and kill the entire Envoy process. Set to 0 to disable kill behavior. If not
  specified the default is 0 (disabled).

watchdog_multikill_timeout_ms
  *(optional, integer)* If at least two watched threads have been nonresponsive for at least this many
  milliseconds assume a true deadlock and kill the entire Envoy process. Set to 0 to disable this
  behavior. If not specified the default is 0 (disabled).

:ref:`tracing <config_tracing>`
  *(optional, object)* Configuration for an external :ref:`tracing <arch_overview_tracing>`
  provider. If not specified, no tracing will be performed.

:ref:`rate_limit_service <config_rate_limit_service>`
  *(optional, object)* Configuration for an external :ref:`rate limit service
  <arch_overview_rate_limit>` provider. If not specified, any calls to the rate limit service will
  immediately return success.

:ref:`runtime <config_runtime>`
  *(optional, object)* Configuration for the :ref:`runtime configuration <arch_overview_runtime>`
  provider. If not specified, a "null" provider will be used which will result in all defaults being
  used.

.. toctree::
  :hidden:

  admin
  tracing
  rate_limit
  runtime

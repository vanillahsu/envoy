.. _config_cluster_manager_cluster:

Cluster
=======

.. code-block:: json

  {
    "name": "...",
    "type": "...",
    "connect_timeout_ms": "...",
    "per_connection_buffer_limit_bytes": "...",
    "lb_type": "...",
    "hosts": [],
    "service_name": "...",
    "health_check": "{...}",
    "max_requests_per_connection": "...",
    "circuit_breakers": "{...}",
    "ssl_context": "{...}",
    "features": "...",
    "http_codec_options": "...",
    "dns_refresh_rate_ms": "...",
    "outlier_detection": "..."
  }

.. _config_cluster_manager_cluster_name:

name
  *(required, string)* Supplies the name of the cluster which must be unique across all clusters.
  The cluster name is used when emitting :ref:`statistics <config_cluster_manager_cluster_stats>`.
  The cluster name can be at most 60 characters long, and must **not** contain ``:``.

.. _config_cluster_manager_type:

type
  *(required, string)* The :ref:`service discovery type <arch_overview_service_discovery_types>` to
  use for resolving the cluster. Possible options are *static*, *strict_dns*, *logical_dns*, and
  *sds*.

connect_timeout_ms
  *(required, integer)* The timeout for new network connections to hosts in the cluster specified
  in milliseconds.

per_connection_buffer_limit_bytes
  *(optional, integer)* Soft limit on size of the cluster's connections read and write buffers.
  If unspecified, an implementation defined default is applied (1MiB).

lb_type
  *(required, string)* The :ref:`load balancer type <arch_overview_load_balancing_types>` to use
  when picking a host in the cluster. Possible options are *round_robin*, *least_request*,
  *ring_hash*, and *random*.

hosts
  *(sometimes required, array)* If the service discovery type is *static*, *strict_dns*, or
  *logical_dns* the hosts array is required. How it is specified depends on the type of service
  discovery:

  static
    Static clusters must use fully resolved hosts that require no DNS lookups. Both TCP and unix
    domain sockets (UDS) addresses are supported. A TCP address looks like:

    ``tcp://<ip>:<port>``

    A UDS address looks like:

    ``unix://<file name>``

    A list of addresses can be specified as in the following example:

    .. code-block:: json

      [{"url": "tcp://10.0.0.2:1234"}, {"url": "tcp://10.0.0.3:5678"}]

  strict_dns
    Strict DNS clusters can specify any number of hostname:port combinations. All names will be
    resolved using DNS and grouped together to form the final cluster. If multiple records are
    returned for a single name, all will be used. For example:

    .. code-block:: json

      [{"url": "tcp://foo1.bar.com:1234"}, {"url": "tcp://foo2.bar.com:5678"}]

  logical_dns
    Logical DNS clusters specify hostnames much like strict DNS, however only the first host will be
    used. For example:

    .. code-block:: json

      [{"url": "tcp://foo1.bar.com:1234"}]

.. _config_cluster_manager_cluster_service_name:

service_name
  *(sometimes required, string)* This parameter is required if the service discovery type is *sds*.
  It will be passed to the :ref:`SDS API <config_cluster_manager_sds_api>` when fetching cluster
  members.

:ref:`health_check <config_cluster_manager_cluster_hc>`
  *(optional, object)* Optional :ref:`active health checking <arch_overview_health_checking>`
  configuration for the cluster. If no configuration is specified no health checking will be done
  and all cluster members will be considered healthy at all times.

max_requests_per_connection
  *(optional, integer)* Optional maximum requests for a single upstream connection. This
  parameter is respected by both the HTTP/1.1 and HTTP/2 connection pool implementations. If not
  specified, there is no limit. Setting this parameter to 1 will effectively disable keep alive.

:ref:`circuit_breakers <config_cluster_manager_cluster_circuit_breakers>`
  *(optional, object)* Optional :ref:`circuit breaking <arch_overview_circuit_break>` settings
  for the cluster.

:ref:`ssl_context <config_cluster_manager_cluster_ssl>`
  *(optional, object)* The TLS configuration for connections to the upstream cluster. If no TLS
  configuration is specified, TLS will not be used for new connections.

.. _config_cluster_manager_cluster_features:

features
  *(optional, string)* A comma delimited list of features that the upstream cluster supports.
  The currently supported features are:

  http2
    If *http2* is specified, Envoy will assume that the upstream supports HTTP/2 when making new
    HTTP connection pool connections. Currently, Envoy only supports prior knowledge for upstream
    connections. Even if TLS is used with ALPN, *http2* must be specified. As an aside this allows
    HTTP/2 connections to happen over plain text.

.. _config_cluster_manager_cluster_http_codec_options:

http_codec_options
  *(optional, string)* Additional options that are passed directly to the codec when initiating
  HTTP connection pool connections. These are the same options supported in the HTTP connection
  manager :ref:`http_codec_options <config_http_conn_man_http_codec_options>` option. When building
  an HTTP/2 mesh, if it's desired to disable HTTP/2 header compression the *no_compression*
  option should be specified both here as well as in the HTTP connection manager.

.. _config_cluster_manager_cluster_dns_refresh_rate_ms:

dns_refresh_rate_ms
  *(optional, integer)* If the dns refresh rate is specified and the cluster type is either *strict_dns*,
  or *logical_dns*, this value is used as the cluster's dns refresh rate. If this setting is not specified,
  the value defaults to 5000. For cluster types other than *strict_dns* and *logical_dns* this setting is
  ignored.

.. _config_cluster_manager_cluster_outlier_detection:

outlier_detection
  *(optional, object)* If specified, outlier detection will be enabled for this upstream cluster.
  See the :ref:`architecture overview <arch_overview_outlier_detection>` for more information on outlier
  detection. The following configuration values are supported:

  .. _config_cluster_manager_cluster_outlier_detection_consecutive_5xx:

  consecutive_5xx
    The number of consecutive 5xx responses before a consecutive 5xx ejection occurs. Defaults to 5.

  .. _config_cluster_manager_cluster_outlier_detection_interval_ms:

  interval_ms
    The time interval between ejection analysis sweeps. This can result in both new ejections as well
    as hosts being returned to service. Defaults to 10000ms or 10s.

  .. _config_cluster_manager_cluster_outlier_detection_base_ejection_time_ms:

  base_ejection_time_ms
    The base time that a host is ejected for. The real time is equal to the base time multiplied by
    the number of times the host has been ejected. Defaults to 30000ms or 30s.

  .. _config_cluster_manager_cluster_outlier_detection_max_ejection_percent:

  max_ejection_percent
    The maximum % of an upstream cluster that can be ejected due to outlier detection. Defaults to 10%.

  .. _config_cluster_manager_cluster_outlier_detection_enforcing_consecutive_5xx:

  enforcing_consecutive_5xx
    The % chance that a host will be actually ejected when an outlier status is detected through
    consecutive 5xx. This setting can be used to disable ejection or to ramp it up slowly. Defaults to 100.

  .. _config_cluster_manager_cluster_outlier_detection_enforcing_success_rate:

  enforcing_success_rate
    The % chance that a host will be actually ejected when an outlier status is detected through
    success rate statistics. This setting can be used to disable ejection or to ramp it up slowly.
    Defaults to 100.

  .. _config_cluster_manager_cluster_outlier_detection_success_rate_minimum_hosts:

  success_rate_minimum_hosts
    The number of hosts in a cluster that must have enough request volume to detect success rate outliers.
    If the number of hosts is less than this setting, outlier detection via success rate statistics is not
    performed for any host in the cluster. Defaults to 5.

  .. _config_cluster_manager_cluster_outlier_detection_success_rate_request_volume:

  success_rate_request_volume
    The minimum number of total requests that must be collected in one interval
    (as defined by :ref:`interval_ms <config_cluster_manager_cluster_outlier_detection_interval_ms>` above)
    to include this host in success rate based outlier detection. If the volume is lower than this setting,
    outlier detection via success rate statistics is not performed for that host. Defaults to 100.

  Each of the above configuration values can be overridden via
  :ref:`runtime values <config_cluster_manager_cluster_runtime_outlier_detection>`.

.. toctree::
  :hidden:

  cluster_hc
  cluster_circuit_breakers
  cluster_ssl
  cluster_stats
  cluster_runtime

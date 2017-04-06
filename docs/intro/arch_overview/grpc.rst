.. _arch_overview_grpc:

gRPC
====

`gRPC <http://www.grpc.io/>`_ is an RPC framework from Google. It uses protocol buffers as the
underlying serialization/IDL format. At the transport layer it uses HTTP/2 for request/response
multiplexing. Envoy has first class support for gRPC both at the transport layer as well as at the
application layer:

* gRPC makes use of HTTP/2 trailers to convey request status. Envoy is one of very few HTTP proxies
  that correctly supports HTTP/2 trailers and is thus one of the few proxies that can transport
  gRPC requests and responses.
* The gRPC runtime for some languages is relatively immature. Envoy supports a gRPC :ref:`bridge
  filter <config_http_filters_grpc_bridge>` that allows gRPC requests to be sent to Envoy over
  HTTP/1.1. Envoy then translates the requests to HTTP/2 for transport to the target server.
  The response is translated back to HTTP/1.1.
* When installed, the bridge filter gathers per RPC statistics in addition to the standard array
  of global HTTP statistics.

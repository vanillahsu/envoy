#pragma once

#include "common/ssl/context_impl.h"
#include "common/ssl/context_manager_impl.h"
#include "common/ssl/openssl.h"

#include "envoy/runtime/runtime.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

namespace Ssl {

// clang-format off
#define ALL_SSL_STATS(COUNTER, GAUGE, TIMER)                                                       \
  COUNTER(connection_error)                                                                        \
  COUNTER(handshake)                                                                               \
  COUNTER(no_certificate)                                                                          \
  COUNTER(fail_verify_san)                                                                         \
  COUNTER(fail_verify_cert_hash)
// clang-format on

/**
 * Wrapper struct for SSL stats. @see stats_macros.h
 */
struct SslStats {
  ALL_SSL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT)
};

typedef CSmartPtr<SSL, SSL_free> SslConPtr;

class ContextImpl : public virtual Context {
public:
  ~ContextImpl() { parent_.releaseContext(this); }

  virtual SslConPtr newSsl() const;

  /**
   * Performs all configured cert verifications on the connection
   * @param ssl the connection to verify
   * @return true if all configured cert verifications succeed
   */
  bool verifyPeer(SSL* ssl) const;

  /**
   * Performs subjectAltName verification
   * @param ssl the certificate to verify
   * @param subject_alt_names the configured subject_alt_names to match
   * @return true if the verification succeeds
   */
  static bool verifySubjectAltName(X509* cert, const std::vector<std::string>& subject_alt_names);

  /**
   * Determines whether the given name matches 'pattern' which may optionally begin with a wildcard.
   * NOTE:  public for testing
   * @param san the subjectAltName to match
   * @param pattern the pattern to match against (*.example.com)
   * @return true if the san matches pattern
   */
  static bool dNSNameMatch(const std::string& dnsName, const char* pattern);

  SslStats& stats() { return stats_; }

  // Ssl::Context
  size_t daysUntilFirstCertExpires() override;
  std::string getCaCertInformation() override;
  std::string getCertChainInformation() override;

protected:
  ContextImpl(ContextManagerImpl& parent, Stats::Scope& scope, ContextConfig& config);

  /**
   * Specifies the context for which the session can be reused.  Any data is acceptable here.
   * @see SSL_CTX_set_session_id_ctx
   */
  static const unsigned char SERVER_SESSION_ID_CONTEXT;

  typedef CSmartPtr<SSL_CTX, SSL_CTX_free> SslCtxPtr;

  /**
   * Verifies certificate hash for pinning. The hash is the SHA-256 has of the DER encoding of the
   * certificate.
   *
   * The hash can be computed using 'openssl x509 -noout -fingerprint -sha256 -in cert.pem'
   *
   * @param ssl the certificate to verify
   * @param certificate_hash the configured certificate hash to match
   * @return true if the verification succeeds
   */
  static bool verifyCertificateHash(X509* cert, const std::vector<uint8_t>& certificate_hash);

  std::vector<uint8_t> parseAlpnProtocols(const std::string& alpn_protocols);
  static SslStats generateStats(Stats::Scope& scope);
  int32_t getDaysUntilExpiration(const X509* cert);
  X509Ptr loadCert(const std::string& cert_file);
  static std::string getSerialNumber(X509* cert);
  std::string getCaFileName() { return ca_file_path_; };
  std::string getCertChainFileName() { return cert_chain_file_path_; };

  ContextManagerImpl& parent_;
  SslCtxPtr ctx_;
  std::vector<std::string> verify_subject_alt_name_list_;
  std::vector<uint8_t> verify_certificate_hash_;
  Stats::Scope& scope_;
  SslStats stats_;
  std::vector<uint8_t> parsed_alpn_protocols_;
  X509Ptr ca_cert_;
  X509Ptr cert_chain_;
  std::string ca_file_path_;
  std::string cert_chain_file_path_;
};

class ClientContextImpl : public ContextImpl, public ClientContext {
public:
  ClientContextImpl(ContextManagerImpl& parent, Stats::Scope& scope, ContextConfig& config);

  SslConPtr newSsl() const override;

private:
  std::string server_name_indication_;
};

class ServerContextImpl : public ContextImpl, public ServerContext {
public:
  ServerContextImpl(ContextManagerImpl& parent, Stats::Scope& scope, ContextConfig& config,
                    Runtime::Loader& runtime);

private:
  int alpnSelectCallback(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                         unsigned int inlen);

  Runtime::Loader& runtime_;
  std::vector<uint8_t> parsed_alt_alpn_protocols_;
};

} // Ssl

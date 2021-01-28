// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "curve.h"
#include "error_string.h"
#include "hash.h"
#include "key_pair.h"
#include "mbedtls/pem.h"
#include "pem.h"

namespace tls
{
  static constexpr size_t max_pem_cert_size = 4096;

  // As these are not exposed by mbedlts, define them here to allow simple
  // conversion from DER to PEM format
  static constexpr auto PEM_CERTIFICATE_HEADER =
    "-----BEGIN CERTIFICATE-----\n";
  static constexpr auto PEM_CERTIFICATE_FOOTER = "-----END CERTIFICATE-----\n";

  class Verifier
  {
  protected:
    mutable mbedtls::X509Crt cert;

  public:
    /**
     * Construct from a pre-parsed cert
     *
     * @param c Initialised and parsed x509 cert
     */
    Verifier(mbedtls::X509Crt&& c) : cert(std::move(c)) {}

    Verifier(const Verifier&) = delete;

    virtual ~Verifier() = default;

    /**
     * Verify that a signature was produced on a hash with the private key
     * associated with the public key contained in the certificate.
     *
     * @param hash First byte in hash sequence
     * @param hash_size Number of bytes in hash sequence
     * @param signature First byte in signature sequence
     * @param signature_size Number of bytes in signature sequence
     * @param md_type Digest algorithm to use. Derived from the
     * public key if MBEDTLS_MD_NONE.
     *
     * @return Whether the signature matches the hash and the key
     */
    virtual bool verify_hash(
      const uint8_t* hash,
      size_t hash_size,
      const uint8_t* signature,
      size_t signature_size,
      mbedtls_md_type_t md_type = {}) const
    {
      if (md_type == MBEDTLS_MD_NONE)
        md_type = get_md_for_ec(get_ec_from_context(cert->pk));

      int rc = mbedtls_pk_verify(
        &cert->pk, md_type, hash, hash_size, signature, signature_size);

      if (rc)
        LOG_DEBUG_FMT("Failed to verify signature: {}", error_string(rc));

      return rc == 0;
    }

    /**
     * Verify that a signature was produced on a hash with the private key
     * associated with the public key contained in the certificate.
     *
     * @param hash Hash produced from contents as a sequence of bytes
     * @param signature Signature as a sequence of bytes
     * @param md_type Digest algorithm to use. Derived from the
     * public key if MBEDTLS_MD_NONE.
     *
     * @return Whether the signature matches the hash and the key
     */
    bool verify_hash(
      const std::vector<uint8_t>& hash,
      const std::vector<uint8_t>& signature,
      mbedtls_md_type_t md_type = {}) const
    {
      return verify_hash(
        hash.data(), hash.size(), signature.data(), signature.size(), md_type);
    }

    bool verify_hash(
      const std::vector<uint8_t>& hash,
      const uint8_t* sig,
      size_t sig_size,
      mbedtls_md_type_t md_type = {}) const
    {
      return verify_hash(hash.data(), hash.size(), sig, sig_size, md_type);
    }

    /**
     * Verify that a signature was produced on contents with the private key
     * associated with the public key contained in the certificate.
     *
     * @param contents Sequence of bytes that was signed
     * @param signature Signature as a sequence of bytes
     * @param md_type Digest algorithm to use. Derived from the
     * public key if MBEDTLS_MD_NONE.
     *
     * @return Whether the signature matches the contents and the key
     */
    bool verify(
      const std::vector<uint8_t>& contents,
      const std::vector<uint8_t>& signature,
      mbedtls_md_type_t md_type = {}) const
    {
      return verify(
        contents.data(),
        contents.size(),
        signature.data(),
        signature.size(),
        md_type);
    }

    bool verify(
      const uint8_t* contents,
      size_t contents_size,
      const uint8_t* sig,
      size_t sig_size,
      mbedtls_md_type_t md_type = {}) const
    {
      HashBytes hash;
      do_hash(cert->pk, contents, contents_size, hash, md_type);

      return verify_hash(hash, sig, sig_size, md_type);
    }

    const mbedtls_x509_crt* raw()
    {
      return cert.get();
    }

    std::vector<uint8_t> der_cert_data()
    {
      return {cert->raw.p, cert->raw.p + cert->raw.len};
    }

    Pem cert_pem()
    {
      unsigned char buf[max_pem_cert_size];
      size_t len;

      auto rc = mbedtls_pem_write_buffer(
        PEM_CERTIFICATE_HEADER,
        PEM_CERTIFICATE_FOOTER,
        cert->raw.p,
        cert->raw.len,
        buf,
        max_pem_cert_size,
        &len);

      if (rc != 0)
      {
        throw std::logic_error(
          "mbedtls_pem_write_buffer failed: " + error_string(rc));
      }

      return Pem(buf, len);
    }
  };

  class Verifier_k1Bitcoin : public Verifier
  {
  protected:
    BCk1ContextPtr bc_ctx = make_bc_context(SECP256K1_CONTEXT_VERIFY);

    secp256k1_pubkey bc_pub;

  public:
    template <typename... Ts>
    Verifier_k1Bitcoin(Ts... ts) : Verifier(std::forward<Ts>(ts)...)
    {
      parse_secp256k_bc(cert->pk, bc_ctx->p, &bc_pub);
    }

    bool verify_hash(
      const uint8_t* hash,
      size_t hash_size,
      const uint8_t* signature,
      size_t signature_size,
      mbedtls_md_type_t = {}) const override
    {
      bool ok = verify_secp256k_bc(
        bc_ctx->p, signature, signature_size, hash, hash_size, &bc_pub);

      return ok;
    }
  };

  using VerifierPtr = std::shared_ptr<Verifier>;
  using VerifierUniquePtr = std::unique_ptr<Verifier>;
  /**
   * Construct Verifier from a certificate in DER or PEM format
   *
   * @param cert Sequence of bytes containing the certificate
   */
  inline VerifierUniquePtr make_unique_verifier(
    const std::vector<uint8_t>& cert,
    bool use_bitcoin_impl = prefer_bitcoin_secp256k1)
  {
    auto x509 = mbedtls::make_unique<mbedtls::X509Crt>();
    int rc = mbedtls_x509_crt_parse(x509.get(), cert.data(), cert.size());
    if (rc)
    {
      throw std::invalid_argument(
        fmt::format("Failed to parse certificate: {}", error_string(rc)));
    }

    if (x509->pk.pk_info == mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))
    {
      const auto curve = get_ec_from_context(x509->pk);

      if (curve == MBEDTLS_ECP_DP_SECP256K1 && use_bitcoin_impl)
      {
        return std::make_unique<Verifier_k1Bitcoin>(std::move(x509));
      }
    }

    return std::make_unique<Verifier>(std::move(x509));
  }

  inline VerifierPtr make_verifier(
    const Pem& cert, bool use_bitcoin_impl = prefer_bitcoin_secp256k1)
  {
    return make_unique_verifier(cert.raw(), use_bitcoin_impl);
  }

  inline tls::Pem cert_der_to_pem(const std::vector<uint8_t>& der_cert_raw)
  {
    return make_verifier(der_cert_raw)->cert_pem();
  }

  inline std::vector<uint8_t> cert_pem_to_der(const std::string& pem_cert_raw)
  {
    return make_verifier(pem_cert_raw)->der_cert_data();
  }
}
#include "network/TlsServerContext.h"

#include "core/Log.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace lectern::net {
namespace {

constexpr std::string_view kCategory = "tls";

void logOpenSslErrors() {
    while (const unsigned long err = ERR_get_error()) {
        char buf[256]{};
        ERR_error_string_n(err, buf, sizeof(buf));
        LEC_WARN(kCategory, "{}", buf);
    }
}

void addSanIp(GENERAL_NAMES* sans, const char* ipv4) {
    in_addr addr{};
    if (::inet_pton(AF_INET, ipv4, &addr) != 1) return;
    ASN1_OCTET_STRING* oct = ASN1_OCTET_STRING_new();
    ASN1_OCTET_STRING_set(oct, reinterpret_cast<const unsigned char*>(&addr), sizeof(addr));
    GENERAL_NAME* gn = GENERAL_NAME_new();
    GENERAL_NAME_set0_value(gn, GEN_IPADD, oct);
    sk_GENERAL_NAME_push(sans, gn);
}

}  // namespace

TlsServerContext::~TlsServerContext() {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

Status TlsServerContext::create(const std::string& lanIpv4) {
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_) {
        logOpenSslErrors();
        return fail(ErrorCode::Internal, "SSL_CTX_new failed");
    }

    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
    SSL_CTX_set_ecdh_auto(ctx_, 1);

    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    if (!pkey) {
        logOpenSslErrors();
        return fail(ErrorCode::Internal, "could not generate RSA key");
    }

    X509* cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        logOpenSslErrors();
        return fail(ErrorCode::Internal, "X509_new failed");
    }

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 14L * 24 * 3600);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("Lectern Camera"), -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_set_pubkey(cert, pkey);

    GENERAL_NAMES* sans = sk_GENERAL_NAME_new_null();
    addSanIp(sans, "127.0.0.1");
    if (!lanIpv4.empty()) addSanIp(sans, lanIpv4.c_str());
    X509_add1_ext_i2d(cert, NID_subject_alt_name, sans, 0, 0);
    sk_GENERAL_NAME_pop_free(sans, GENERAL_NAME_free);

    if (X509_sign(cert, pkey, EVP_sha256()) <= 0) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        logOpenSslErrors();
        return fail(ErrorCode::Internal, "X509_sign failed");
    }

    if (SSL_CTX_use_certificate(ctx_, cert) != 1 || SSL_CTX_use_PrivateKey(ctx_, pkey) != 1 ||
        SSL_CTX_check_private_key(ctx_) != 1) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        logOpenSslErrors();
        return fail(ErrorCode::Internal, "could not install certificate");
    }

    X509_free(cert);
    EVP_PKEY_free(pkey);
    LEC_INFO(kCategory, "self-signed TLS ready for LAN host {}", lanIpv4);
    return ok();
}

}  // namespace lectern::net

#pragma once

#include "core/Error.h"

#include <string>

struct ssl_ctx_st;
using SSL_CTX = ssl_ctx_st;

namespace lectern::net {

/// Ephemeral TLS credentials for the phone camera page (self-signed, LAN-only).
/// Mobile browsers require HTTPS before `getUserMedia` on non-localhost hosts.
class TlsServerContext {
public:
    TlsServerContext() = default;
    ~TlsServerContext();
    TlsServerContext(const TlsServerContext&) = delete;
    TlsServerContext& operator=(const TlsServerContext&) = delete;

    [[nodiscard]] Status create(const std::string& lanIpv4);
    [[nodiscard]] SSL_CTX* ctx() const noexcept { return ctx_; }

private:
    SSL_CTX* ctx_ = nullptr;
};

}  // namespace lectern::net

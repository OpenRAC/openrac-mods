// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The transport (transport.hpp): mbedTLS's sockets, and its TLS on top for wss://.
#include "transport.hpp"

#include <mutex>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#ifdef _WIN32
// windows.h first: wincrypt.h needs its types.
#include <windows.h>
// clang-format off
#include <wincrypt.h>
// clang-format on
#endif

namespace ap {

namespace {

// The roots every TLS connection checks against, loaded once for the life of the mod.
mbedtls_x509_crt g_roots;
int g_root_count = -1;
std::mutex g_roots_mutex;

// The files and folders the systems keep their roots in (the first that parses is used).
const char* const kRootFiles[] = {
    "/etc/ssl/certs/ca-certificates.crt",                 // Debian, Ubuntu, Arch, Gentoo
    "/etc/pki/tls/certs/ca-bundle.crt",                   // Fedora, RHEL
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",  // Fedora, RHEL (extracted)
    "/etc/ssl/ca-bundle.pem",                             // openSUSE
    "/etc/ssl/cert.pem",                                  // macOS, Alpine, the BSDs
};
const char* const kRootDirs[] = {
    "/system/etc/security/cacerts",  // Android
    "/etc/ssl/certs",
};

// mbedTLS's text for an error code.
std::string describe(int code) {
    char text[160];
    mbedtls_strerror(code, text, sizeof text);
    return text;
}

#ifdef _WIN32
// Windows keeps its roots in the ROOT store, each in DER.
int load_windows_roots() {
    HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
    int n = 0;

    // No store: no roots.
    if (store == nullptr) {
        return 0;
    }

    // Each certificate of the store.
    for (PCCERT_CONTEXT c = CertEnumCertificatesInStore(store, nullptr); c != nullptr;
         c = CertEnumCertificatesInStore(store, c)) {
        n += mbedtls_x509_crt_parse_der(&g_roots, c->pbCertEncoded, c->cbCertEncoded) == 0;
    }

    CertCloseStore(store, 0);
    return n;
}
#endif

#ifndef _WIN32
// mbedTLS's send, but without the SIGPIPE a write to a closed socket raises on Linux and Android,
// which would end the game: MSG_NOSIGNAL where there is one (macOS has SO_NOSIGPIPE, set at open).
int send_quietly(void* ctx, const unsigned char* buf, size_t len) {
#if defined(MSG_NOSIGNAL)
    const int fd = static_cast<mbedtls_net_context*>(ctx)->fd;
    const ssize_t r = ::send(fd, buf, len, MSG_NOSIGNAL);

    // As mbedtls_net_send says it: try again, or the connection reset.
    if (r < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
                   ? MBEDTLS_ERR_SSL_WANT_WRITE
               : errno == EPIPE || errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET
                                                       : MBEDTLS_ERR_NET_SEND_FAILED;
    }

    return static_cast<int>(r);
#else
    return mbedtls_net_send(ctx, buf, len);
#endif
}
#else
int send_quietly(void* ctx, const unsigned char* buf, size_t len) {
    return mbedtls_net_send(ctx, buf, len);
}
#endif

}  // namespace

int load_system_roots() {
    std::lock_guard<std::mutex> lock(g_roots_mutex);

    // Loaded once already.
    if (g_root_count >= 0) {
        return g_root_count;
    }

    mbedtls_x509_crt_init(&g_roots);
    int n = 0;
#ifdef _WIN32
    n = load_windows_roots();
#else
    // A bundle file, the first that parses.
    for (const char* file : kRootFiles) {
        // parse_file gives how many certificates it could not read, or a negative error.
        if (mbedtls_x509_crt_parse_file(&g_roots, file) >= 0) {
            n = 1;
            break;
        }
    }

    // No bundle: a folder of one certificate per file.
    for (std::size_t i = 0; n == 0 && i < sizeof kRootDirs / sizeof kRootDirs[0]; ++i) {
        n = mbedtls_x509_crt_parse_path(&g_roots, kRootDirs[i]) >= 0;
    }
#endif
    g_root_count = n;
    return n;
}

struct Transport::State {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    bool tls = false;
    bool open = false;

    State() {
        mbedtls_net_init(&net);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
    }

    ~State() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_net_free(&net);
    }
};

Transport::Transport() = default;

Transport::~Transport() {
    close();
}

bool Transport::is_open() const {
    return state_ != nullptr && state_->open;
}

void Transport::close() {
    // Say goodbye on TLS before the socket goes.
    if (state_ != nullptr && state_->open && state_->tls) {
        mbedtls_ssl_close_notify(&state_->ssl);
    }

    state_.reset();
}

bool Transport::open(const std::string& host, int port, bool tls, std::string& error) {
    close();
    state_ = std::make_unique<State>();
    State& s = *state_;
    const std::string service = std::to_string(port);

    // The TCP connection first (mbedTLS resolves the name and tries each address).
    int r = mbedtls_net_connect(&s.net, host.c_str(), service.c_str(), MBEDTLS_NET_PROTO_TCP);

    // No server there.
    if (r != 0) {
        error = "cannot reach " + host + ":" + service + " (" + describe(r) + ")";
        state_.reset();
        return false;
    }

#if defined(SO_NOSIGPIPE)
    // macOS: no SIGPIPE from this socket.
    const int on = 1;
    setsockopt(s.net.fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif

    // Plain ws://: the socket is the stream.
    if (!tls) {
        s.open = true;
        return true;
    }

    // TLS: the random numbers, the configuration and the server's name.
    if (psa_crypto_init() != PSA_SUCCESS) {
        error = "TLS cannot start (no random numbers)";
        state_.reset();
        return false;
    }

    static const char kPersonal[] = "openrac-archipelago";
    r = mbedtls_ctr_drbg_seed(
        &s.drbg,
        mbedtls_entropy_func,
        &s.entropy,
        reinterpret_cast<const unsigned char*>(kPersonal),
        sizeof kPersonal - 1
    );

    // The client's configuration: verify the server against the system's roots.
    if (r == 0) {
        r = mbedtls_ssl_config_defaults(
            &s.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT
        );
    }

    // No roots to check against: refuse, rather than talk to anyone in the middle.
    if (r == 0 && load_system_roots() == 0) {
        error = "TLS: this system's root certificates were not found";
        state_.reset();
        return false;
    }

    // The configuration takes the roots and the random numbers, the context the server's name.
    if (r == 0) {
        mbedtls_ssl_conf_authmode(&s.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&s.conf, &g_roots, nullptr);
        mbedtls_ssl_conf_rng(&s.conf, mbedtls_ctr_drbg_random, &s.drbg);
        r = mbedtls_ssl_setup(&s.ssl, &s.conf);
    }

    if (r == 0) {
        r = mbedtls_ssl_set_hostname(&s.ssl, host.c_str());
    }

    // The handshake, on the socket, with reads that wait (the socket is blocking).
    if (r == 0) {
        mbedtls_ssl_set_bio(&s.ssl, &s.net, send_quietly, nullptr, mbedtls_net_recv_timeout);
        mbedtls_ssl_conf_read_timeout(&s.conf, 10000);

        // The handshake goes on while it asks to read or write more.
        do {
            r = mbedtls_ssl_handshake(&s.ssl);
        } while (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE);
    }

    // Any step failed: say which.
    if (r != 0) {
        const uint32_t flags = mbedtls_ssl_get_verify_result(&s.ssl);
        error = "TLS with " + host + " failed (" + describe(r) + ")";

        // A certificate that did not check out says why.
        if (flags != 0 && flags != static_cast<uint32_t>(-1)) {
            char why[256];
            mbedtls_x509_crt_verify_info(why, sizeof why, "", flags);
            error += ": " + std::string(why);
        }

        state_.reset();
        return false;
    }

    s.tls = true;
    s.open = true;
    return true;
}

bool Transport::send(const char* data, std::size_t size) {
    // Nothing to send on.
    if (!is_open()) {
        return false;
    }

    State& s = *state_;
    std::size_t done = 0;

    // Every byte, in as many writes as it takes.
    while (done < size) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(data + done);
        const int r = s.tls ? mbedtls_ssl_write(&s.ssl, bytes, size - done)
                            : send_quietly(&s.net, bytes, size - done);

        // The stream asks to be tried again.
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }

        // The connection broke.
        if (r <= 0) {
            s.open = false;
            return false;
        }

        done += static_cast<std::size_t>(r);
    }

    return true;
}

long Transport::receive(char* out, std::size_t size, int timeout_ms) {
    // Nothing to read from.
    if (!is_open()) {
        return -1;
    }

    State& s = *state_;
    auto* bytes = reinterpret_cast<unsigned char*>(out);
    int r;

    // TLS may hold decrypted bytes already; else wait on the socket.
    if (s.tls) {
        mbedtls_ssl_conf_read_timeout(&s.conf, static_cast<uint32_t>(timeout_ms));
        r = mbedtls_ssl_read(&s.ssl, bytes, size);
    } else {
        r = mbedtls_net_recv_timeout(&s.net, bytes, size, static_cast<uint32_t>(timeout_ms));
    }

    // Nothing in time, or TLS wants another round (a TLS 1.3 ticket): try again later.
    if (r == MBEDTLS_ERR_SSL_TIMEOUT || r == MBEDTLS_ERR_SSL_WANT_READ
        || r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
        return 0;
    }

    // Closed by the server, or broken.
    if (r <= 0) {
        s.open = false;
        return -1;
    }

    return r;
}

}  // namespace ap

#include "tunnel.h"
#include "tunnel_socket.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

namespace sony {
namespace {
constexpr long timeoutMs = 10000;
bool formatFingerprint(const char* key, size_t size, char (&out)[52]) {
    unsigned char digest[32];
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(key), size, digest, 0)) return false;
    size_t n = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out + 7), sizeof out - 7, &n,
                              digest, sizeof digest) || n != 44) return false;
    memcpy(out, "SHA256:", 7);
    out[50] = 0; // OpenSSH SHA256 notation omits the single padding character
    return true;
}
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void respond(const char*, int, const char*, int, int count, const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
             LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** context) {
    const char* password = static_cast<const char*>(*context);
    if (!password || count != 1 || prompts[0].echo) return;
    size_t n = strlen(password);
    responses[0].text = static_cast<char*>(malloc(n + 1)); // released by the session allocator
    if (responses[0].text) {
        memcpy(responses[0].text, password, n + 1);
        responses[0].length = n;
    }
}
bool method(const char* list, const char* wanted) {
    while (list && *list) {
        size_t n = strcspn(list, ",");
        if (strlen(wanted) == n && !memcmp(list, wanted, n)) return true;
        list += n;
        if (*list) ++list;
    }
    return false;
}
}
void Tunnel::deadline() { until_ = nowMs() + timeoutMs; }
bool Tunnel::timeLeft() {
    if (cancelled_ && cancelled_->load()) return false;
    auto ms = until_ - nowMs();
    if (ms <= 0) return false;
    libssh2_session_set_timeout(ssh_, static_cast<long>(ms));
    return true;
}
bool Tunnel::handshake(uint32_t address, uint16_t port) {
    if (socket_ != LIBSSH2_INVALID_SOCKET || !address) return false;
    socket_ = net::connect(address, port, timeoutMs, cancelled_);
    if (socket_ == LIBSSH2_INVALID_SOCKET) return false;
    if (cancelled_ && cancelled_->load()) return false;
    if (libssh2_init(0)) return false;
    initialized_ = true;
    ssh_ = libssh2_session_init_ex([](size_t n, void**) { return malloc(n); },
        [](void* p, void**) { free(p); }, [](void* p, size_t n, void**) { return realloc(p, n); }, nullptr);
    if (!ssh_) return false;
    libssh2_session_set_timeout(ssh_, timeoutMs);
    if (libssh2_session_handshake(ssh_, socket_)) return false;
    size_t keySize = 0;
    int keyType = 0;
    const char* key = libssh2_session_hostkey(ssh_, &keySize, &keyType);
    return key && keySize && formatFingerprint(key, keySize, fingerprint_);
}
bool Tunnel::authenticate(const char* confirmedFingerprint, const char* user, const char* password) {
    if (!ssh_ || !fingerprint_[0] || !confirmedFingerprint || strcmp(fingerprint_, confirmedFingerprint) ||
        !user || !*user || !password || !*password || authenticated_) return false;
    const char* list = libssh2_userauth_list(ssh_, user, strlen(user));
    int rc;
    if (method(list, "keyboard-interactive")) {
        *libssh2_session_abstract(ssh_) = const_cast<char*>(password);
        rc = libssh2_userauth_keyboard_interactive(ssh_, user, respond);
        *libssh2_session_abstract(ssh_) = nullptr;
    } else if (method(list, "password")) {
        rc = libssh2_userauth_password(ssh_, user, password);
    } else return false;
    authenticated_ = rc == 0 && libssh2_userauth_authenticated(ssh_);
    return authenticated_;
}
bool Tunnel::open(Stream s) {
    auto& ch = channels_[int(s)];
    if (!authenticated_ || ch || !timeLeft()) return false;
    ch = libssh2_channel_direct_tcpip_ex(ssh_, "localhost", 15740, "127.0.0.1", 0);
    return ch != nullptr;
}
bool Tunnel::read(Stream s, void* out, size_t n) {
    auto p = static_cast<char*>(out);
    while (n && timeLeft()) {
        auto got = libssh2_channel_read(channels_[int(s)], p, n);
        if (got <= 0) return false;
        p += got; n -= got;
    }
    return n == 0;
}
bool Tunnel::write(Stream s, const void* data, size_t n) {
    auto p = static_cast<const char*>(data);
    while (n && timeLeft()) {
        auto sent = libssh2_channel_write(channels_[int(s)], p, n);
        if (sent <= 0) return false;
        p += sent; n -= sent;
    }
    return n == 0;
}
int Tunnel::tryEventByte(uint8_t& byte) {
    libssh2_session_set_blocking(ssh_, 0);
    auto n = libssh2_channel_read(channels_[1], reinterpret_cast<char*>(&byte), 1);
    libssh2_session_set_blocking(ssh_, 1);
    return n == LIBSSH2_ERROR_EAGAIN ? 0 : n == 1 ? 1 : -1;
}
void Tunnel::pause(unsigned ms) { net::sleep(ms); }
void Tunnel::interrupt() {
    const auto socket = socket_.load();
    if (socket != LIBSSH2_INVALID_SOCKET) net::shutdown(socket);
}
Tunnel::~Tunnel() {
    interrupt();
    if (ssh_) {
        libssh2_session_set_timeout(ssh_, 500);
        libssh2_session_free(ssh_); // session owns both channels, also on partial init
    }
    if (socket_ != LIBSSH2_INVALID_SOCKET) net::close(socket_);
    if (initialized_) libssh2_exit();
}
}

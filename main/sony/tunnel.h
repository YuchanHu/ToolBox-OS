#pragma once
#include "session.h"
#include <atomic>
#include <libssh2.h>

namespace sony {
class Tunnel final : public Wire {
public:
    Tunnel() = default;
    Tunnel(const Tunnel&) = delete;
    Tunnel& operator=(const Tunnel&) = delete;
    ~Tunnel();
    // Connect and display the peer key before any credential can be submitted.
    bool handshake(uint32_t ipv4NetworkOrder, uint16_t port = 22);
    const char* fingerprint() const { return fingerprint_; }
    const char* algorithm(int kind) const { return libssh2_session_methods(ssh_, kind); }
    int lastError() const { return ssh_ ? libssh2_session_last_errno(ssh_) : 0; }
    bool authenticate(const char* confirmedFingerprint, const char* user, const char* password);
    void cancelWhen(const std::atomic_bool& cancelled) { cancelled_ = &cancelled; }
    void interrupt();
    bool open(Stream) override;
    void deadline() override;
    bool read(Stream, void*, size_t) override;
    bool write(Stream, const void*, size_t) override;
    int tryEventByte(uint8_t&) override;
    void pause(unsigned ms) override;
private:
    bool timeLeft();
    const std::atomic_bool* cancelled_ = nullptr;
    LIBSSH2_SESSION* ssh_ = nullptr;
    LIBSSH2_CHANNEL* channels_[2]{};
    std::atomic<libssh2_socket_t> socket_{LIBSSH2_INVALID_SOCKET};
    int64_t until_ = 0;
    char fingerprint_[52]{};
    bool authenticated_ = false, initialized_ = false;
};
}

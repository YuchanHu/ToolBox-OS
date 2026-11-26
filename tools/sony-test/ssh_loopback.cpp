// Windows OS adapter for the loopback test. Authentication, both SSH channels,
// PTP framing, SDIO and property decoding are compiled from the firmware sources.
#include "sony/tunnel.h"
#include "sony/tunnel_socket.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sony::net {
libssh2_socket_t connect(uint32_t address, uint16_t port, unsigned, const std::atomic_bool*) {
    if (address != htonl(INADDR_LOOPBACK)) return INVALID_SOCKET;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in peer{};
    peer.sin_family = AF_INET; peer.sin_addr.s_addr = address; peer.sin_port = htons(port);
    if (::connect(s, reinterpret_cast<sockaddr*>(&peer), sizeof peer) == 0) return s;
    closesocket(s);
    return INVALID_SOCKET;
}
void sleep(unsigned ms) { Sleep(ms); }
void shutdown(libssh2_socket_t s) { ::shutdown(s, SD_BOTH); }
void close(libssh2_socket_t s) { closesocket(s); }
}
static int run(char** argv) {
    sony::Tunnel tunnel;
    if (!tunnel.handshake(htonl(INADDR_LOOPBACK), uint16_t(atoi(argv[1])))) {
        std::fprintf(stderr, "SSH handshake failed: libssh2=%d winsock=%d\n", tunnel.lastError(), WSAGetLastError());
        return 2;
    }
    if (strcmp(tunnel.fingerprint(), argv[2])) return 3;
    bool mismatch = !strcmp(argv[3], "pin"), denied = !strcmp(argv[3], "denied");
    const char* password = getenv("SONY_LOOPBACK_SECRET");
    bool auth = tunnel.authenticate(mismatch ? "SHA256:incorrect" : argv[2], "fixture", password);
    if (mismatch || denied) return auth ? 4 : 0;
    if (!auth) return 5;
    std::printf("Loopback KEX=%s key=%s cipher=%s MAC=%s\n", tunnel.algorithm(LIBSSH2_METHOD_KEX),
                tunnel.algorithm(LIBSSH2_METHOD_HOSTKEY), tunnel.algorithm(LIBSSH2_METHOD_CRYPT_CS),
                tunnel.algorithm(LIBSSH2_METHOD_MAC_CS));
    uint8_t scratch[16384]{}, guid[16]{1};
    sony::Session s(tunnel, scratch, sizeof scratch);
    int rc = s.start(guid);
    if (rc || strcmp(s.model(), "Loopback") || s.version() != 300 || s.recordingRaw() != 1) {
        std::fprintf(stderr, "SDIO rc=%d last=%04x state=%d\n", rc, s.lastOp(), s.recordingRaw());
        return 6;
    }
    // This independent responder advertises half-press only.
    if (s.capture(1) != sony::Unsupported || s.movie(1) != sony::Unsupported) return 7;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        sony::Event e{};
        rc = s.event(e);
        if (rc < 0) return 8;
        if (rc == 1 && e.code == 0xc203 && e.count && e.params[0] == 0xd21d) {
            s.close();
            return 0;
        }
        Sleep(20);
    }
    return 9;
}
int main(int argc, char** argv) {
    if (argc != 4) return 1;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
    int rc = run(argv);
    WSACleanup();
    return rc;
}

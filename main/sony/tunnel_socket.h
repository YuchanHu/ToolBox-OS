#pragma once
#include <atomic>
#include <libssh2.h>
#include <cstdint>

namespace sony::net {
// The ESP implementation uses lwIP, mbedTLS and FreeRTOS. The Windows
// loopback test uses Winsock and the same SSH/PTP session logic.
libssh2_socket_t connect(uint32_t ipv4NetworkOrder, uint16_t port, unsigned timeoutMs,
                         const std::atomic_bool* cancelled);
void shutdown(libssh2_socket_t);
void close(libssh2_socket_t);
void sleep(unsigned ms);
}

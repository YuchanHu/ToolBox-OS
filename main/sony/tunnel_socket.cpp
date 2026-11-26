#include "tunnel_socket.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

namespace sony::net {
libssh2_socket_t connect(uint32_t address, uint16_t port, unsigned timeoutMs, const std::atomic_bool* cancelled) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
    sockaddr_in peer{};
    peer.sin_family = AF_INET; peer.sin_port = htons(port); peer.sin_addr.s_addr = address;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == 0) {
        if (::connect(fd, reinterpret_cast<sockaddr*>(&peer), sizeof peer) == 0) return fd;
        if (errno == EINPROGRESS) {
            int64_t until = esp_timer_get_time() + int64_t(timeoutMs) * 1000;
            while (!cancelled || !cancelled->load()) {
                auto left = until - esp_timer_get_time();
                if (left <= 0) break;
                fd_set writeSet;
                FD_ZERO(&writeSet); FD_SET(fd, &writeSet);
                timeval tv{0, long(std::min<int64_t>(left, 200000))};
                const int ready = select(fd + 1, nullptr, &writeSet, nullptr, &tv);
                if (!ready) continue;
                int err = 0;
                socklen_t n = sizeof err;
                if (ready == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &n) == 0 && !err) return fd;
                break;
            }
        }
    }
    ::close(fd);
    return -1;
}
void shutdown(libssh2_socket_t fd) { ::shutdown(fd, SHUT_RDWR); }
void close(libssh2_socket_t fd) { ::close(fd); }
void sleep(unsigned ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
}

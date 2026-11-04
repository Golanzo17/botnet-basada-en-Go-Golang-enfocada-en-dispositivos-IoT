/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: src/network.c
 * Descripción: Implementación de conexiones TCP no bloqueantes con timeout (POSIX)
 *              y contabilización de bytes enviados (TX) y recibidos (RX).
 * ============================================================================
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "network.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

int network_dial_tcp_timeout(const char *ip, int port, int timeout_ms,
                             const uint8_t *payload, size_t payload_len,
                             telemetry_t *t) {
    if (!ip || port <= 0 || port > 65535) {
        if (t) atomic_fetch_add(&t->failed_dials, 1);
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0) {
        if (t) atomic_fetch_add(&t->failed_dials, 1);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sockfd);
        if (t) atomic_fetch_add(&t->failed_dials, 1);
        return -1;
    }

    int res = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) {
        close(sockfd);
        if (t) atomic_fetch_add(&t->failed_dials, 1);
        return -1;
    }

    if (res != 0) {
        // Conexión en progreso (SYN_SENT): esperar mediante epoll con timeout estricto
        int epfd = epoll_create1(EPOLL_CLOEXEC);
        if (epfd < 0) {
            close(sockfd);
            if (t) atomic_fetch_add(&t->failed_dials, 1);
            return -1;
        }

        struct epoll_event ev, events[1];
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
        ev.data.fd = sockfd;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) < 0) {
            close(epfd);
            close(sockfd);
            if (t) atomic_fetch_add(&t->failed_dials, 1);
            return -1;
        }

        int nfds = epoll_wait(epfd, events, 1, timeout_ms);
        close(epfd);

        if (nfds <= 0) {
            // Timeout alcanzado
            close(sockfd);
            if (t) atomic_fetch_add(&t->failed_dials, 1);
            return -1;
        }

        // Verificar el estado asíncrono de finalización de conexión
        int so_error = 0;
        socklen_t optlen = sizeof(so_error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &optlen) < 0 || so_error != 0) {
            close(sockfd);
            if (t) atomic_fetch_add(&t->failed_dials, 1);
            return -1;
        }
    }

    // Conexión exitosa: registrar socket activo y enviar payload al servidor mock
    if (t) {
        atomic_fetch_add(&t->active_sockets, 1);
        atomic_fetch_add(&t->successful_dials, 1);
    }

    if (payload && payload_len > 0) {
        ssize_t sent = send(sockfd, payload, payload_len, MSG_NOSIGNAL);
        if (sent > 0 && t) {
            atomic_fetch_add(&t->bytes_transferred, sent);

            // Esperar brevemente respuesta ACK_OK del servidor mock para contabilizar RX
            int repfd = epoll_create1(EPOLL_CLOEXEC);
            if (repfd >= 0) {
                struct epoll_event rev, revents[1];
                memset(&rev, 0, sizeof(rev));
                rev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
                rev.data.fd = sockfd;
                if (epoll_ctl(repfd, EPOLL_CTL_ADD, sockfd, &rev) == 0) {
                    if (epoll_wait(repfd, revents, 1, 15) > 0) {
                        uint8_t rx_buf[64];
                        ssize_t r = recv(sockfd, rx_buf, sizeof(rx_buf), MSG_DONTWAIT);
                        if (r > 0) {
                            atomic_fetch_add(&t->bytes_received, r);
                        }
                    }
                }
                close(repfd);
            }
        }
    }

    close(sockfd);
    if (t) {
        atomic_fetch_sub(&t->active_sockets, 1);
    }

    return 0;
}

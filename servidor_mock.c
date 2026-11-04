/**
 * ============================================================================
 * SERVIDOR DE ESCUCHA LOCAL (MOCK TCP SINK) - Puerto 9090
 * ============================================================================
 * Propósito:
 *   Recibir las conexiones TCP locales de `cliente_c` y `cliente_go` en un
 *   proceso o contenedor separado para no sesgar las mediciones de RAM/CPU
 *   del cliente bajo prueba (Ref: Fig. 6 del informe).
 *
 * Compilación:
 *   gcc -O2 -pthread servidor_mock.c -o servidor_mock
 * ============================================================================
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 1024
#define LISTEN_PORT 9090

static volatile sig_atomic_t running = 1;
static volatile long total_conns = 0;
static volatile long interval_conns = 0;
static volatile long interval_bytes = 0;

static void handle_sig(int sig) {
    (void)sig;
    running = 0;
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void *stats_thread(void *arg) {
    (void)arg;
    while (running) {
        sleep(1);
        long c = __sync_lock_test_and_set(&interval_conns, 0);
        long b = __sync_lock_test_and_set(&interval_bytes, 0);
        printf("\r[MOCK SERVER :%d] Conexiones Totales: %ld | Conns/s: %ld | RX: %.1f KB/s   ",
               LISTEN_PORT, total_conns, c, (double)b / 1024.0);
        fflush(stdout);
    }
    return NULL;
}

int main(void) {
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_nonblocking(listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(LISTEN_PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 4096) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    int epfd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    printf("=================================================================\n");
    printf(" SERVIDOR DE ESCUCHA LOCAL (MOCK TCP SINK) ACTIVO EN PUERTO %d\n", LISTEN_PORT);
    printf("=================================================================\n");

    pthread_t tid;
    pthread_create(&tid, NULL, stats_thread, NULL);

    char buf[256];
    const char *ack = "ACK_OK\n";

    while (running) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 250);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listen_fd) {
                while (1) {
                    struct sockaddr_in cli_addr;
                    socklen_t cli_len = sizeof(cli_addr);
                    int conn_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
                    if (conn_fd < 0) break;
                    set_nonblocking(conn_fd);
                    struct epoll_event cev;
                    cev.events = EPOLLIN | EPOLLET;
                    cev.data.fd = conn_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &cev);
                    __sync_add_and_fetch(&total_conns, 1);
                    __sync_add_and_fetch(&interval_conns, 1);
                }
            } else {
                int cfd = events[i].data.fd;
                ssize_t r = recv(cfd, buf, sizeof(buf), 0);
                if (r > 0) {
                    __sync_add_and_fetch(&interval_bytes, r);
                    send(cfd, ack, 7, MSG_NOSIGNAL);
                }
                epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
                close(cfd);
            }
        }
    }

    printf("\n[+] Cerrando servidor mock...\n");
    close(epfd);
    close(listen_fd);
    return 0;
}


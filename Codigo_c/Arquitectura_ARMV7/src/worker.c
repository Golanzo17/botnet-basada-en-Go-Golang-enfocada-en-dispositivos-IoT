/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura ARMV7 / 32-bit ARM)
 * Archivo: src/worker.c
 * Descripción: Implementación del worker concurrente con multiplexación epoll
 *              real de N slots por hilo (16 hilos x 4 sockets reales = 64 UCA)
 *              e instrumentación atómica real de Unidades Concurrentes (UCA).
 * ============================================================================
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "worker.h"
#include "network.h"
#include "sha256.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_SLOTS_PER_WORKER 16

static void *worker_thread_func(void *arg) {
    worker_context_t *ctx = (worker_context_t *)arg;
    int id = ctx->worker_id;
    int slots = ctx->slots_assigned;
    if (slots < 1) slots = 1;
    if (slots > MAX_SLOTS_PER_WORKER) slots = MAX_SLOTS_PER_WORKER;

    const Config *cfg = ctx->cfg;
    telemetry_t *telemetry = ctx->telemetry;
    volatile sig_atomic_t *keep_running = ctx->keep_running;

    atomic_fetch_add(&telemetry->active_slots, slots);

    unsigned int seed = (unsigned int)(time(NULL) ^ (id * 7919) ^ (uintptr_t)pthread_self());

    char payload_str[128];
    int payload_len = snprintf(payload_str, sizeof(payload_str),
                               "synthetic_payload_signature_data_block_worker_%d", id);
    if (payload_len < 0 || (size_t)payload_len >= sizeof(payload_str)) {
        payload_len = (int)sizeof(payload_str) - 1;
        payload_str[payload_len] = '\0';
    }

    uint8_t digest[SHA256_DIGEST_SIZE];
    int iter = 0;

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    int fds[MAX_SLOTS_PER_WORKER];

    while (*keep_running) {
        int opened = 0;

        // 1. Abrir 'slots' sockets TCP no bloqueantes reales en paralelo y registrar cada unidad real (+1)
        for (int s = 0; s < slots; s++) {
            fds[s] = -1;
            int is_unreachable = (cfg->unreachable_ratio > 0) &&
                                 ((rand_r(&seed) % 100) < cfg->unreachable_ratio);
            const char *target_ip = is_unreachable ? cfg->unreachable_host : cfg->target_host;
            int target_port = is_unreachable ? cfg->unreachable_port : cfg->base_port;

            if (is_unreachable) {
                atomic_fetch_add(&telemetry->syn_sent_dials, 1);
            }

            int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                atomic_fetch_add(&telemetry->failed_dials, 1);
                continue;
            }

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port   = htons((uint16_t)target_port);
            if (inet_pton(AF_INET, target_ip, &addr.sin_addr) <= 0) {
                close(fd);
                atomic_fetch_add(&telemetry->failed_dials, 1);
                continue;
            }

            int res = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
            if (res < 0 && errno != EINPROGRESS) {
                close(fd);
                atomic_fetch_add(&telemetry->failed_dials, 1);
                continue;
            }

            fds[s] = fd;
            opened++;

            // Contabilización estricta: +1 por cada socket real abierto
            long cur_uca = atomic_fetch_add(&telemetry->uca_active, 1) + 1;
            telemetry_update_uca_max(telemetry, cur_uca);

            if (epfd >= 0) {
                struct epoll_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
                ev.data.fd = fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
            }
        }

        // 2. Ejecutar carga sintética de memoria (malloc/free 4-8 KB) y CPU (250 SHA-256)
        size_t buffer_size = 4096 + (size_t)(rand_r(&seed) % 4096);
        uint8_t *dynamic_buf = (uint8_t *)malloc(buffer_size);
        if (dynamic_buf) {
            for (size_t b = 0; b < buffer_size; b++) {
                dynamic_buf[b] = (uint8_t)((iter + b) % 256);
            }
            sha256_compute(dynamic_buf, buffer_size, digest);
            free(dynamic_buf);
            atomic_fetch_add(&telemetry->manual_frees, 1);
        }

        for (int k = 0; k < cfg->cpu_loop_work; k++) {
            sha256_compute((const uint8_t *)payload_str, (size_t)payload_len, digest);
        }
        iter++;

        // 3. Multiplexar envío y recepción de los sockets reales mediante epoll
        if (epfd >= 0 && opened > 0) {
            struct epoll_event events[MAX_SLOTS_PER_WORKER];
            int nfds = epoll_wait(epfd, events, slots, cfg->dial_timeout_ms);
            for (int e = 0; e < nfds; e++) {
                int fd = events[e].data.fd;
                if (events[e].events & (EPOLLERR | EPOLLHUP)) {
                    atomic_fetch_add(&telemetry->failed_dials, 1);
                    continue;
                }
                int so_error = 0;
                socklen_t optlen = sizeof(so_error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &optlen) < 0 || so_error != 0) {
                    atomic_fetch_add(&telemetry->failed_dials, 1);
                    continue;
                }
                atomic_fetch_add(&telemetry->successful_dials, 1);
                atomic_fetch_add(&telemetry->active_sockets, 1);

                ssize_t sent = send(fd, payload_str, (size_t)payload_len, MSG_NOSIGNAL);
                if (sent > 0) {
                    atomic_fetch_add(&telemetry->bytes_transferred, sent);
                    uint8_t rx_buf[64];
                    ssize_t r = recv(fd, rx_buf, sizeof(rx_buf), MSG_DONTWAIT);
                    if (r > 0) {
                        atomic_fetch_add(&telemetry->bytes_received, r);
                    }
                }
                atomic_fetch_sub(&telemetry->active_sockets, 1);
            }
        }

        // 4. Cerrar cada socket real y decrementar -1 por unidad real
        for (int s = 0; s < slots; s++) {
            if (fds[s] >= 0) {
                if (epfd >= 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fds[s], NULL);
                }
                close(fds[s]);
                fds[s] = -1;
                atomic_fetch_sub(&telemetry->uca_active, 1);
            }
        }

        // 5. Pausa de ciclo (1 ms)
        if (cfg->worker_delay_us > 0) {
            usleep((useconds_t)cfg->worker_delay_us);
        }
    }

    if (epfd >= 0) {
        close(epfd);
    }
    atomic_fetch_sub(&telemetry->active_slots, slots);
    return NULL;
}

int worker_pool_start(worker_pool_t *pool, const Config *cfg,
                      telemetry_t *telemetry, volatile sig_atomic_t *keep_running) {
    if (!pool || !cfg || !telemetry || !keep_running) return -1;

    pool->num_workers = cfg->num_workers;
    pool->keep_running = keep_running;

    pool->threads = (pthread_t *)calloc((size_t)cfg->num_workers, sizeof(pthread_t));
    pool->contexts = (worker_context_t *)calloc((size_t)cfg->num_workers, sizeof(worker_context_t));

    if (!pool->threads || !pool->contexts) {
        perror("calloc worker pool ARMv7");
        free(pool->threads);
        free(pool->contexts);
        return -1;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);

    int base_slots = cfg->simulated_slots / cfg->num_workers;
    int remainder  = cfg->simulated_slots % cfg->num_workers;

    int created = 0;
    for (int i = 0; i < cfg->num_workers; i++) {
        pool->contexts[i].worker_id      = i;
        pool->contexts[i].slots_assigned = base_slots + (i < remainder ? 1 : 0);
        pool->contexts[i].cfg            = cfg;
        pool->contexts[i].telemetry      = telemetry;
        pool->contexts[i].keep_running   = keep_running;

        if (pthread_create(&pool->threads[i], &attr, worker_thread_func, &pool->contexts[i]) != 0) {
            perror("pthread_create worker ARMv7");
            *keep_running = 0;
            break;
        }
        created++;
    }

    pthread_attr_destroy(&attr);

    if (created < cfg->num_workers) {
        for (int j = 0; j < created; j++) {
            pthread_join(pool->threads[j], NULL);
        }
        free(pool->threads);
        free(pool->contexts);
        pool->threads = NULL;
        pool->contexts = NULL;
        return -1;
    }

    return 0;
}

void worker_pool_wait_and_stop(worker_pool_t *pool) {
    if (!pool || !pool->threads) return;

    *pool->keep_running = 0;

    for (int i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    free(pool->contexts);
    pool->threads = NULL;
    pool->contexts = NULL;
}

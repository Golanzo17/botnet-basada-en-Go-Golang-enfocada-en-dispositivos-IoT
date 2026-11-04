/*
 * network.c — Subsistema de red con epoll (Arquitectura MIPS)
 *
 * Gestiona conexiones TCP no-bloqueantes usando epoll de Linux.
 * epoll es un syscall del kernel Linux, independiente de la arquitectura
 * del procesador. Funciona exactamente igual en MIPS que en x86.
 *
 * Particularidades MIPS a considerar:
 *   - struct epoll_event: contiene uint32_t + epoll_data_t (uint64_t).
 *     En MIPS 32-bit big-endian, el campo data.ptr es un puntero 32-bit
 *     embebido en un campo de 64-bit. El kernel maneja esto correctamente.
 *   - connect() en MIPS es idéntico en semántica a x86 (POSIX socket API).
 *   - EINPROGRESS, EISCONN, etc. tienen los mismos valores en MIPS Linux.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdatomic.h>

#include "network.h"
#include "memory.h"
#include "telemetry.h"
#include "sha256.h"

/* -------------------------------------------------------------------------
 * Constantes internas
 * ------------------------------------------------------------------------- */

/** Máximo de eventos epoll procesados por tick. */
#define MAX_EVENTS      64

/** Tamaño del payload SHA-256 hex: 64 chars + '\n' + '\0'. */
#define PAYLOAD_SIZE    66

/* -------------------------------------------------------------------------
 * Funciones auxiliares internas
 * ------------------------------------------------------------------------- */

/**
 * set_nonblocking() — Pone un file descriptor en modo no-bloqueante.
 *
 * Usa fcntl(O_NONBLOCK), compatible con MIPS Linux.
 *
 * @return 0 si OK, -1 si error.
 */
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * make_seed() — Genera una semilla pseudo-aleatoria para SHA-256.
 *
 * Combina tiempo, PID, y ID del slot para variedad de payloads.
 * No requiere /dev/urandom para evitar overhead en MIPS embebido.
 *
 * @param slot_id   ID del slot (0-indexed).
 * @param buf       Buffer de salida (mínimo 32 bytes).
 * @param buf_size  Tamaño del buffer de salida.
 */
static void make_seed(uint32_t slot_id, uint8_t *buf, size_t buf_size)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* XOR de tiempo nanosegundos, PID y slot_id para variedad */
    uint32_t seed_a = (uint32_t)(ts.tv_nsec & 0xFFFFFFFFUL);
    uint32_t seed_b = (uint32_t)(ts.tv_sec  & 0xFFFFFFFFUL);
    uint32_t seed_c = slot_id;

    /* Llenar el buffer con los seeds repetidos */
    for (size_t i = 0; i < buf_size; i++) {
        buf[i] = (uint8_t)((seed_a >> (i % 32)) ^ (seed_b >> ((i + 7) % 32))
                           ^ (seed_c >> ((i + 13) % 32)) ^ (uint8_t)i);
    }
}

/* -------------------------------------------------------------------------
 * Implementación pública
 * ------------------------------------------------------------------------- */

/**
 * net_pool_create() — Crea el pool de red.
 */
NetPool *net_pool_create(const Config *cfg)
{
    NetPool *pool = (NetPool *)mem_alloc(sizeof(NetPool));
    if (!pool) return NULL;

    pool->cfg       = cfg;
    pool->num_slots = cfg->slots;

    /* Crear instancia epoll */
    pool->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (pool->epoll_fd < 0) {
        fprintf(stderr, "[NETWORK MIPS] epoll_create1 falló: %s\n", strerror(errno));
        mem_free(pool);
        return NULL;
    }

    /* Alocar array de slots */
    pool->slots = (Slot *)mem_alloc(sizeof(Slot) * (size_t)cfg->slots);
    if (!pool->slots) {
        close(pool->epoll_fd);
        mem_free(pool);
        return NULL;
    }

    /* Inicializar todos los slots */
    for (int i = 0; i < cfg->slots; i++) {
        pool->slots[i].fd      = -1;
        pool->slots[i].state   = SLOT_FREE;
        pool->slots[i].retries = MAX_RETRIES;
        pool->slots[i].id      = (uint32_t)i;
    }

    return pool;
}

/**
 * net_pool_destroy() — Destruye el pool y libera todos los recursos.
 */
void net_pool_destroy(NetPool *pool)
{
    if (!pool) return;

    for (int i = 0; i < pool->num_slots; i++) {
        if (pool->slots[i].fd >= 0) {
            close(pool->slots[i].fd);
            pool->slots[i].fd = -1;
        }
    }

    if (pool->epoll_fd >= 0) {
        close(pool->epoll_fd);
        pool->epoll_fd = -1;
    }

    mem_free(pool->slots);
    mem_free(pool);
}

/**
 * net_slot_connect() — Inicia una conexión async TCP en el slot.
 */
int net_slot_connect(NetPool *pool, Slot *slot)
{
    if (!pool->cfg->addr) return -1;

    int fd = socket(pool->cfg->addr->ai_family,
                    pool->cfg->addr->ai_socktype,
                    pool->cfg->addr->ai_protocol);
    if (fd < 0) {
        return -1;
    }

    /* Modo no-bloqueante */
    if (set_nonblocking(fd) < 0) {
        close(fd);
        return -1;
    }

    /* TCP_NODELAY: reduce latencia en MIPS embebido */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    /* Iniciar conexión async */
    int ret = connect(fd, pool->cfg->addr->ai_addr,
                      pool->cfg->addr->ai_addrlen);
    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        atomic_fetch_add(&g_metrics.total_timeouts, 1L);
        return -1;
    }

    /* Registrar en epoll para EPOLLOUT (conexión completada) */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events   = EPOLLOUT | EPOLLERR | EPOLLHUP;
    ev.data.ptr = slot;

    if (epoll_ctl(pool->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        close(fd);
        return -1;
    }

    slot->fd    = fd;
    slot->state = SLOT_CONNECTING;

    atomic_fetch_add(&g_metrics.total_dials,  1L);
    atomic_fetch_add(&g_metrics.active_slots, 1);

    return 0;
}

/**
 * net_slot_reset() — Cierra el slot y lo devuelve a FREE.
 */
void net_slot_reset(NetPool *pool, Slot *slot)
{
    if (slot->fd >= 0) {
        epoll_ctl(pool->epoll_fd, EPOLL_CTL_DEL, slot->fd, NULL);
        close(slot->fd);
        slot->fd = -1;
        atomic_fetch_sub(&g_metrics.active_slots, 1);
    }
    slot->state   = SLOT_FREE;
    slot->retries = MAX_RETRIES;
}

/**
 * net_slot_send() — Envía un payload SHA-256 hexadecimal.
 *
 * Genera una semilla con clock_gettime + slot_id, calcula SHA-256,
 * y envía los 64 bytes hex + '\n' al socket conectado.
 */
int net_slot_send(Slot *slot)
{
    if (slot->fd < 0 || slot->state != SLOT_CONNECTED) return -1;

    uint8_t seed[32];
    make_seed(slot->id, seed, sizeof(seed));

    char hexbuf[65];
    sha256_hex(seed, sizeof(seed), hexbuf);

    char payload[PAYLOAD_SIZE];
    int plen = snprintf(payload, sizeof(payload), "%s\n", hexbuf);

    ssize_t sent = send(slot->fd, payload, (size_t)plen, MSG_NOSIGNAL);
    if (sent > 0) {
        atomic_fetch_add(&g_metrics.total_bytes_sent, (long)sent);
        return (int)sent;
    }

    return -1;
}

/**
 * net_pool_tick() — Procesa eventos epoll del período actual.
 */
int net_pool_tick(NetPool *pool, int timeout_ms)
{
    struct epoll_event events[MAX_EVENTS];
    int n = epoll_wait(pool->epoll_fd, events, MAX_EVENTS, timeout_ms);

    if (n < 0) {
        if (errno == EINTR) return 0;  /* señal recibida, no es error fatal */
        return -1;
    }

    for (int i = 0; i < n; i++) {
        Slot *slot = (Slot *)events[i].data.ptr;
        if (!slot) continue;

        uint32_t ev = events[i].events;

        if (ev & (EPOLLERR | EPOLLHUP)) {
            /* Error en la conexión */
            atomic_fetch_add(&g_metrics.total_timeouts, 1L);
            net_slot_reset(pool, slot);
            continue;
        }

        if (slot->state == SLOT_CONNECTING && (ev & EPOLLOUT)) {
            /* Verificar si connect() completó exitosamente */
            int err = 0;
            socklen_t errlen = sizeof(err);
            getsockopt(slot->fd, SOL_SOCKET, SO_ERROR, &err, &errlen);

            if (err != 0) {
                atomic_fetch_add(&g_metrics.total_timeouts, 1L);
                net_slot_reset(pool, slot);
                continue;
            }

            /* Conexión establecida: cambiar a CONNECTED y esperar lectura */
            slot->state = SLOT_CONNECTED;

            struct epoll_event mod_ev;
            memset(&mod_ev, 0, sizeof(mod_ev));
            mod_ev.events   = EPOLLIN | EPOLLERR | EPOLLHUP;
            mod_ev.data.ptr = slot;
            epoll_ctl(pool->epoll_fd, EPOLL_CTL_MOD, slot->fd, &mod_ev);

            /* Enviar payload inmediatamente tras conectar */
            net_slot_send(slot);
            continue;
        }

        if (slot->state == SLOT_CONNECTED && (ev & EPOLLIN)) {
            /* Leer respuesta del servidor mock */
            char buf[BUFFER_SIZE];
            ssize_t recv_bytes = recv(slot->fd, buf, sizeof(buf) - 1, 0);

            if (recv_bytes <= 0) {
                /* Servidor cerró la conexión: reconectar */
                net_slot_reset(pool, slot);
            } else {
                atomic_fetch_add(&g_metrics.total_bytes_recv, (long)recv_bytes);
                /* Enviar otro payload */
                net_slot_send(slot);
            }
        }
    }

    return n;
}

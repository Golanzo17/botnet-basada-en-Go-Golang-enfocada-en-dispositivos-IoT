/*
 * network.h — Subsistema de red con epoll (Arquitectura MIPS)
 *
 * Gestiona conexiones TCP no-bloqueantes usando epoll (Linux).
 * Cada slot mantiene su propio file descriptor y estado de conexión.
 *
 * epoll es un syscall de Linux portable a MIPS; no es específico de x86.
 * Los sockets no-bloqueantes y epoll funcionan igual en MIPS Linux.
 *
 * Compatible con MIPS 32-bit. Los fd son int (32-bit), los eventos
 * epoll_event usan uint32_t y uint64_t (alineados correctamente en MIPS).
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <netdb.h>      /* struct addrinfo */
#include "config.h"

/* -------------------------------------------------------------------------
 * Estados de un slot de conexión
 * ------------------------------------------------------------------------- */

/** Estados posibles de un slot de red. */
typedef enum {
    SLOT_FREE       = 0,    /**< Slot disponible, sin uso.                 */
    SLOT_CONNECTING = 1,    /**< Conexión TCP en progreso (async).         */
    SLOT_CONNECTED  = 2,    /**< Conexión establecida, listo para I/O.     */
    SLOT_ERROR      = 3,    /**< Error en la conexión, pendiente de reset. */
} SlotState;

/* -------------------------------------------------------------------------
 * Estructura de un slot de conexión
 * ------------------------------------------------------------------------- */

/**
 * Representa una conexión TCP individual (slot).
 * El array de slots se aloca con mem_alloc() al iniciar.
 */
typedef struct {
    int         fd;         /**< File descriptor del socket (o -1 si libre).*/
    SlotState   state;      /**< Estado actual del slot.                     */
    int         retries;    /**< Intentos de reconexión restantes.           */
    uint32_t    id;         /**< Identificador único del slot (0-indexed).   */
} Slot;

/* -------------------------------------------------------------------------
 * Estructura del pool de red
 * ------------------------------------------------------------------------- */

/**
 * Pool de slots de conexión gestionados por epoll.
 * Se crea uno por proceso; los workers comparten el epoll_fd.
 */
typedef struct {
    int         epoll_fd;   /**< File descriptor del epoll instance.        */
    Slot       *slots;      /**< Array dinámico de Slot[cfg->slots].        */
    int         num_slots;  /**< Cantidad de slots activos.                 */
    const Config *cfg;      /**< Configuración de referencia (read-only).   */
} NetPool;

/* -------------------------------------------------------------------------
 * Funciones públicas
 * ------------------------------------------------------------------------- */

/**
 * net_pool_create() — Crea e inicializa el pool de red.
 *
 * Aloca el array de slots y crea el epoll_fd.
 *
 * @param cfg   Configuración (slots, host, port, addr).
 * @return      Puntero al NetPool alocado, o NULL si falla.
 */
NetPool *net_pool_create(const Config *cfg);

/**
 * net_pool_destroy() — Destruye el pool de red y libera recursos.
 *
 * Cierra todos los file descriptors abiertos y libera memoria.
 *
 * @param pool  Pool a destruir.
 */
void net_pool_destroy(NetPool *pool);

/**
 * net_slot_connect() — Inicia una conexión async en un slot.
 *
 * Crea socket no-bloqueante, llama a connect() (retorna EINPROGRESS),
 * y registra en epoll para EPOLLOUT.
 *
 * @param pool  Pool de red.
 * @param slot  Slot a conectar.
 * @return      0 si OK, -1 si error.
 */
int net_slot_connect(NetPool *pool, Slot *slot);

/**
 * net_slot_reset() — Cierra un slot y lo devuelve al estado FREE.
 *
 * Remueve de epoll, cierra el fd, y limpia el estado del slot.
 *
 * @param pool  Pool de red.
 * @param slot  Slot a resetear.
 */
void net_slot_reset(NetPool *pool, Slot *slot);

/**
 * net_slot_send() — Envía un payload SHA-256 hexadecimal al slot.
 *
 * Genera un payload de 64 bytes hex + "\n" y lo envía al fd conectado.
 * Actualiza g_metrics.total_bytes_sent.
 *
 * @param slot  Slot conectado (state == SLOT_CONNECTED).
 * @return      Bytes enviados, o -1 si error.
 */
int net_slot_send(Slot *slot);

/**
 * net_pool_tick() — Procesa eventos epoll pendientes.
 *
 * Llama a epoll_wait() con timeout corto y procesa los eventos:
 * - EPOLLOUT en CONNECTING → confirma conexión.
 * - EPOLLIN en CONNECTED → lee datos (actualiza bytes_recv).
 * - EPOLLERR/EPOLLHUP → reset del slot.
 *
 * @param pool      Pool de red.
 * @param timeout_ms Timeout de epoll_wait en ms.
 * @return          Número de eventos procesados, o -1 si error fatal.
 */
int net_pool_tick(NetPool *pool, int timeout_ms);

#endif /* NETWORK_H */


/*
 * worker.h — Pool de workers con pthread (Arquitectura MIPS)
 *
 * Gestiona los hilos de trabajo que realizan conexiones TCP concurrentes.
 * Cada worker corre su propio loop de net_pool_tick() independientemente.
 *
 * Compatible con MIPS 32-bit. Los pthread en MIPS usan las mismas APIs
 * POSIX que x86. El tamaño de pila se configura explícitamente via
 * pthread_attr_setstacksize() (WORKER_STACK_SIZE = 128 KB).
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>
#include "config.h"
#include "network.h"

/* -------------------------------------------------------------------------
 * Argumento pasado a cada worker thread
 * ------------------------------------------------------------------------- */

/**
 * Argumento que recibe cada pthread worker al arrancar.
 * Se aloca en el heap y el worker lo libera al terminar.
 */
typedef struct {
    int             worker_id;  /**< ID del worker (0-indexed).             */
    const Config   *cfg;        /**< Configuración read-only.               */
} WorkerArg;

/* -------------------------------------------------------------------------
 * Estructura del pool de workers
 * ------------------------------------------------------------------------- */

/**
 * Pool de worker threads.
 * Se crea uno por proceso; los hilos corren hasta que g_running == 0.
 */
typedef struct {
    pthread_t      *threads;    /**< Array de pthread_t[cfg->workers].      */
    int             count;      /**< Cantidad de workers activos.           */
    const Config   *cfg;        /**< Configuración de referencia.           */
} WorkerPool;

/* -------------------------------------------------------------------------
 * Variable global de control de ejecución
 * ------------------------------------------------------------------------- */

/**
 * Flag global de ejecución.
 * Cuando se pone a 0 (por SIGINT), todos los workers terminan su loop.
 * Acceso atómico via atomic_int (C11).
 */
extern volatile int g_running;

/* -------------------------------------------------------------------------
 * Funciones públicas
 * ------------------------------------------------------------------------- */

/**
 * worker_pool_start() — Crea y lanza todos los worker threads.
 *
 * Cada worker corre en un pthread con pila de WORKER_STACK_SIZE bytes.
 * Retorna inmediatamente; los workers corren en paralelo.
 *
 * @param cfg   Configuración (slots, workers, host, addr).
 * @return      Puntero al WorkerPool, o NULL si falla.
 */
WorkerPool *worker_pool_start(const Config *cfg);

/**
 * worker_pool_join() — Espera a que todos los workers terminen.
 *
 * Se llama después de poner g_running = 0.
 * Hace pthread_join() en cada hilo y libera el pool.
 *
 * @param pool  Pool de workers a unir y liberar.
 */
void worker_pool_join(WorkerPool *pool);

#endif /* WORKER_H */

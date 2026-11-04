/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: include/worker.h
 * Descripción: Gestión del pool de hilos de trabajo POSIX concurrentes,
 *              simulación de carga de CPU, asignación dinámica y ciclo de red.
 * ============================================================================
 */

#ifndef BENCHMARK_WORKER_H
#define BENCHMARK_WORKER_H

#include "config.h"
#include "telemetry.h"
#include <pthread.h>
#include <signal.h>

typedef struct {
    int                           worker_id;
    int                           slots_assigned;
    const Config                 *cfg;
    telemetry_t                  *telemetry;
    volatile sig_atomic_t        *keep_running;
} worker_context_t;

typedef struct {
    pthread_t                    *threads;
    worker_context_t             *contexts;
    int                           num_workers;
    volatile sig_atomic_t        *keep_running;
} worker_pool_t;

/**
 * Inicializa y lanza el pool de hilos de trabajo POSIX con stack reducido (128 KB).
 *
 * @param pool Estructura del pool a inicializar.
 * @param cfg Configuración del benchmark.
 * @param telemetry Estructura de telemetría para registro de métricas.
 * @param keep_running Bandera atómica de control de ciclo de vida.
 * @return 0 en éxito, -1 en error.
 */
int worker_pool_start(worker_pool_t *pool, const Config *cfg,
                      telemetry_t *telemetry, volatile sig_atomic_t *keep_running);

/**
 * Espera la finalización de todos los hilos del pool (pthread_join) y libera recursos asociados.
 */
void worker_pool_wait_and_stop(worker_pool_t *pool);

#endif /* BENCHMARK_WORKER_H */


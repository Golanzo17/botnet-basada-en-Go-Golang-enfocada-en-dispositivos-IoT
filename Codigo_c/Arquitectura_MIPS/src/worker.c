/*
 * worker.c — Pool de workers con pthread (Arquitectura MIPS)
 *
 * Cada worker es un pthread que crea su propio NetPool y corre un loop
 * de net_pool_tick() hasta que g_running == 0.
 *
 * Particularidades MIPS:
 *   - pthread_attr_setstacksize() con WORKER_STACK_SIZE (128 KB).
 *     En MIPS O32 ABI, el stack crece hacia abajo. 128 KB es suficiente
 *     para los frames de red.
 *   - g_running es volatile int (no atómico), suficiente para señal/worker.
 *   - Los workers leen g_running en cada iteración del loop.
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
#include <pthread.h>

#include "worker.h"
#include "network.h"
#include "memory.h"
#include "telemetry.h"
#include "config.h"

/* -------------------------------------------------------------------------
 * Variable global de control de ejecución
 * ------------------------------------------------------------------------- */

/** Flag de ejecución: 1 = corriendo, 0 = detener todos los workers. */
volatile int g_running = 1;

/* -------------------------------------------------------------------------
 * Función interna: loop de un worker
 * ------------------------------------------------------------------------- */

/**
 * worker_thread() — Función principal de cada worker pthread.
 *
 * Crea su propio NetPool con los slots asignados, luego corre el loop:
 *   1. Conectar slots libres.
 *   2. Llamar net_pool_tick() para procesar eventos epoll.
 *   3. Repetir hasta g_running == 0.
 */
static void *worker_thread(void *arg)
{
    WorkerArg *warg = (WorkerArg *)arg;
    const Config *cfg = warg->cfg;
    int wid = warg->worker_id;
    mem_free(warg);  /* El arg fue alocado por el pool, nosotros lo liberamos */

    atomic_fetch_add(&g_metrics.active_workers, 1);

    /* Crear pool de red para este worker */
    NetPool *pool = net_pool_create(cfg);
    if (!pool) {
        fprintf(stderr, "[WORKER MIPS] Worker %d: no pudo crear NetPool\n", wid);
        atomic_fetch_sub(&g_metrics.active_workers, 1);
        return NULL;
    }

    /* Loop principal del worker */
    while (g_running) {
        /* Fase 1: conectar slots libres */
        for (int i = 0; i < pool->num_slots && g_running; i++) {
            Slot *slot = &pool->slots[i];
            if (slot->state == SLOT_FREE) {
                if (slot->retries > 0) {
                    if (net_slot_connect(pool, slot) < 0) {
                        slot->retries--;
                    }
                } else {
                    /* Resetear retries para permitir reconexión en el futuro */
                    slot->retries = MAX_RETRIES;
                }
            }
        }

        /* Fase 2: procesar eventos epoll (100ms timeout) */
        net_pool_tick(pool, 100);
    }

    /* Limpieza: destruir el pool al salir */
    net_pool_destroy(pool);
    atomic_fetch_sub(&g_metrics.active_workers, 1);

    return NULL;
}

/* -------------------------------------------------------------------------
 * Implementación pública
 * ------------------------------------------------------------------------- */

/**
 * worker_pool_start() — Crea y lanza los worker threads.
 */
WorkerPool *worker_pool_start(const Config *cfg)
{
    WorkerPool *wpool = (WorkerPool *)mem_alloc(sizeof(WorkerPool));
    if (!wpool) return NULL;

    wpool->cfg   = cfg;
    wpool->count = cfg->workers;

    wpool->threads = (pthread_t *)mem_alloc(sizeof(pthread_t) * (size_t)cfg->workers);
    if (!wpool->threads) {
        mem_free(wpool);
        return NULL;
    }

    /* Atributos de hilo: stack size explícito para MIPS O32 */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, WORKER_STACK_SIZE);

    printf("[WORKER MIPS] Iniciando %d workers con %d slots c/u "
           "(pila: %d KB por worker)...\n",
           cfg->workers, cfg->slots, WORKER_STACK_SIZE / 1024);
    fflush(stdout);

    for (int i = 0; i < cfg->workers; i++) {
        WorkerArg *warg = (WorkerArg *)mem_alloc(sizeof(WorkerArg));
        if (!warg) {
            fprintf(stderr, "[WORKER MIPS] No pudo alocar WorkerArg para worker %d\n", i);
            wpool->count = i;
            break;
        }
        warg->worker_id = i;
        warg->cfg       = cfg;

        int ret = pthread_create(&wpool->threads[i], &attr, worker_thread, warg);
        if (ret != 0) {
            fprintf(stderr, "[WORKER MIPS] pthread_create falló para worker %d: %s\n",
                    i, strerror(ret));
            mem_free(warg);
            wpool->count = i;
            break;
        }
    }

    pthread_attr_destroy(&attr);
    return wpool;
}

/**
 * worker_pool_join() — Espera que todos los workers terminen y libera el pool.
 */
void worker_pool_join(WorkerPool *pool)
{
    if (!pool) return;

    for (int i = 0; i < pool->count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    mem_free(pool->threads);
    mem_free(pool);
}

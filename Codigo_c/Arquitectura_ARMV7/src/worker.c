/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura ARMV7 / 32-bit ARM)
 * Archivo: src/worker.c
 * Descripción: Implementación del worker concurrente, gestión del pool de hilos
 *              e instrumentación atómica de Unidades Concurrentes de Ataque (UCA).
 * ============================================================================
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "worker.h"
#include "network.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

static void *worker_thread_func(void *arg) {
    worker_context_t *ctx = (worker_context_t *)arg;
    int id = ctx->worker_id;
    int slots = ctx->slots_assigned;
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

    while (*keep_running) {
        // Instrumentación de Métrica 3 (UCA): inicio de unidades de trabajo concurrentes activas
        long cur_uca = atomic_fetch_add(&telemetry->uca_active, slots) + slots;
        telemetry_update_uca_max(telemetry, cur_uca);

        // A. ASIGNACIÓN Y LIBERACIÓN DINÁMICA MANUAL
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

        // B. CÓMPUTO INTENSIVO DE CPU (SHA-256)
        for (int k = 0; k < cfg->cpu_loop_work; k++) {
            sha256_compute((const uint8_t *)payload_str, (size_t)payload_len, digest);
        }
        iter++;

        // C. SELECCIÓN DE OBJETIVO (exclusivamente servidor mock local por defecto)
        int is_unreachable = (cfg->unreachable_ratio > 0) &&
                             ((rand_r(&seed) % 100) < cfg->unreachable_ratio);
        const char *target_ip = cfg->target_host;
        int target_port = cfg->base_port;

        if (is_unreachable) {
            target_ip = cfg->unreachable_host;
            target_port = cfg->unreachable_port;
            atomic_fetch_add(&telemetry->syn_sent_dials, 1);
        }

        // D. INTENTO DE CONEXIÓN TCP NO BLOQUEANTE
        network_dial_tcp_timeout(target_ip, target_port, cfg->dial_timeout_ms,
                                 (const uint8_t *)payload_str, (size_t)payload_len,
                                 telemetry);

        // Fin del ciclo activo de estas unidades concurrentes antes de la pausa
        atomic_fetch_sub(&telemetry->uca_active, slots);

        // E. PAUSA DE CICLO
        if (cfg->worker_delay_us > 0) {
            usleep((useconds_t)cfg->worker_delay_us);
        }
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

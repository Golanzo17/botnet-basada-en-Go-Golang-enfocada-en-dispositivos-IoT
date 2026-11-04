/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: src/worker.c
 * Descripción: Implementación del worker concurrente y gestión del pool de hilos.
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

    // Registrar slots asignados al contador de concurrencia
    atomic_fetch_add(&telemetry->active_slots, slots);

    // Semilla thread-safe para rand_r
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
        // A. ASIGNACIÓN Y LIBERACIÓN DINÁMICA MANUAL (Contraste con el GC de Go)
        // Se reserva un buffer dinámico de tamaño variable (4KB a 8KB) y se libera
        // inmediatamente tras su cómputo, devolviendo la memoria directamente al allocator
        // sin provocar acumulación ni pausas de recolección de basura.
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

        // B. CÓMPUTO INTENSIVO DE CPU (SHA-256 idéntico al cliente Go)
        for (int k = 0; k < cfg->cpu_loop_work; k++) {
            sha256_compute((const uint8_t *)payload_str, (size_t)payload_len, digest);
        }
        iter++;

        // C. SELECCIÓN DE OBJETIVO CON ROTACIÓN DE IP
        int is_unreachable = (rand_r(&seed) % 100) < cfg->unreachable_ratio;
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

        // E. PAUSA DE CICLO
        if (cfg->worker_delay_us > 0) {
            usleep((useconds_t)cfg->worker_delay_us);
        }
    }

    // Decrementar slots al finalizar el hilo
    atomic_fetch_sub(&telemetry->active_slots, slots);
    return NULL;
}

int worker_pool_start(worker_pool_t *pool, const Config *cfg,
                      telemetry_t *telemetry, volatile sig_atomic_t *keep_running) {
    if (!pool || !cfg || !telemetry || !keep_running) return -1;

    pool->num_workers = cfg->num_workers;
    pool->keep_running = keep_running;

    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)pool->num_workers);
    if (!pool->threads) {
        perror("malloc worker threads array");
        return -1;
    }

    pool->contexts = (worker_context_t *)malloc(sizeof(worker_context_t) * (size_t)pool->num_workers);
    if (!pool->contexts) {
        perror("malloc worker contexts array");
        free(pool->threads);
        pool->threads = NULL;
        return -1;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Configurar el stack por hilo en 128 KB (en lugar de 8 MB por defecto en Linux x86-64)
    // práctica estándar en clientes de alta concurrencia y sistemas embebidos en C
    pthread_attr_setstacksize(&attr, 128 * 1024);

    int base_slots = cfg->simulated_slots / cfg->num_workers;
    int remainder  = cfg->simulated_slots % cfg->num_workers;

    printf("[+] Desplegando %d slots sobre %d workers POSIX (stack 128 KB)...\n",
           cfg->simulated_slots, cfg->num_workers);

    for (int i = 0; i < pool->num_workers; i++) {
        pool->contexts[i].worker_id      = i;
        pool->contexts[i].slots_assigned = base_slots + (i < remainder ? 1 : 0);
        pool->contexts[i].cfg            = cfg;
        pool->contexts[i].telemetry      = telemetry;
        pool->contexts[i].keep_running   = keep_running;

        if (pthread_create(&pool->threads[i], &attr, worker_thread_func, &pool->contexts[i]) != 0) {
            perror("pthread_create worker");
            // Si falla la creación de algún hilo, se ajusta num_workers a los creados exitosamente
            pool->num_workers = i;
            break;
        }
    }

    pthread_attr_destroy(&attr);
    return 0;
}

void worker_pool_wait_and_stop(worker_pool_t *pool) {
    if (!pool) return;

    for (int i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    if (pool->threads) {
        free(pool->threads);
        pool->threads = NULL;
    }

    if (pool->contexts) {
        free(pool->contexts);
        pool->contexts = NULL;
    }
}

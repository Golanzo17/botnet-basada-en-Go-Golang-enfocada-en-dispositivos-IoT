/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: src/telemetry.c
 * Descripción: Implementación de la recolección y emisión de métricas de telemetría.
 * ============================================================================
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "telemetry.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

void telemetry_init(telemetry_t *t) {
    if (!t) return;
    atomic_init(&t->active_slots, 0);
    atomic_init(&t->active_sockets, 0);
    atomic_init(&t->successful_dials, 0);
    atomic_init(&t->failed_dials, 0);
    atomic_init(&t->syn_sent_dials, 0);
    atomic_init(&t->bytes_transferred, 0);
    atomic_init(&t->manual_frees, 0);
}

double telemetry_get_process_rss_mb(void) {
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) {
        return 0.0;
    }

    long total_pages = 0;
    long rss_pages = 0;
    if (fscanf(fp, "%ld %ld", &total_pages, &rss_pages) != 2) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);

    long page_size_bytes = sysconf(_SC_PAGESIZE);
    if (page_size_bytes <= 0) {
        page_size_bytes = 4096; // fallback típico en arquitectura x86-64
    }

    double rss_bytes = (double)rss_pages * (double)page_size_bytes;
    return rss_bytes / (1024.0 * 1024.0);
}

void *telemetry_reporter_thread(void *arg) {
    telemetry_reporter_args_t *args = (telemetry_reporter_args_t *)arg;
    telemetry_t *metrics = args->metrics;
    volatile sig_atomic_t *running = args->keep_running;

    struct timespec sleep_interval;
    sleep_interval.tv_sec = 0;
    sleep_interval.tv_nsec = 250 * 1000 * 1000; // 250 ms (4 muestras por segundo)

    while (*running) {
        nanosleep(&sleep_interval, NULL);
        if (!*running) break;

        // Lectura e intercambio atómico a cero para calcular tasas por segundo
        long dials_ok  = atomic_exchange(&metrics->successful_dials, 0);
        long dials_err = atomic_exchange(&metrics->failed_dials, 0);
        long dials_syn = atomic_exchange(&metrics->syn_sent_dials, 0);
        long bytes_tx  = atomic_exchange(&metrics->bytes_transferred, 0);
        long frees_cnt = atomic_exchange(&metrics->manual_frees, 0);

        long active_slots = atomic_load(&metrics->active_slots);
        double rss_mb     = telemetry_get_process_rss_mb();

        // Formato homólogo a [GO METRICS] para facilitar análisis empírico
        printf("\r[C METRICS]  Slots: %ld | RAM (RSS): %.2f MB | Frees/s: %ld | Dials/s: %ld | Timeouts/s: %ld | Traffic: %.1f KB/s   ",
               active_slots,
               rss_mb,
               frees_cnt * 4,
               dials_ok * 4,
               (dials_err + dials_syn) * 4,
               ((double)bytes_tx / 1024.0) * 4.0);
        fflush(stdout);
    }

    return NULL;
}

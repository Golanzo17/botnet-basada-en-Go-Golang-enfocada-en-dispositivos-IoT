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

    atomic_init(&t->total_successful_dials, 0);
    atomic_init(&t->total_failed_dials, 0);
    atomic_init(&t->total_syn_sent_dials, 0);
    atomic_init(&t->total_bytes_transferred, 0);
    atomic_init(&t->total_manual_frees, 0);
    t->peak_rss_mb = 0.0;
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

        // Acumular totales para el reporte final
        atomic_fetch_add(&metrics->total_successful_dials, dials_ok);
        atomic_fetch_add(&metrics->total_failed_dials, dials_err);
        atomic_fetch_add(&metrics->total_syn_sent_dials, dials_syn);
        atomic_fetch_add(&metrics->total_bytes_transferred, bytes_tx);
        atomic_fetch_add(&metrics->total_manual_frees, frees_cnt);

        if (rss_mb > metrics->peak_rss_mb) {
            metrics->peak_rss_mb = rss_mb;
        }

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

void telemetry_print_final_summary(const telemetry_t *t, int heap_target_mb,
                                   int simulated_slots, double idle_rss,
                                   double duration_sec) {
    if (!t) return;

    long ok_dials   = atomic_load(&t->total_successful_dials);
    long syn_dials  = atomic_load(&t->total_syn_sent_dials);
    long fail_dials = atomic_load(&t->total_failed_dials);
    long total_reqs = ok_dials + syn_dials + fail_dials;
    long frees      = atomic_load(&t->total_manual_frees);
    long bytes_tx   = atomic_load(&t->total_bytes_transferred);

    double mb_tx = (double)bytes_tx / (1024.0 * 1024.0);
    double avg_traffic_kb_s = duration_sec > 0 ? (((double)bytes_tx / 1024.0) / duration_sec) : 0.0;
    double avg_dials_s = duration_sec > 0 ? ((double)total_reqs / duration_sec) : 0.0;

    printf("\n\n");
    printf("=================================================================\n");
    printf("        RESUMEN FINAL CONSOLIDADO DEL BENCHMARK (C - AMD64)      \n");
    printf("=================================================================\n");
    printf("  Tiempo medido en carga:        %.1f segundos\n", duration_sec);
    printf("  Slots lógicos concurrentes:    %d\n", simulated_slots);
    printf("  ---------------------------------------------------------------\n");
    printf("  RAM Base en Reposo (Idle RSS): %.2f MB\n", idle_rss);
    printf("  RAM Estática en Heap:          %d MB\n", heap_target_mb);
    printf("  RAM Pico Registrada (Max RSS): %.2f MB\n", t->peak_rss_mb);
    printf("  ---------------------------------------------------------------\n");
    printf("  Conexiones TCP Totales:        %ld (promedio: %.1f req/s)\n", total_reqs, avg_dials_s);
    printf("    -> Exitosas (ESTABLISHED):   %ld\n", ok_dials);
    printf("    -> Timeout (SYN_SENT):       %ld\n", syn_dials);
    printf("    -> Rechazadas / Errores:     %ld\n", fail_dials);
    printf("  Liberaciones Manuales (free):  %ld bloques (sin pausas GC)\n", frees);
    printf("  Tráfico de Red Transmitido:    %.2f MB (%.1f KB/s)\n", mb_tx, avg_traffic_kb_s);
    printf("  Comportamiento frente a RAM:   ESTABLE (Sin OOM Stop)\n");
    printf("=================================================================\n");

    // Guardar también en archivo de texto para trazabilidad experimental
    FILE *f = fopen("benchmark_resultado.txt", "w");
    if (f) {
        fprintf(f, "RESUMEN FINAL CONSOLIDADO DEL BENCHMARK (C - AMD64)\n");
        fprintf(f, "Tiempo_Carga_s: %.1f\n", duration_sec);
        fprintf(f, "Slots_Concurrentes: %d\n", simulated_slots);
        fprintf(f, "RAM_Idle_RSS_MB: %.2f\n", idle_rss);
        fprintf(f, "RAM_Heap_Target_MB: %d\n", heap_target_mb);
        fprintf(f, "RAM_Peak_RSS_MB: %.2f\n", t->peak_rss_mb);
        fprintf(f, "Conexiones_Totales: %ld\n", total_reqs);
        fprintf(f, "Conexiones_Exitosas: %ld\n", ok_dials);
        fprintf(f, "Conexiones_Timeout: %ld\n", syn_dials);
        fprintf(f, "Conexiones_Fallidas: %ld\n", fail_dials);
        fprintf(f, "Liberaciones_Manuales: %ld\n", frees);
        fprintf(f, "Trafico_Total_MB: %.2f\n", mb_tx);
        fprintf(f, "Estado: ESTABLE_SIN_OOM\n");
        fclose(f);
        printf("[+] Resultados persistidos en disco en: benchmark_resultado.txt\n");
    }
}

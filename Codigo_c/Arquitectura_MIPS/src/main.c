/*
 * main.c — Punto de entrada del cliente C (Arquitectura MIPS)
 *
 * Orquesta el ciclo de vida completo del benchmark:
 *
 *   Fase 0 — Parseo de config y resolución DNS.
 *   Fase 1 — Idle: mide RAM base del proceso.
 *   Fase 2 — Heap estático: aloca memoria estática retenida (homólogo a AMD64/ARMv7).
 *   Fase 3 — Workers + Telemetría CSV: lanza los workers y registra las 3 métricas
 *             (RAM, Red y UCA) cada segundo en el archivo CSV.
 *
 * Compatible con MIPS 32-bit Linux.
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
#include <signal.h>
#include <time.h>
#include <stdatomic.h>

#include "config.h"
#include "telemetry.h"
#include "memory.h"
#include "worker.h"

static struct timespec g_start_time;

static void sigint_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char *argv[])
{
    Config cfg;
    if (config_parse(&cfg, argc, argv) != 0) {
        fprintf(stderr, "Uso: %s [idle_sec] [heap_mb] [total_uca] [host] [port]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    config_print(&cfg);

    telemetry_init();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* ----------------------------------------------------------------
     * Fase 1: Idle — medición de RAM base
     * ---------------------------------------------------------------- */
    if (cfg.idle_sec > 0) {
        printf("[*] FASE 1 (REPOSO / IDLE): Midiendo huella base en MIPS durante %d s...\n",
               cfg.idle_sec);
        fflush(stdout);

        for (int i = cfg.idle_sec; i > 0 && g_running; i--) {
            double rss = mem_get_rss_mb();
            printf("\r%s Tiempo restante: %d s | RAM Física (RSS): %.2f MB   ",
                   IDLE_PREFIX, i, rss);
            fflush(stdout);
            sleep(1);
        }
        printf("\n");
    }

    if (!g_running) {
        config_free(&cfg);
        return EXIT_SUCCESS;
    }

    /* ----------------------------------------------------------------
     * Fase 2: Heap estático retenido (homólogo a AMD64/ARMv7)
     * ---------------------------------------------------------------- */
    size_t static_bytes = (cfg.heap_mb > 0)
                          ? ((size_t)cfg.heap_mb * 1024U * 1024U)
                          : (size_t)BUFFER_SIZE;
    void *static_buf = mem_alloc(static_bytes);
    if (static_buf) {
        memset(static_buf, 0xAB, static_bytes);
        double rss_after = mem_get_rss_mb();
        printf("[+] Memoria estática retenida en Heap: %d MB (RSS actual: %.2f MB)\n",
               cfg.heap_mb, rss_after);
        fflush(stdout);
    }

    if (!g_running) {
        mem_free(static_buf);
        config_free(&cfg);
        return EXIT_SUCCESS;
    }

    /* ----------------------------------------------------------------
     * Fase 3: Workers + Telemetría CSV (RAM, Red y UCA)
     * ---------------------------------------------------------------- */
    const char *csv_path = getenv("CSV_PATH");
    const char *arch_env = getenv("BENCHMARK_ARCH");
    const char *mem_env  = getenv("BENCHMARK_MEM_MB");
    const char *dur_env  = getenv("BENCHMARK_DURATION");

    int mem_limit_mb = (mem_env && atoi(mem_env) > 0) ? atoi(mem_env) : 0;
    int max_duration = (dur_env && atoi(dur_env) > 0) ? atoi(dur_env) : 0;

    telemetry_setup_csv(csv_path,
                        (arch_env && arch_env[0] != '\0') ? arch_env : "MIPS",
                        mem_limit_mb);

    clock_gettime(CLOCK_MONOTONIC, &g_start_time);

    WorkerPool *wpool = worker_pool_start(&cfg);
    if (!wpool) {
        fprintf(stderr, "[MIPS] ERROR: No se pudo crear el pool de workers.\n");
        telemetry_close_csv();
        mem_free(static_buf);
        config_free(&cfg);
        return EXIT_FAILURE;
    }

    long prev_dials = 0;
    long prev_frees = 0;
    long prev_bytes = 0;
    int  elapsed_ticks = 0;

    while (g_running) {
        struct timespec period_start;
        clock_gettime(CLOCK_MONOTONIC, &period_start);

        for (int i = 0; i < TELEMETRY_INTERVAL && g_running; i++) {
            sleep(1);
        }

        if (!g_running) break;

        struct timespec period_end;
        clock_gettime(CLOCK_MONOTONIC, &period_end);
        double elapsed_period = (double)(period_end.tv_sec  - period_start.tv_sec)
                              + (double)(period_end.tv_nsec - period_start.tv_nsec) / 1e9;
        double total_elapsed  = (double)(period_end.tv_sec  - g_start_time.tv_sec)
                              + (double)(period_end.tv_nsec - g_start_time.tv_nsec) / 1e9;

        TelemetrySnapshot snap;
        telemetry_snapshot(&snap, prev_dials, prev_frees, prev_bytes,
                           elapsed_period, total_elapsed);
        telemetry_print(&snap, total_elapsed);

        prev_dials = atomic_load(&g_metrics.total_dials);
        prev_frees = atomic_load(&g_metrics.total_frees);
        long bs = atomic_load(&g_metrics.total_bytes_sent);
        long br = atomic_load(&g_metrics.total_bytes_recv);
        prev_bytes = bs + br;

        elapsed_ticks += TELEMETRY_INTERVAL;
        if (max_duration > 0 && elapsed_ticks >= max_duration) {
            g_running = 0;
            break;
        }
    }

    printf("\n[!] Finalizando test de MIPS y cerrando registros...\n");
    fflush(stdout);

    worker_pool_join(wpool);
    telemetry_close_csv();

    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_elapsed = (double)(end_time.tv_sec  - g_start_time.tv_sec)
                         + (double)(end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;

    telemetry_print_summary(cfg.slots, cfg.workers, total_elapsed);

    mem_free(static_buf);
    config_free(&cfg);

    printf("[+] Prueba de MIPS finalizada limpiamente.\n");
    fflush(stdout);

    return EXIT_SUCCESS;
}

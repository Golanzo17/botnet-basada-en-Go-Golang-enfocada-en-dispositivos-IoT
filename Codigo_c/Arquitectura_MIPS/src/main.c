/*
 * main.c — Punto de entrada del cliente C (Arquitectura MIPS)
 *
 * Orquesta el ciclo de vida completo del benchmark:
 *
 *   Fase 0 — Parseo de config y resolución DNS.
 *   Fase 1 — Idle: mide RAM base del proceso (sin workers, sin conexiones).
 *   Fase 2 — Heap estático: aloca buffer de BUFFER_SIZE para medir overhead.
 *   Fase 3 — Workers + Telemetría: lanza los workers y reporta métricas
 *             cada TELEMETRY_INTERVAL segundos.
 *   Finalización — SIGINT (Ctrl+C): join de workers, resumen + guardado de
 *                  benchmark_resultado.txt en host/ (volumen Docker).
 *
 * Compatible con MIPS 32-bit Linux.
 * El handler de SIGINT es async-signal-safe (solo escribe g_running = 0).
 * Las operaciones de telemetría y guardado ocurren en el hilo main después
 * del join, fuera del handler de señal.
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

/* -------------------------------------------------------------------------
 * Variables globales del main
 * ------------------------------------------------------------------------- */

/** Timestamp de inicio de la fase activa (workers corriendo). */
static struct timespec g_start_time;

/* -------------------------------------------------------------------------
 * Manejador de señales
 * ------------------------------------------------------------------------- */

/**
 * sigint_handler() — Captura Ctrl+C y detiene el loop principal.
 *
 * Solo pone g_running = 0. Async-signal-safe en MIPS Linux.
 * El procesamiento real (telemetría, guardado) ocurre en main() tras el join.
 */
static void sigint_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* -------------------------------------------------------------------------
 * Función principal
 * ------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    /* ----------------------------------------------------------------
     * Fase 0: Parseo de configuración
     * ---------------------------------------------------------------- */

    Config cfg;
    if (config_parse(&cfg, argc, argv) != 0) {
        fprintf(stderr, "Uso: %s [slots] [workers] [stack_kb] [host] [port]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    config_print(&cfg);

    /* Inicializar telemetría */
    telemetry_init();

    /* Registrar manejador SIGINT */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* ----------------------------------------------------------------
     * Fase 1: Idle — medición de RAM base
     * ---------------------------------------------------------------- */

    printf("\n[MIPS IDLE] Fase idle: midiendo RAM base (%d s)...\n",
           IDLE_DURATION);
    fflush(stdout);

    for (int i = 0; i < IDLE_DURATION && g_running; i++) {
        double rss = mem_get_rss_mb();
        printf("%s RAM base: %.2f MB (idle %d/%d)\n",
               IDLE_PREFIX, rss, i + 1, IDLE_DURATION);
        fflush(stdout);
        sleep(1);
    }

    if (!g_running) goto cleanup;

    /* ----------------------------------------------------------------
     * Fase 2: Heap estático — aloca buffer de red
     * ---------------------------------------------------------------- */

    printf("\n[MIPS IDLE] Fase heap estático: alocando buffer %d bytes...\n",
           BUFFER_SIZE);
    fflush(stdout);

    void *static_buf = mem_alloc(BUFFER_SIZE);
    if (static_buf) {
        memset(static_buf, 0xAB, BUFFER_SIZE);  /* evitar lazy allocation */
        double rss_after = mem_get_rss_mb();
        printf("%s RAM tras buffer estático: %.2f MB\n", IDLE_PREFIX, rss_after);
        fflush(stdout);
    }

    if (!g_running) {
        mem_free(static_buf);
        goto cleanup;
    }

    /* ----------------------------------------------------------------
     * Fase 3: Workers + Telemetría
     * ---------------------------------------------------------------- */

    printf("\n[MIPS] Iniciando workers y telemetría...\n");
    printf("[MIPS] Presiona Ctrl+C para finalizar y guardar resultados.\n\n");
    fflush(stdout);

    /* Registrar timestamp de inicio */
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);

    /* Lanzar workers */
    WorkerPool *wpool = worker_pool_start(&cfg);
    if (!wpool) {
        fprintf(stderr, "[MIPS] ERROR: No se pudo crear el pool de workers.\n");
        mem_free(static_buf);
        config_free(&cfg);
        return EXIT_FAILURE;
    }

    /* Loop de telemetría */
    long prev_dials = 0;
    long prev_frees = 0;
    long prev_bytes = 0;

    while (g_running) {
        struct timespec period_start;
        clock_gettime(CLOCK_MONOTONIC, &period_start);

        /* Esperar TELEMETRY_INTERVAL segundos (con comprobación de g_running) */
        for (int i = 0; i < TELEMETRY_INTERVAL && g_running; i++) {
            sleep(1);
        }

        if (!g_running) break;

        /* Calcular tiempo transcurrido en el período */
        struct timespec period_end;
        clock_gettime(CLOCK_MONOTONIC, &period_end);
        double elapsed_period = (double)(period_end.tv_sec  - period_start.tv_sec)
                              + (double)(period_end.tv_nsec - period_start.tv_nsec) / 1e9;

        /* Snapshot y print */
        TelemetrySnapshot snap;
        telemetry_snapshot(&snap, prev_dials, prev_frees, prev_bytes, elapsed_period);
        telemetry_print(&snap);

        /* Actualizar valores previos */
        prev_dials = atomic_load(&g_metrics.total_dials);
        prev_frees = atomic_load(&g_metrics.total_frees);
        long bs = atomic_load(&g_metrics.total_bytes_sent);
        long br = atomic_load(&g_metrics.total_bytes_recv);
        prev_bytes = bs + br;
    }

    /* ----------------------------------------------------------------
     * Finalización: join workers + resumen + guardado
     * ---------------------------------------------------------------- */

    printf("\n[MIPS] Deteniendo workers...\n");
    fflush(stdout);

    worker_pool_join(wpool);

    /* Calcular duración total */
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double total_elapsed = (double)(end_time.tv_sec  - g_start_time.tv_sec)
                         + (double)(end_time.tv_nsec - g_start_time.tv_nsec) / 1e9;

    /* Imprimir tabla resumen */
    telemetry_print_summary(cfg.slots, cfg.workers, total_elapsed);

    /* Guardar archivo de resultados */
    telemetry_save_results(cfg.slots, cfg.workers, total_elapsed);

    /* Liberar buffer estático */
    mem_free(static_buf);

cleanup:
    config_free(&cfg);

    printf("[MIPS] Fin de ejecución. ¡Hasta luego!\n");
    fflush(stdout);

    return EXIT_SUCCESS;
}


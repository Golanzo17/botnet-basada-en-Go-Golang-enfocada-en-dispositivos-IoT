/*
 * telemetry.c — Recolección de métricas y benchmark (Arquitectura MIPS)
 *
 * Implementa:
 *   - telemetry_init(): inicializa métricas a cero.
 *   - telemetry_snapshot(): calcula tasas del período y acumula sumadores.
 *   - telemetry_print(): imprime línea de métricas con prefijo [MIPS METRICS].
 *   - telemetry_print_summary(): tabla resumen al finalizar.
 *   - telemetry_save_results(): guarda benchmark_resultado.txt.
 *
 * Al finalizar con Ctrl+C, el manejador SIGINT llama a estas funciones
 * para garantizar que el archivo se guarde siempre.
 *
 * Compatible con MIPS 32-bit. Los atómicos C11 se implementan en MIPS
 * con instrucciones LL/SC del ISA. Se enlaza con -latomic para el soporte
 * de operaciones de 64-bit sobre aritmética de 32-bit.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "telemetry.h"
#include "memory.h"
#include "config.h"

/* -------------------------------------------------------------------------
 * Definición de la variable global de métricas
 * ------------------------------------------------------------------------- */

/** Instancia global de métricas, compartida entre todos los módulos. */
Metrics g_metrics;

/* -------------------------------------------------------------------------
 * Implementación pública
 * ------------------------------------------------------------------------- */

/**
 * telemetry_init() — Pone todos los campos atómicos a cero.
 */
void telemetry_init(void)
{
    atomic_store(&g_metrics.total_dials,      0L);
    atomic_store(&g_metrics.total_timeouts,   0L);
    atomic_store(&g_metrics.total_bytes_sent, 0L);
    atomic_store(&g_metrics.total_bytes_recv, 0L);
    atomic_store(&g_metrics.total_frees,      0L);
    atomic_store(&g_metrics.active_slots,     0);
    atomic_store(&g_metrics.active_workers,   0);
    atomic_store(&g_metrics.sum_rss_mb,       0L);
    atomic_store(&g_metrics.sum_dials_per_s,  0L);
    atomic_store(&g_metrics.sum_frees_per_s,  0L);
    atomic_store(&g_metrics.sum_traffic_kbps, 0L);
    atomic_store(&g_metrics.sample_count,     0L);
    atomic_store(&g_metrics.peak_rss_mb,      0L);
}

/**
 * telemetry_snapshot() — Calcula las métricas del período actual.
 *
 * Calcula tasas por segundo respecto a los valores del período anterior.
 * Acumula en sumadores para el resumen final con promedios correctos.
 * Actualiza el pico de RSS si el actual es mayor.
 */
void telemetry_snapshot(TelemetrySnapshot *snap,
                        long prev_dials, long prev_frees,
                        long prev_bytes, double elapsed_s)
{
    /* Leer contadores actuales */
    long cur_dials = atomic_load(&g_metrics.total_dials);
    long cur_frees = atomic_load(&g_metrics.total_frees);
    long cur_bytes_sent = atomic_load(&g_metrics.total_bytes_sent);
    long cur_bytes_recv = atomic_load(&g_metrics.total_bytes_recv);
    long cur_bytes = cur_bytes_sent + cur_bytes_recv;
    int  cur_slots = atomic_load(&g_metrics.active_slots);

    /* Calcular deltas del período */
    double dt = (elapsed_s > 0.001) ? elapsed_s : 1.0;

    snap->dials_per_s  = (double)(cur_dials - prev_dials)  / dt;
    snap->frees_per_s  = (double)(cur_frees - prev_frees)  / dt;
    snap->traffic_kbps = (double)(cur_bytes - prev_bytes)  / dt / 1024.0;
    snap->rss_mb       = mem_get_rss_mb();
    snap->slots        = cur_slots;

    /* Acumular en sumadores para promedio final */
    atomic_fetch_add(&g_metrics.sum_rss_mb,       (long)(snap->rss_mb * 100));
    atomic_fetch_add(&g_metrics.sum_dials_per_s,  (long)(snap->dials_per_s * 100));
    atomic_fetch_add(&g_metrics.sum_frees_per_s,  (long)(snap->frees_per_s * 100));
    atomic_fetch_add(&g_metrics.sum_traffic_kbps, (long)(snap->traffic_kbps * 100));
    atomic_fetch_add(&g_metrics.sample_count,     1L);

    /* Actualizar pico de RSS */
    long rss_scaled = (long)(snap->rss_mb * 100);
    long cur_peak   = atomic_load(&g_metrics.peak_rss_mb);
    while (rss_scaled > cur_peak) {
        if (atomic_compare_exchange_weak(&g_metrics.peak_rss_mb,
                                         &cur_peak, rss_scaled)) {
            break;
        }
    }
}

/**
 * telemetry_print() — Imprime una línea de métricas al stdout.
 *
 * Formato estándar para parsing posterior:
 * [MIPS METRICS] Slots: X | RAM (RSS): X.XX MB | Frees/s: X | Dials/s: X | Timeouts/s: X | Traffic: X.XX KB/s
 */
void telemetry_print(const TelemetrySnapshot *snap)
{
    long cur_timeouts = atomic_load(&g_metrics.total_timeouts);
    long prev_timeouts = cur_timeouts; /* simplificado: timeouts totales */

    printf("%s Slots: %d | RAM (RSS): %.2f MB | Frees/s: %.0f | "
           "Dials/s: %.0f | Timeouts/s: %ld | Traffic: %.2f KB/s\n",
           ARCH_PREFIX,
           snap->slots,
           snap->rss_mb,
           snap->frees_per_s,
           snap->dials_per_s,
           prev_timeouts,
           snap->traffic_kbps);
    fflush(stdout);
}

/**
 * telemetry_print_summary() — Imprime la tabla resumen completa.
 *
 * Calcula promedios a partir de los sumadores acumulados.
 * Se llama desde el manejador SIGINT antes de guardar el archivo.
 */
void telemetry_print_summary(int cfg_slots, int cfg_workers, double elapsed_s)
{
    long n = atomic_load(&g_metrics.sample_count);
    if (n == 0) n = 1;

    double avg_rss        = (double)atomic_load(&g_metrics.sum_rss_mb)       / (double)n / 100.0;
    double avg_dials_s    = (double)atomic_load(&g_metrics.sum_dials_per_s)   / (double)n / 100.0;
    double avg_frees_s    = (double)atomic_load(&g_metrics.sum_frees_per_s)   / (double)n / 100.0;
    double avg_traffic    = (double)atomic_load(&g_metrics.sum_traffic_kbps)  / (double)n / 100.0;
    double peak_rss       = (double)atomic_load(&g_metrics.peak_rss_mb)       / 100.0;

    long total_dials      = atomic_load(&g_metrics.total_dials);
    long total_timeouts   = atomic_load(&g_metrics.total_timeouts);
    long total_bytes_sent = atomic_load(&g_metrics.total_bytes_sent);
    long total_bytes_recv = atomic_load(&g_metrics.total_bytes_recv);
    long total_frees      = atomic_load(&g_metrics.total_frees);

    fprintf(stderr, "\n");
    fprintf(stderr, "╔══════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║         %s         ║\n", SUMMARY_HEADER);
    fprintf(stderr, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(stderr, "║  Arquitectura      : MIPS 32-bit (Big-Endian)               ║\n");
    fprintf(stderr, "║  Slots config.     : %-4d                                   ║\n", cfg_slots);
    fprintf(stderr, "║  Workers config.   : %-4d                                   ║\n", cfg_workers);
    fprintf(stderr, "║  Duración total    : %.1f s                                 ║\n", elapsed_s);
    fprintf(stderr, "║  Muestras telemetría: %-4ld                                  ║\n", n);
    fprintf(stderr, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(stderr, "║  MÉTRICAS PROMEDIO (durante fase activa)                    ║\n");
    fprintf(stderr, "║  RAM (RSS) prom.   : %.2f MB                               ║\n", avg_rss);
    fprintf(stderr, "║  RAM (RSS) pico    : %.2f MB                               ║\n", peak_rss);
    fprintf(stderr, "║  Dials/s prom.     : %.1f                                  ║\n", avg_dials_s);
    fprintf(stderr, "║  Frees/s prom.     : %.1f                                  ║\n", avg_frees_s);
    fprintf(stderr, "║  Tráfico prom.     : %.2f KB/s                             ║\n", avg_traffic);
    fprintf(stderr, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(stderr, "║  TOTALES ACUMULADOS                                         ║\n");
    fprintf(stderr, "║  Total dials       : %ld                                    ║\n", total_dials);
    fprintf(stderr, "║  Total timeouts    : %ld                                    ║\n", total_timeouts);
    fprintf(stderr, "║  Bytes enviados    : %ld                                    ║\n", total_bytes_sent);
    fprintf(stderr, "║  Bytes recibidos   : %ld                                    ║\n", total_bytes_recv);
    fprintf(stderr, "║  Total free()s     : %ld                                    ║\n", total_frees);
    fprintf(stderr, "╚══════════════════════════════════════════════════════════════╝\n");
    fflush(stderr);
}

/**
 * telemetry_save_results() — Guarda benchmark_resultado.txt.
 *
 * Intenta 3 rutas en orden de prioridad:
 *   1. host/benchmark_resultado.txt  → volumen Docker montado con -v
 *   2. out/benchmark_resultado.txt   → directorio de salida alternativo
 *   3. benchmark_resultado.txt       → directorio actual (fallback)
 *
 * El archivo incluye timestamp, configuración, promedios y totales.
 */
void telemetry_save_results(int cfg_slots, int cfg_workers, double elapsed_s)
{
    static const char *paths[] = {
        "host/benchmark_resultado.txt",
        "out/benchmark_resultado.txt",
        "benchmark_resultado.txt",
        NULL
    };

    FILE *f = NULL;
    const char *saved_path = NULL;

    /* Crear directorio host/ si no existe (para el volumen Docker) */
    mkdir("host", 0755);
    mkdir("out",  0755);

    for (int i = 0; paths[i]; i++) {
        f = fopen(paths[i], "w");
        if (f) {
            saved_path = paths[i];
            break;
        }
    }

    if (!f) {
        fprintf(stderr, "[TELEMETRY MIPS] ERROR: No se pudo guardar benchmark_resultado.txt\n");
        return;
    }

    /* ---- Cabecera ---- */
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", gmtime(&now));

    long n = atomic_load(&g_metrics.sample_count);
    if (n == 0) n = 1;

    double avg_rss        = (double)atomic_load(&g_metrics.sum_rss_mb)       / (double)n / 100.0;
    double avg_dials_s    = (double)atomic_load(&g_metrics.sum_dials_per_s)   / (double)n / 100.0;
    double avg_frees_s    = (double)atomic_load(&g_metrics.sum_frees_per_s)   / (double)n / 100.0;
    double avg_traffic    = (double)atomic_load(&g_metrics.sum_traffic_kbps)  / (double)n / 100.0;
    double peak_rss       = (double)atomic_load(&g_metrics.peak_rss_mb)       / 100.0;

    long total_dials      = atomic_load(&g_metrics.total_dials);
    long total_timeouts   = atomic_load(&g_metrics.total_timeouts);
    long total_bytes_sent = atomic_load(&g_metrics.total_bytes_sent);
    long total_bytes_recv = atomic_load(&g_metrics.total_bytes_recv);
    long total_frees      = atomic_load(&g_metrics.total_frees);

    fprintf(f, "====================================================================\n");
    fprintf(f, "%s\n", SUMMARY_HEADER);
    fprintf(f, "====================================================================\n");
    fprintf(f, "Fecha/Hora          : %s\n", time_str);
    fprintf(f, "Arquitectura        : %s\n", ARCH_NAME);
    fprintf(f, "Implementación      : C11 (cliente botnet simulado — sin ataque)\n");
    fprintf(f, "Compilador          : mips-linux-gnu-gcc (cross-compile)\n");
    fprintf(f, "\n");
    fprintf(f, "--- CONFIGURACIÓN ---\n");
    fprintf(f, "Slots de conexión   : %d\n", cfg_slots);
    fprintf(f, "Workers (hilos)     : %d\n", cfg_workers);
    fprintf(f, "Intervalo telemetría: %d s\n", TELEMETRY_INTERVAL);
    fprintf(f, "Stack por worker    : %d KB\n", WORKER_STACK_SIZE / 1024);
    fprintf(f, "Duración total      : %.1f s\n", elapsed_s);
    fprintf(f, "Muestras            : %ld\n", n);
    fprintf(f, "\n");
    fprintf(f, "--- MÉTRICAS PROMEDIO (fase activa) ---\n");
    fprintf(f, "RAM (RSS) promedio  : %.2f MB\n", avg_rss);
    fprintf(f, "RAM (RSS) pico      : %.2f MB\n", peak_rss);
    fprintf(f, "Dials/s promedio    : %.1f\n",    avg_dials_s);
    fprintf(f, "Frees/s promedio    : %.1f\n",    avg_frees_s);
    fprintf(f, "Tráfico promedio    : %.2f KB/s\n", avg_traffic);
    fprintf(f, "\n");
    fprintf(f, "--- TOTALES ACUMULADOS ---\n");
    fprintf(f, "Total dials         : %ld\n", total_dials);
    fprintf(f, "Total timeouts      : %ld\n", total_timeouts);
    fprintf(f, "Bytes enviados      : %ld\n", total_bytes_sent);
    fprintf(f, "Bytes recibidos     : %ld\n", total_bytes_recv);
    fprintf(f, "Total free()s       : %ld\n", total_frees);
    fprintf(f, "\n");
    fprintf(f, "====================================================================\n");
    fprintf(f, "Notas MIPS:\n");
    fprintf(f, "  - MIPS 32-bit big-endian (ABI O32)\n");
    fprintf(f, "  - SHA-256 con operaciones 32-bit nativas MIPS\n");
    fprintf(f, "  - Atómicos C11 con LL/SC (Load-Linked/Store-Conditional)\n");
    fprintf(f, "  - Destino real: routers IoT Atheros MIPS (TP-Link, Netgear)\n");
    fprintf(f, "  - Mirai original: 10 arquitecturas incluyendo MIPS y MIPSEL\n");
    fprintf(f, "====================================================================\n");

    fclose(f);

    fprintf(stderr, "[TELEMETRY MIPS] Resultados guardados en: %s\n", saved_path);
    fflush(stderr);
}


/*
 * telemetry.c — Recolección de métricas y generación de CSV (Arquitectura MIPS)
 *
 * Implementa el registro periódico (1s) de las tres métricas principales:
 *   1. Consumo de memoria RAM (usada y máxima en MB)
 *   2. Consumo de red (RX, TX y total acumulado en MB)
 *   3. Unidades Concurrentes de Ataque (UCA activas y máximo simultáneo)
 *
 * Compatible con MIPS 32-bit (instrucciones atómicas LL/SC con -latomic).
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
#include <stdbool.h>

#include "telemetry.h"
#include "memory.h"
#include "config.h"

/** Instancia global de métricas, compartida entre todos los módulos. */
Metrics g_metrics;

static FILE *g_csv_fp = NULL;
static char  g_csv_path[512] = {0};
static char  g_arch[32] = "MIPS";
static int   g_memory_limit_mb = 0;

static unsigned long long g_base_rx_bytes = 0;
static unsigned long long g_base_tx_bytes = 0;
static bool               g_has_net_dev   = false;

static double g_last_net_rx_mb    = 0.0;
static double g_last_net_tx_mb    = 0.0;
static double g_last_net_total_mb = 0.0;

/**
 * Lee los bytes acumulados RX y TX de las interfaces del contenedor (/proc/net/dev)
 * excluyendo la interfaz loopback 'lo'.
 */
static int read_container_net_bytes(unsigned long long *rx_bytes,
                                    unsigned long long *tx_bytes)
{
    if (!rx_bytes || !tx_bytes) return -1;
    *rx_bytes = 0;
    *tx_bytes = 0;

    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return -1;

    char line[512];
    int found = 0;

    if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        char *ifname = line;
        while (*ifname == ' ' || *ifname == '\t') ifname++;

        if (strcmp(ifname, "lo") == 0) continue;

        unsigned long long r_bytes = 0, r_packets = 0, r_errs = 0, r_drop = 0;
        unsigned long long r_fifo = 0, r_frame = 0, r_compressed = 0, r_multicast = 0;
        unsigned long long t_bytes = 0;

        int scanned = sscanf(colon + 1,
                             "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                             &r_bytes, &r_packets, &r_errs, &r_drop,
                             &r_fifo, &r_frame, &r_compressed, &r_multicast,
                             &t_bytes);
        if (scanned == 9) {
            *rx_bytes += r_bytes;
            *tx_bytes += t_bytes;
            found = 1;
        }
    }

    fclose(fp);
    return found ? 0 : -1;
}

void telemetry_init(void)
{
    atomic_store(&g_metrics.total_dials,      0L);
    atomic_store(&g_metrics.total_timeouts,   0L);
    atomic_store(&g_metrics.total_bytes_sent, 0L);
    atomic_store(&g_metrics.total_bytes_recv, 0L);
    atomic_store(&g_metrics.total_frees,      0L);
    atomic_store(&g_metrics.active_slots,     0);
    atomic_store(&g_metrics.active_workers,   0);
    atomic_store(&g_metrics.uca_max,          0);
    atomic_store(&g_metrics.sum_rss_mb,       0L);
    atomic_store(&g_metrics.sum_uca_active,   0L);
    atomic_store(&g_metrics.sum_dials_per_s,  0L);
    atomic_store(&g_metrics.sum_frees_per_s,  0L);
    atomic_store(&g_metrics.sum_traffic_kbps, 0L);
    atomic_store(&g_metrics.sample_count,     0L);
    atomic_store(&g_metrics.peak_rss_mb,      0L);

    g_last_net_rx_mb    = 0.0;
    g_last_net_tx_mb    = 0.0;
    g_last_net_total_mb = 0.0;

    g_has_net_dev = (read_container_net_bytes(&g_base_rx_bytes, &g_base_tx_bytes) == 0);
}

int telemetry_setup_csv(const char *csv_path, const char *arch, int memory_limit_mb)
{
    if (arch && arch[0] != '\0') {
        strncpy(g_arch, arch, sizeof(g_arch) - 1);
        g_arch[sizeof(g_arch) - 1] = '\0';
    }
    if (memory_limit_mb > 0) {
        g_memory_limit_mb = memory_limit_mb;
    }

    if (!csv_path || csv_path[0] == '\0') {
        return 0;
    }

    strncpy(g_csv_path, csv_path, sizeof(g_csv_path) - 1);
    g_csv_path[sizeof(g_csv_path) - 1] = '\0';

    g_csv_fp = fopen(csv_path, "w");
    if (!g_csv_fp) {
        perror("[TELEMETRY MIPS] Error al abrir archivo CSV");
        return -1;
    }

    fprintf(g_csv_fp,
            "timestamp,elapsed_seconds,architecture,memory_limit_mb,"
            "ram_used_mb,ram_max_mb,network_rx_mb,network_tx_mb,"
            "network_total_mb,uca_active,uca_max\n");
    fflush(g_csv_fp);
    return 0;
}

void telemetry_close_csv(void)
{
    if (g_csv_fp) {
        fflush(g_csv_fp);
        fclose(g_csv_fp);
        g_csv_fp = NULL;
    }
}

void telemetry_update_uca_max(int current_uca)
{
    if (current_uca <= 0) return;
    int prev_max = atomic_load(&g_metrics.uca_max);
    while (current_uca > prev_max) {
        if (atomic_compare_exchange_weak(&g_metrics.uca_max, &prev_max, current_uca)) {
            break;
        }
    }
}

void telemetry_snapshot(TelemetrySnapshot *snap,
                        long prev_dials, long prev_frees,
                        long prev_bytes, double elapsed_s,
                        double total_elapsed_s)
{
    long cur_dials = atomic_load(&g_metrics.total_dials);
    long cur_frees = atomic_load(&g_metrics.total_frees);
    long cur_bytes_sent = atomic_load(&g_metrics.total_bytes_sent);
    long cur_bytes_recv = atomic_load(&g_metrics.total_bytes_recv);
    long cur_bytes = cur_bytes_sent + cur_bytes_recv;
    int  cur_slots = atomic_load(&g_metrics.active_slots);

    telemetry_update_uca_max(cur_slots);
    int cur_uca_max = atomic_load(&g_metrics.uca_max);
    if (cur_slots > cur_uca_max) cur_uca_max = cur_slots;

    double dt = (elapsed_s > 0.001) ? elapsed_s : 1.0;

    snap->dials_per_s  = (double)(cur_dials - prev_dials)  / dt;
    snap->frees_per_s  = (double)(cur_frees - prev_frees)  / dt;
    snap->traffic_kbps = (double)(cur_bytes - prev_bytes)  / dt / 1024.0;
    snap->rss_mb       = mem_get_rss_mb();
    snap->slots        = cur_slots;
    snap->uca_max      = cur_uca_max;

    /* Actualizar pico de RSS */
    long rss_scaled = (long)(snap->rss_mb * 100.0);
    long cur_peak   = atomic_load(&g_metrics.peak_rss_mb);
    while (rss_scaled > cur_peak) {
        if (atomic_compare_exchange_weak(&g_metrics.peak_rss_mb,
                                         &cur_peak, rss_scaled)) {
            cur_peak = rss_scaled;
            break;
        }
    }
    snap->ram_max_mb = (double)cur_peak / 100.0;
    if (snap->rss_mb > snap->ram_max_mb) {
        snap->ram_max_mb = snap->rss_mb;
    }

    /* Calcular tráfico de red acumulado en MB (interfaz de contenedor + sockets) */
    double net_rx_mb = (double)cur_bytes_recv / (1024.0 * 1024.0);
    double net_tx_mb = (double)cur_bytes_sent / (1024.0 * 1024.0);

    if (g_has_net_dev) {
        unsigned long long dev_rx = 0, dev_tx = 0;
        if (read_container_net_bytes(&dev_rx, &dev_tx) == 0) {
            unsigned long long d_rx = (dev_rx >= g_base_rx_bytes) ? (dev_rx - g_base_rx_bytes) : dev_rx;
            unsigned long long d_tx = (dev_tx >= g_base_tx_bytes) ? (dev_tx - g_base_tx_bytes) : dev_tx;
            double dev_rx_mb = (double)d_rx / (1024.0 * 1024.0);
            double dev_tx_mb = (double)d_tx / (1024.0 * 1024.0);
            if (dev_rx_mb > net_rx_mb) net_rx_mb = dev_rx_mb;
            if (dev_tx_mb > net_tx_mb) net_tx_mb = dev_tx_mb;
        }
    }

    snap->net_rx_mb    = net_rx_mb;
    snap->net_tx_mb    = net_tx_mb;
    snap->net_total_mb = net_rx_mb + net_tx_mb;

    g_last_net_rx_mb    = snap->net_rx_mb;
    g_last_net_tx_mb    = snap->net_tx_mb;
    g_last_net_total_mb = snap->net_total_mb;

    /* Acumular en sumadores para promedio final */
    atomic_fetch_add(&g_metrics.sum_rss_mb,       rss_scaled);
    atomic_fetch_add(&g_metrics.sum_uca_active,   (long)cur_slots);
    atomic_fetch_add(&g_metrics.sum_dials_per_s,  (long)(snap->dials_per_s * 100.0));
    atomic_fetch_add(&g_metrics.sum_frees_per_s,  (long)(snap->frees_per_s * 100.0));
    atomic_fetch_add(&g_metrics.sum_traffic_kbps, (long)(snap->traffic_kbps * 100.0));
    atomic_fetch_add(&g_metrics.sample_count,     1L);

    /* Escribir fila en CSV si está configurado */
    if (g_csv_fp) {
        time_t raw_now = time(NULL);
        struct tm tm_info;
        localtime_r(&raw_now, &tm_info);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

        fprintf(g_csv_fp,
                "%s,%.2f,%s,%d,%.2f,%.2f,%.4f,%.4f,%.4f,%d,%d\n",
                time_str,
                total_elapsed_s,
                g_arch,
                g_memory_limit_mb,
                snap->rss_mb,
                snap->ram_max_mb,
                snap->net_rx_mb,
                snap->net_tx_mb,
                snap->net_total_mb,
                snap->slots,
                snap->uca_max);
        fflush(g_csv_fp);
    }
}

void telemetry_print(const TelemetrySnapshot *snap, double total_elapsed_s)
{
    printf("\r[%s METRICS] t=%.0fs | RAM: %.2f/%.2f MB | Red RX: %.4f MB TX: %.4f MB Total: %.4f MB | UCA: %d (Max: %d)   ",
           g_arch,
           total_elapsed_s,
           snap->rss_mb,
           snap->ram_max_mb,
           snap->net_rx_mb,
           snap->net_tx_mb,
           snap->net_total_mb,
           snap->slots,
           snap->uca_max);
    fflush(stdout);
}

void telemetry_print_summary(int cfg_slots, int cfg_workers, double elapsed_s)
{
    (void)cfg_slots;
    (void)cfg_workers;

    long n = atomic_load(&g_metrics.sample_count);
    long div = (n > 0) ? n : 1;

    double avg_rss  = (double)atomic_load(&g_metrics.sum_rss_mb) / (double)div / 100.0;
    double peak_rss = (double)atomic_load(&g_metrics.peak_rss_mb) / 100.0;
    double avg_uca  = (double)atomic_load(&g_metrics.sum_uca_active) / (double)div;
    int    max_uca  = atomic_load(&g_metrics.uca_max);

    printf("\n\n");
    printf("=================================================================\n");
    printf("        RESUMEN FINAL DEL BENCHMARK C (%s)\n", g_arch);
    printf("=================================================================\n");
    printf("  Arquitectura:                  %s\n", g_arch);
    printf("  Límite de Memoria Docker:      %d MB\n", g_memory_limit_mb);
    printf("  Duración de la prueba:         %.1f segundos (%ld muestras)\n", elapsed_s, n);
    printf("  ---------------------------------------------------------------\n");
    printf("  [Métrica 1] Consumo de Memoria RAM:\n");
    printf("    -> RAM Promedio Utilizada:   %.2f MB\n", avg_rss);
    printf("    -> RAM Máxima (Pico):        %.2f MB\n", peak_rss);
    printf("  ---------------------------------------------------------------\n");
    printf("  [Métrica 2] Consumo de Red (contra servidor_mock):\n");
    printf("    -> Datos Recibidos (RX):     %.4f MB\n", g_last_net_rx_mb);
    printf("    -> Datos Enviados (TX):      %.4f MB\n", g_last_net_tx_mb);
    printf("    -> Total Transferido:        %.4f MB\n", g_last_net_total_mb);
    printf("  ---------------------------------------------------------------\n");
    printf("  [Métrica 3] Unidades Concurrentes de Ataque (UCA):\n");
    printf("    -> UCA Activas Promedio:     %.1f\n", avg_uca);
    printf("    -> UCA Máximo Simultáneo:    %d\n", max_uca);
    if (g_csv_path[0] != '\0') {
        printf("  ---------------------------------------------------------------\n");
        printf("  Archivo CSV generado (en contenedor): %s\n", g_csv_path);
    }
    printf("=================================================================\n");
    fflush(stdout);
}

/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura ARMV7 / 32-bit ARM)
 * Archivo: src/telemetry.c
 * Descripción: Implementación del sistema de telemetría y generación de CSV
 *              para las tres métricas principales: RAM, Red y UCA.
 * ============================================================================
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "telemetry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/**
 * Lee los contadores acumulados de red (RX y TX en bytes) desde /proc/net/dev
 * para las interfaces de red del contenedor (excluyendo loopback 'lo').
 */
static int read_container_net_bytes(unsigned long long *rx_bytes,
                                    unsigned long long *tx_bytes) {
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

void telemetry_init(telemetry_t *t) {
    if (!t) return;
    atomic_init(&t->uca_active, 0);
    atomic_init(&t->uca_max, 0);
    atomic_init(&t->active_slots, 0);
    atomic_init(&t->active_sockets, 0);

    atomic_init(&t->successful_dials, 0);
    atomic_init(&t->failed_dials, 0);
    atomic_init(&t->syn_sent_dials, 0);
    atomic_init(&t->bytes_transferred, 0);
    atomic_init(&t->bytes_received, 0);
    atomic_init(&t->manual_frees, 0);

    atomic_init(&t->total_successful_dials, 0);
    atomic_init(&t->total_failed_dials, 0);
    atomic_init(&t->total_syn_sent_dials, 0);
    atomic_init(&t->total_bytes_transferred, 0);
    atomic_init(&t->total_bytes_received, 0);
    atomic_init(&t->total_manual_frees, 0);

    t->peak_rss_mb       = 0.0;
    t->sum_rss_mb        = 0.0;
    t->sum_uca_active    = 0.0;
    t->sample_count      = 0;
    t->last_net_rx_mb    = 0.0;
    t->last_net_tx_mb    = 0.0;
    t->last_net_total_mb = 0.0;

    strncpy(t->architecture, "ARMV7", sizeof(t->architecture) - 1);
    t->architecture[sizeof(t->architecture) - 1] = '\0';
    t->memory_limit_mb = 0;
    t->csv_fp          = NULL;
    t->csv_path[0]     = '\0';
}

int telemetry_setup_csv(telemetry_t *t, const char *csv_path,
                        const char *arch, int memory_limit_mb) {
    if (!t) return -1;

    if (arch && arch[0] != '\0') {
        strncpy(t->architecture, arch, sizeof(t->architecture) - 1);
        t->architecture[sizeof(t->architecture) - 1] = '\0';
    }
    if (memory_limit_mb > 0) {
        t->memory_limit_mb = memory_limit_mb;
    }

    if (!csv_path || csv_path[0] == '\0') {
        return 0;
    }

    strncpy(t->csv_path, csv_path, sizeof(t->csv_path) - 1);
    t->csv_path[sizeof(t->csv_path) - 1] = '\0';

    t->csv_fp = fopen(csv_path, "w");
    if (!t->csv_fp) {
        perror("[-] Error al abrir archivo CSV de telemetría en ARMv7");
        return -1;
    }

    fprintf(t->csv_fp,
            "timestamp,elapsed_seconds,architecture,memory_limit_mb,"
            "ram_used_mb,ram_max_mb,network_rx_mb,network_tx_mb,"
            "network_total_mb,uca_active,uca_max\n");
    fflush(t->csv_fp);
    return 0;
}

void telemetry_close_csv(telemetry_t *t) {
    if (!t) return;
    if (t->csv_fp) {
        fflush(t->csv_fp);
        fclose(t->csv_fp);
        t->csv_fp = NULL;
    }
}

void telemetry_update_uca_max(telemetry_t *t, long current_uca) {
    if (!t || current_uca <= 0) return;
    long prev_max = atomic_load(&t->uca_max);
    while (current_uca > prev_max) {
        if (atomic_compare_exchange_weak(&t->uca_max, &prev_max, current_uca)) {
            break;
        }
    }
}

double telemetry_get_process_rss_mb(void) {
    double rss_mb = 0.0;

    FILE *fp = fopen("/proc/self/statm", "r");
    if (fp) {
        long total_pages = 0;
        long rss_pages = 0;
        if (fscanf(fp, "%ld %ld", &total_pages, &rss_pages) == 2) {
            long page_size_bytes = sysconf(_SC_PAGESIZE);
            if (page_size_bytes <= 0) page_size_bytes = 4096;
            rss_mb = ((double)rss_pages * (double)page_size_bytes) / (1024.0 * 1024.0);
        }
        fclose(fp);
    }

    FILE *cg_max = fopen("/sys/fs/cgroup/memory.max", "r");
    if (cg_max) {
        char max_buf[64] = {0};
        if (fgets(max_buf, sizeof(max_buf), cg_max)) {
            if (max_buf[0] >= '0' && max_buf[0] <= '9') {
                FILE *cg_cur = fopen("/sys/fs/cgroup/memory.current", "r");
                if (cg_cur) {
                    unsigned long long cur_bytes = 0;
                    if (fscanf(cg_cur, "%llu", &cur_bytes) == 1) {
                        unsigned long long inactive_file = 0;
                        FILE *cg_stat = fopen("/sys/fs/cgroup/memory.stat", "r");
                        if (cg_stat) {
                            char sline[128];
                            while (fgets(sline, sizeof(sline), cg_stat)) {
                                if (sscanf(sline, "inactive_file %llu", &inactive_file) == 1) {
                                    break;
                                }
                            }
                            fclose(cg_stat);
                        }
                        unsigned long long working_bytes = (cur_bytes > inactive_file)
                                                           ? (cur_bytes - inactive_file)
                                                           : cur_bytes;
                        double cg_mb = (double)working_bytes / (1024.0 * 1024.0);
                        if (cg_mb > rss_mb) {
                            rss_mb = cg_mb;
                        }
                    }
                    fclose(cg_cur);
                }
            }
        }
        fclose(cg_max);
    }

    return rss_mb;
}

void *telemetry_reporter_thread(void *arg) {
    telemetry_reporter_args_t *args = (telemetry_reporter_args_t *)arg;
    telemetry_t *metrics = args->metrics;
    volatile sig_atomic_t *running = args->keep_running;

    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    unsigned long long base_rx_bytes = 0, base_tx_bytes = 0;
    bool has_net_dev = (read_container_net_bytes(&base_rx_bytes, &base_tx_bytes) == 0);

    struct timespec sleep_interval;
    sleep_interval.tv_sec = 0;
    sleep_interval.tv_nsec = 250 * 1000 * 1000; // 250 ms (4 ticks = 1 s por fila CSV)

    int sub_tick = 0;

    while (*running) {
        nanosleep(&sleep_interval, NULL);
        if (!*running) break;

        long dials_ok  = atomic_exchange(&metrics->successful_dials, 0);
        long dials_err = atomic_exchange(&metrics->failed_dials, 0);
        long dials_syn = atomic_exchange(&metrics->syn_sent_dials, 0);
        long bytes_tx  = atomic_exchange(&metrics->bytes_transferred, 0);
        long bytes_rx  = atomic_exchange(&metrics->bytes_received, 0);
        long frees_cnt = atomic_exchange(&metrics->manual_frees, 0);

        atomic_fetch_add(&metrics->total_successful_dials, dials_ok);
        atomic_fetch_add(&metrics->total_failed_dials, dials_err);
        atomic_fetch_add(&metrics->total_syn_sent_dials, dials_syn);
        atomic_fetch_add(&metrics->total_bytes_transferred, bytes_tx);
        atomic_fetch_add(&metrics->total_bytes_received, bytes_rx);
        atomic_fetch_add(&metrics->total_manual_frees, frees_cnt);

        long uca_cur = atomic_load(&metrics->uca_active);
        telemetry_update_uca_max(metrics, uca_cur);

        double rss_mb = telemetry_get_process_rss_mb();
        if (rss_mb > metrics->peak_rss_mb) {
            metrics->peak_rss_mb = rss_mb;
        }

        sub_tick++;
        if (sub_tick < 4) {
            continue;
        }
        sub_tick = 0;

        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        double elapsed_sec = (double)(now_ts.tv_sec - start_ts.tv_sec) +
                             (double)(now_ts.tv_nsec - start_ts.tv_nsec) / 1e9;

        long uca_max = atomic_load(&metrics->uca_max);
        if (uca_cur > uca_max) {
            uca_max = uca_cur;
        }

        double net_rx_mb = (double)atomic_load(&metrics->total_bytes_received) / (1024.0 * 1024.0);
        double net_tx_mb = (double)atomic_load(&metrics->total_bytes_transferred) / (1024.0 * 1024.0);

        if (has_net_dev) {
            unsigned long long cur_rx = 0, cur_tx = 0;
            if (read_container_net_bytes(&cur_rx, &cur_tx) == 0) {
                unsigned long long delta_rx = (cur_rx >= base_rx_bytes) ? (cur_rx - base_rx_bytes) : cur_rx;
                unsigned long long delta_tx = (cur_tx >= base_tx_bytes) ? (cur_tx - base_tx_bytes) : cur_tx;
                double dev_rx_mb = (double)delta_rx / (1024.0 * 1024.0);
                double dev_tx_mb = (double)delta_tx / (1024.0 * 1024.0);
                if (dev_rx_mb > net_rx_mb) net_rx_mb = dev_rx_mb;
                if (dev_tx_mb > net_tx_mb) net_tx_mb = dev_tx_mb;
            }
        }

        double net_total_mb = net_rx_mb + net_tx_mb;

        metrics->sum_rss_mb        += rss_mb;
        metrics->sum_uca_active    += (double)uca_cur;
        metrics->sample_count      += 1;
        metrics->last_net_rx_mb     = net_rx_mb;
        metrics->last_net_tx_mb     = net_tx_mb;
        metrics->last_net_total_mb  = net_total_mb;

        time_t raw_now = time(NULL);
        struct tm tm_info;
        localtime_r(&raw_now, &tm_info);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

        if (metrics->csv_fp) {
            fprintf(metrics->csv_fp,
                    "%s,%.2f,%s,%d,%.2f,%.2f,%.4f,%.4f,%.4f,%ld,%ld\n",
                    time_str,
                    elapsed_sec,
                    metrics->architecture,
                    metrics->memory_limit_mb,
                    rss_mb,
                    metrics->peak_rss_mb,
                    net_rx_mb,
                    net_tx_mb,
                    net_total_mb,
                    uca_cur,
                    uca_max);
            fflush(metrics->csv_fp);
        }

        printf("\r[%s METRICS] t=%.0fs | RAM: %.2f/%.2f MB | Red RX: %.4f MB TX: %.4f MB Total: %.4f MB | UCA: %ld (Max: %ld)   ",
               metrics->architecture,
               elapsed_sec,
               rss_mb,
               metrics->peak_rss_mb,
               net_rx_mb,
               net_tx_mb,
               net_total_mb,
               uca_cur,
               uca_max);
        fflush(stdout);
    }

    return NULL;
}

void telemetry_print_final_summary(const telemetry_t *t, int heap_target_mb,
                                   int simulated_slots, double idle_rss,
                                   double duration_sec) {
    if (!t) return;
    (void)heap_target_mb;
    (void)simulated_slots;
    (void)idle_rss;

    long samples = t->sample_count > 0 ? t->sample_count : 1;
    double avg_ram_mb = t->sample_count > 0 ? (t->sum_rss_mb / (double)samples) : telemetry_get_process_rss_mb();
    double max_ram_mb = t->peak_rss_mb > 0.0 ? t->peak_rss_mb : avg_ram_mb;
    double avg_uca    = t->sample_count > 0 ? (t->sum_uca_active / (double)samples) : (double)atomic_load(&t->uca_active);
    long   max_uca    = atomic_load(&t->uca_max);

    printf("\n\n");
    printf("=================================================================\n");
    printf("        RESUMEN FINAL DEL BENCHMARK C (%s)\n", t->architecture);
    printf("=================================================================\n");
    printf("  Arquitectura:                  %s\n", t->architecture);
    printf("  Límite de Memoria Docker:      %d MB\n", t->memory_limit_mb);
    printf("  Duración de la prueba:         %.1f segundos (%ld muestras)\n", duration_sec, t->sample_count);
    printf("  ---------------------------------------------------------------\n");
    printf("  [Métrica 1] Consumo de Memoria RAM:\n");
    printf("    -> RAM Promedio Utilizada:   %.2f MB\n", avg_ram_mb);
    printf("    -> RAM Máxima (Pico):        %.2f MB\n", max_ram_mb);
    printf("  ---------------------------------------------------------------\n");
    printf("  [Métrica 2] Consumo de Red (contra servidor_mock):\n");
    printf("    -> Datos Recibidos (RX):     %.4f MB\n", t->last_net_rx_mb);
    printf("    -> Datos Enviados (TX):      %.4f MB\n", t->last_net_tx_mb);
    printf("    -> Total Transferido:        %.4f MB\n", t->last_net_total_mb);
    printf("  ---------------------------------------------------------------\n");
    printf("  [Métrica 3] Unidades Concurrentes de Ataque (UCA):\n");
    printf("    -> UCA Activas Promedio:     %.1f\n", avg_uca);
    printf("    -> UCA Máximo Simultáneo:    %ld\n", max_uca);
    if (t->csv_path[0] != '\0') {
        printf("  ---------------------------------------------------------------\n");
        printf("  Archivo CSV generado (en contenedor): %s\n", t->csv_path);
    }
    printf("=================================================================\n");
}

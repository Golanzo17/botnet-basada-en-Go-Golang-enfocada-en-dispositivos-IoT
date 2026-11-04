/*
 * memory.c — Allocator custom con seguimiento (Arquitectura MIPS)
 *
 * Envuelve malloc/free para:
 *   - Registrar frees en g_metrics.total_frees (atómico).
 *   - Leer /proc/self/status y cgroups para obtener el consumo efectivo de RAM (MB).
 *
 * Compatible con MIPS 32-bit Linux.
 * sizeof(void*) == 4; los punteros son 32-bit en MIPS O32 ABI.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "memory.h"
#include "telemetry.h"  /* g_metrics */

void *mem_alloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "[MEMORY MIPS] malloc(%zu) falló — sin memoria\n", size);
    }
    return ptr;
}

void mem_free(void *ptr)
{
    if (!ptr) return;
    free(ptr);
    atomic_fetch_add_explicit(&g_metrics.total_frees, 1L, memory_order_relaxed);
}

double mem_get_rss_mb(void)
{
    double rss_mb = 0.0;

    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[128];
        long rss_kb = 0;

        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                if (sscanf(line + 6, "%ld", &rss_kb) == 1) {
                    break;
                }
            }
        }
        fclose(f);
        rss_mb = (double)rss_kb / 1024.0;
    }

    /* Consultar también el cgroup v2 del contenedor si tiene límite configurado */
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

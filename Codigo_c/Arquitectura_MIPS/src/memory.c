/*
 * memory.c — Allocator custom con seguimiento (Arquitectura MIPS)
 *
 * Envuelve malloc/free para:
 *   - Registrar frees en g_metrics.total_frees (atómico).
 *   - Leer /proc/self/status para obtener VmRSS (RSS en kB → MB).
 *
 * Compatible con MIPS 32-bit Linux.
 * sizeof(void*) == 4; los punteros son 32-bit en MIPS O32 ABI.
 * /proc/self/status está disponible en kernels Linux MIPS.
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

/* -------------------------------------------------------------------------
 * Implementación pública
 * ------------------------------------------------------------------------- */

/**
 * mem_alloc() — malloc con reporte de error.
 *
 * En MIPS 32-bit, el heap está limitado por la arquitectura de 32-bit
 * (máx ~3 GB de espacio de usuario). Para IoT con 32 MB de RAM, el límite
 * real es mucho menor. Fallos de malloc deben reportarse inmediatamente.
 */
void *mem_alloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "[MEMORY MIPS] malloc(%zu) falló — sin memoria\n", size);
    }
    return ptr;
}

/**
 * mem_free() — free con contador atómico.
 *
 * Incrementa g_metrics.total_frees para calcular frees/s en telemetría.
 * atomic_fetch_add con memory_order_relaxed es suficiente para contadores.
 * En MIPS 32-bit, esto se implementa con instrucciones LL/SC.
 */
void mem_free(void *ptr)
{
    if (!ptr) return;
    free(ptr);
    atomic_fetch_add_explicit(&g_metrics.total_frees, 1L, memory_order_relaxed);
}

/**
 * mem_get_rss_mb() — Lee el RSS actual del proceso desde /proc/self/status.
 *
 * /proc/self/status contiene una línea "VmRSS:\t<kB> kB".
 * Se parsea con fgets() + strstr() para evitar dependencias de scanf.
 *
 * Funciona en Linux MIPS porque /proc es un VFS del kernel, independiente
 * de la arquitectura del espacio de usuario.
 *
 * @return RSS en MB (double), o 0.0 si no puede leer el archivo.
 */
double mem_get_rss_mb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return 0.0;

    char line[128];
    long rss_kb = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            /* Formato: "VmRSS:\t   1234 kB\n" */
            if (sscanf(line + 6, "%ld", &rss_kb) == 1) {
                break;
            }
        }
    }

    fclose(f);
    return (double)rss_kb / 1024.0;
}

/*
 * memory.h — Allocator custom con seguimiento (Arquitectura MIPS)
 *
 * Envuelve malloc/free estándar para:
 *   - Registrar cada free() en las métricas globales (g_metrics.total_frees).
 *   - Proveer un punto centralizado para debug de memoria.
 *   - Simular el comportamiento manual de gestión de memoria de Mirai.
 *
 * Compatible con MIPS 32-bit. sizeof(void*) == 4; los punteros son 32-bit.
 * No usa atomic en este módulo; los contadores se actualizan en telemetry.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Funciones públicas
 * ------------------------------------------------------------------------- */

/**
 * mem_alloc() — Aloca memoria y registra la operación.
 *
 * Equivalente a malloc(), pero con logging opcional (compilado con DEBUG).
 * En caso de fallo de allocación, imprime un error y retorna NULL.
 *
 * @param size  Bytes a alocar.
 * @return      Puntero a la memoria alocada, o NULL si falla.
 */
void *mem_alloc(size_t size);

/**
 * mem_free() — Libera memoria e incrementa el contador global de frees.
 *
 * Equivalente a free(), pero actualiza g_metrics.total_frees atómicamente.
 * Es seguro llamar con puntero NULL (no-op como free() estándar).
 *
 * @param ptr   Puntero a liberar (puede ser NULL).
 */
void mem_free(void *ptr);

/**
 * mem_get_rss_mb() — Retorna el RSS actual del proceso en megabytes.
 *
 * Lee /proc/self/status en Linux para obtener VmRSS.
 * Compatible con Linux MIPS (kernel MIPS soporta /proc/self/status).
 *
 * @return  RSS en MB como double, o 0.0 si no puede leerlo.
 */
double mem_get_rss_mb(void);

#endif /* MEMORY_H */


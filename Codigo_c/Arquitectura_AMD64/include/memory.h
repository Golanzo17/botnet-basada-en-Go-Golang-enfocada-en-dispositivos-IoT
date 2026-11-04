/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: include/memory.h
 * Descripción: Gestión controlada de memoria dinámica estática en el Heap,
 *              homólogo a la función allocateStaticHeap de cliente.go.
 * ============================================================================
 */

#ifndef BENCHMARK_MEMORY_H
#define BENCHMARK_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/**
 * Reserva target_mb megabytes en bloques de 1 MB en el Heap mediante malloc().
 * Toca activamente cada byte asignado para forzar al kernel Linux a asignar páginas
 * físicas reales de memoria RAM (RSS), emulando la retención de heap en Go.
 *
 * @param target_mb Cantidad de megabytes a retener.
 * @param out_chunks Puntero de salida donde se guardará el array de punteros asignados.
 * @return 0 en caso de éxito, -1 en caso de error de asignación.
 */
int memory_allocate_static_heap(int target_mb, uint8_t ***out_chunks);

/**
 * Libera de forma completa y determinista todos los bloques reservados en el Heap,
 * garantizando cero fugas de memoria (valgrind-clean).
 *
 * @param chunks Array de punteros devuelto por memory_allocate_static_heap.
 * @param target_mb Cantidad de megabytes que fueron reservados.
 */
void memory_free_static_heap(uint8_t **chunks, int target_mb);

#endif /* BENCHMARK_MEMORY_H */


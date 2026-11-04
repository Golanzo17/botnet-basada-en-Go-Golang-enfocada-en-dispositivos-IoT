/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura ARMV7 / 32-bit ARM)
 * Archivo: src/memory.c
 * Descripción: Implementación de retención y liberación determinista de Heap en ARMv7.
 * ============================================================================
 */

#include "memory.h"
#include <stdio.h>
#include <stdlib.h>

int memory_allocate_static_heap(int target_mb, uint8_t ***out_chunks) {
    if (!out_chunks) return -1;
    if (target_mb <= 0) {
        *out_chunks = NULL;
        return 0;
    }

    printf("[+] Reservando %d MB de RAM fija en el Heap (malloc ARMv7)...\n", target_mb);

    uint8_t **chunks = (uint8_t **)malloc(sizeof(uint8_t *) * (size_t)target_mb);
    if (!chunks) {
        perror("malloc static heap array");
        return -1;
    }

    const size_t chunk_size = 1024 * 1024; // 1 MB por bloque
    for (int i = 0; i < target_mb; i++) {
        chunks[i] = (uint8_t *)malloc(chunk_size);
        if (!chunks[i]) {
            perror("malloc static heap chunk");
            for (int k = 0; k < i; k++) {
                free(chunks[k]);
            }
            free(chunks);
            *out_chunks = NULL;
            return -1;
        }

        // Forzar page faults en el MMU de ARM para asegurar asignación física real (RSS)
        for (size_t j = 0; j < chunk_size; j++) {
            chunks[i][j] = (uint8_t)((i + j) % 256);
        }
    }

    *out_chunks = chunks;
    return 0;
}

void memory_free_static_heap(uint8_t **chunks, int target_mb) {
    if (!chunks || target_mb <= 0) return;

    for (int i = 0; i < target_mb; i++) {
        if (chunks[i]) {
            free(chunks[i]);
            chunks[i] = NULL;
        }
    }
    free(chunks);
}


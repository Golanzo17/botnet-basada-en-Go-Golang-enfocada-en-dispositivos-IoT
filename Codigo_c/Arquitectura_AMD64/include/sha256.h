/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: include/sha256.h
 * Descripción: Implementación compacta y autónoma del algoritmo FIPS 180-4
 *              SHA-256 para cputime sintético y verificación de integridad.
 *              No depende de bibliotecas criptográficas externas (OpenSSL/libcrypto).
 * ============================================================================
 */

#ifndef BENCHMARK_SHA256_H
#define BENCHMARK_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

/**
 * Inicializa el contexto de cómputo SHA-256.
 */
void sha256_init(sha256_ctx_t *ctx);

/**
 * Procesa un bloque de datos de tamaño arbitrario.
 */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * Finaliza el cálculo del hash y almacena el resultado de 32 bytes en `hash`.
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t hash[SHA256_DIGEST_SIZE]);

/**
 * Función de conveniencia de un solo paso: calcula SHA-256 sobre un buffer completo.
 */
void sha256_compute(const uint8_t *data, size_t len, uint8_t hash[SHA256_DIGEST_SIZE]);

#endif /* BENCHMARK_SHA256_H */


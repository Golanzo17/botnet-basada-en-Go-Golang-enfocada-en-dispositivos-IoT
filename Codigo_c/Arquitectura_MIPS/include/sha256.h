/*
 * sha256.h — Interfaz pública de SHA-256 autocontenida (Arquitectura MIPS)
 *
 * Implementación puramente en C11 sin dependencias externas.
 * Funciona correctamente en sistemas big-endian y little-endian,
 * incluyendo MIPS 32-bit (que es big-endian por defecto).
 *
 * Las operaciones de word son naturalmente de 32 bits, alineadas para MIPS.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Constantes del algoritmo
 * ------------------------------------------------------------------------- */

/** Tamaño del digest SHA-256 en bytes. */
#define SHA256_DIGEST_SIZE  32

/** Tamaño de bloque SHA-256 en bytes. */
#define SHA256_BLOCK_SIZE   64

/* -------------------------------------------------------------------------
 * Estructura de contexto SHA-256
 * ------------------------------------------------------------------------- */

/**
 * Contexto de hash SHA-256.
 * Mantiene el estado interno entre llamadas a sha256_update().
 */
typedef struct {
    uint32_t    state[8];               /**< Estado interno (H0..H7).      */
    uint8_t     buffer[SHA256_BLOCK_SIZE]; /**< Buffer de bloque parcial.  */
    uint64_t    bit_count;              /**< Bits procesados hasta ahora.   */
} SHA256_CTX;

/* -------------------------------------------------------------------------
 * Funciones públicas
 * ------------------------------------------------------------------------- */

/**
 * sha256_init() — Inicializa el contexto SHA-256.
 *
 * @param ctx   Puntero al contexto a inicializar.
 */
void sha256_init(SHA256_CTX *ctx);

/**
 * sha256_update() — Alimenta datos al contexto SHA-256.
 *
 * Puede llamarse múltiples veces para datos en streaming.
 *
 * @param ctx   Contexto SHA-256.
 * @param data  Puntero a los datos de entrada.
 * @param len   Longitud en bytes de los datos.
 */
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);

/**
 * sha256_final() — Finaliza el hash y escribe el digest.
 *
 * @param ctx     Contexto SHA-256 (se invalida tras la llamada).
 * @param digest  Buffer de salida (mínimo SHA256_DIGEST_SIZE bytes).
 */
void sha256_final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/**
 * sha256_hex() — Calcula SHA-256 y retorna representación hexadecimal.
 *
 * Conveniencia: hace init+update+final en una sola llamada.
 *
 * @param data    Datos de entrada.
 * @param len     Longitud de los datos.
 * @param hexout  Buffer de salida (mínimo 65 bytes: 64 hex + '\0').
 */
void sha256_hex(const uint8_t *data, size_t len, char hexout[65]);

#endif /* SHA256_H */


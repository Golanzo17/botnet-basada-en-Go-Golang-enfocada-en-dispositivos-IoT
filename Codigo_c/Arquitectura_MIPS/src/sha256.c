/*
 * sha256.c — Implementación autocontenida de SHA-256 (Arquitectura MIPS)
 *
 * Implementación puramente en C11, sin dependencias externas (no OpenSSL).
 * Portable entre big-endian y little-endian:
 *   - MIPS es big-endian por defecto (network byte order nativo).
 *   - Las operaciones usan uint32_t con rotaciones explícitas.
 *   - La conversión de bytes a words usa shifts portables (no memcpy ni unions).
 *
 * En MIPS, las operaciones de 32-bit (AND, OR, XOR, ROT) son instrucciones
 * nativas de un ciclo. SHA-256 es naturalmente eficiente en MIPS 32-bit.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#include <string.h>
#include <stdio.h>
#include "sha256.h"

/* -------------------------------------------------------------------------
 * Constantes SHA-256 (primeras 32 bits de las raíces cúbicas de primos)
 * ------------------------------------------------------------------------- */

static const uint32_t K[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/* Valores iniciales H0..H7 (primeras 32 bits de raíces cuadradas de primos) */
static const uint32_t H_INIT[8] = {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
    0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
};

/* -------------------------------------------------------------------------
 * Macros de operaciones SHA-256
 * Escritos como shifts portables (funciona igual en big-endian y little-endian)
 * ------------------------------------------------------------------------- */

#define ROTR32(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(e, f, g)   (((e) & (f)) ^ (~(e) & (g)))
#define MAJ(a, b, c)  (((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))
#define SIG0(a)       (ROTR32(a,  2) ^ ROTR32(a, 13) ^ ROTR32(a, 22))
#define SIG1(e)       (ROTR32(e,  6) ^ ROTR32(e, 11) ^ ROTR32(e, 25))
#define sig0(x)       (ROTR32(x,  7) ^ ROTR32(x, 18) ^ ((x) >>  3))
#define sig1(x)       (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

/* Lectura de uint32_t big-endian desde bytes (portable: no depende de endianness del host) */
#define BE32(b, i) ( ((uint32_t)(b)[(i)  ] << 24) \
                   | ((uint32_t)(b)[(i)+1] << 16) \
                   | ((uint32_t)(b)[(i)+2] <<  8) \
                   | ((uint32_t)(b)[(i)+3]       ) )

/* -------------------------------------------------------------------------
 * Función interna: comprime un bloque de 64 bytes
 * ------------------------------------------------------------------------- */

static void sha256_compress(SHA256_CTX *ctx, const uint8_t block[SHA256_BLOCK_SIZE])
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;
    int i;

    /* Preparar el message schedule W[0..63] */
    for (i = 0; i < 16; i++) {
        W[i] = BE32(block, i * 4);
    }
    for (i = 16; i < 64; i++) {
        W[i] = sig1(W[i-2]) + W[i-7] + sig0(W[i-15]) + W[i-16];
    }

    /* Inicializar variables de trabajo */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* 64 rondas de compresión */
    for (i = 0; i < 64; i++) {
        T1 = h + SIG1(e) + CH(e, f, g) + K[i] + W[i];
        T2 = SIG0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    /* Actualizar estado */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* -------------------------------------------------------------------------
 * Implementación pública
 * ------------------------------------------------------------------------- */

void sha256_init(SHA256_CTX *ctx)
{
    ctx->bit_count = 0;
    for (int i = 0; i < 8; i++) {
        ctx->state[i] = H_INIT[i];
    }
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len)
{
    size_t buf_used = (size_t)(ctx->bit_count / 8) % SHA256_BLOCK_SIZE;

    ctx->bit_count += (uint64_t)len * 8;

    /* Completar el bloque parcial del buffer si hay datos previos */
    if (buf_used > 0) {
        size_t space = SHA256_BLOCK_SIZE - buf_used;
        size_t copy  = (len < space) ? len : space;
        memcpy(ctx->buffer + buf_used, data, copy);
        data += copy;
        len  -= copy;
        buf_used += copy;

        if (buf_used == SHA256_BLOCK_SIZE) {
            sha256_compress(ctx, ctx->buffer);
        }
    }

    /* Procesar bloques completos directamente desde data */
    while (len >= SHA256_BLOCK_SIZE) {
        sha256_compress(ctx, data);
        data += SHA256_BLOCK_SIZE;
        len  -= SHA256_BLOCK_SIZE;
    }

    /* Guardar el resto en el buffer */
    if (len > 0) {
        memcpy(ctx->buffer, data, len);
    }
}

void sha256_final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    size_t buf_used = (size_t)(ctx->bit_count / 8) % SHA256_BLOCK_SIZE;

    /* Padding: 0x80 seguido de ceros */
    ctx->buffer[buf_used++] = 0x80;
    if (buf_used > 56) {
        /* No cabe la longitud en este bloque: rellenar y comprimir */
        memset(ctx->buffer + buf_used, 0, SHA256_BLOCK_SIZE - buf_used);
        sha256_compress(ctx, ctx->buffer);
        memset(ctx->buffer, 0, 56);
    } else {
        memset(ctx->buffer + buf_used, 0, 56 - buf_used);
    }

    /* Escribir longitud (big-endian 64-bit) en los últimos 8 bytes */
    uint64_t bits = ctx->bit_count;
    ctx->buffer[56] = (uint8_t)(bits >> 56);
    ctx->buffer[57] = (uint8_t)(bits >> 48);
    ctx->buffer[58] = (uint8_t)(bits >> 40);
    ctx->buffer[59] = (uint8_t)(bits >> 32);
    ctx->buffer[60] = (uint8_t)(bits >> 24);
    ctx->buffer[61] = (uint8_t)(bits >> 16);
    ctx->buffer[62] = (uint8_t)(bits >>  8);
    ctx->buffer[63] = (uint8_t)(bits      );

    sha256_compress(ctx, ctx->buffer);

    /* Escribir digest en big-endian (portable) */
    for (int i = 0; i < 8; i++) {
        digest[i*4 + 0] = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4 + 2] = (uint8_t)(ctx->state[i] >>  8);
        digest[i*4 + 3] = (uint8_t)(ctx->state[i]      );
    }
}

void sha256_hex(const uint8_t *data, size_t len, char hexout[65])
{
    SHA256_CTX ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    static const char hex_chars[] = "0123456789abcdef";

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);

    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        hexout[i*2 + 0] = hex_chars[(digest[i] >> 4) & 0xF];
        hexout[i*2 + 1] = hex_chars[ digest[i]       & 0xF];
    }
    hexout[64] = '\0';
}


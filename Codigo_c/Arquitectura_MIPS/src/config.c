/*
 * config.c — Implementación de parseo de configuración (Arquitectura MIPS)
 *
 * Parsea argumentos de línea de comandos y llena la estructura Config.
 * Realiza resolución DNS via getaddrinfo() para soportar hostnames de Docker.
 *
 * Compatible con MIPS 32-bit Linux. getaddrinfo() es parte de glibc y
 * funciona igual en MIPS que en x86. El struct addrinfo usa punteros 32-bit.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "config.h"

/* -------------------------------------------------------------------------
 * Implementación pública
 * ------------------------------------------------------------------------- */

/**
 * config_parse() — Parsea argv[] y llena la Config con valores o defaults.
 *
 * Sintaxis homóloga a AMD64 y ARMV7:
 *   ./cliente_c_mips [idle_sec] [heap_mb] [total_uca] [host] [port]
 */
int config_parse(Config *cfg, int argc, char *argv[])
{
    if (!cfg) {
        return -1;
    }

    /* ---- Valores por defecto ---- */
    cfg->idle_sec   = IDLE_DURATION;
    cfg->heap_mb    = 2;
    cfg->total_uca  = 64;
    cfg->workers    = 4;
    cfg->slots      = 16; /* 4 workers * 16 slots = 64 UCA concurrentes */
    cfg->stack_kb   = 128;
    cfg->port       = DEFAULT_PORT;
    cfg->addr       = NULL;
    strncpy(cfg->host, DEFAULT_HOST, sizeof(cfg->host) - 1);
    cfg->host[sizeof(cfg->host) - 1] = '\0';

    /* ---- Parseo de argumentos posicionales ---- */
    if (argc > 1) cfg->idle_sec  = atoi(argv[1]);
    if (argc > 2) cfg->heap_mb   = atoi(argv[2]);
    if (argc > 3) cfg->total_uca = atoi(argv[3]);
    if (argc > 4) {
        strncpy(cfg->host, argv[4], sizeof(cfg->host) - 1);
        cfg->host[sizeof(cfg->host) - 1] = '\0';
    }
    if (argc > 5) cfg->port = (uint16_t)atoi(argv[5]);

    /* ---- Validaciones básicas ---- */
    if (cfg->idle_sec < 0)  cfg->idle_sec = IDLE_DURATION;
    if (cfg->heap_mb  < 0)  cfg->heap_mb  = 2;
    if (cfg->total_uca <= 0) cfg->total_uca = 64;

    /* Distribuir UCA entre workers (4 hilos con N slots epoll cada uno) */
    if (cfg->total_uca < 4) {
        cfg->workers = 1;
        cfg->slots   = cfg->total_uca;
    } else {
        cfg->workers = 4;
        cfg->slots   = cfg->total_uca / cfg->workers;
        if (cfg->slots <= 0) cfg->slots = 1;
        if (cfg->slots > MAX_SLOTS) cfg->slots = MAX_SLOTS;
    }

    /* ---- Resolución DNS del host ---- */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", cfg->port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;        /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;    /* TCP  */

    int gai_ret = getaddrinfo(cfg->host, port_str, &hints, &cfg->addr);
    if (gai_ret != 0) {
        fprintf(stderr, "[CONFIG MIPS] Error resolviendo '%s:%s': %s\n",
                cfg->host, port_str, gai_strerror(gai_ret));
        cfg->addr = NULL;
        return -1;
    }

    return 0;
}

/**
 * config_free() — Libera el addrinfo obtenido por getaddrinfo().
 */
void config_free(Config *cfg)
{
    if (cfg && cfg->addr) {
        freeaddrinfo(cfg->addr);
        cfg->addr = NULL;
    }
}

/**
 * config_print() — Muestra la configuración activa en stdout.
 */
void config_print(const Config *cfg)
{
    if (!cfg) return;

    printf("=================================================================\n");
    printf(" BENCHMARK EMPIRICO DE C — ARQUITECTURA MIPS 32-bit (Big-Endian) \n");
    printf(" UCA Totales: %d (%d workers x %d slots) | RAM Heap Base: %d MB | Host: %s:%u\n",
           cfg->workers * cfg->slots, cfg->workers, cfg->slots,
           cfg->heap_mb, cfg->host, cfg->port);
    printf("=================================================================\n");
    fflush(stdout);
}

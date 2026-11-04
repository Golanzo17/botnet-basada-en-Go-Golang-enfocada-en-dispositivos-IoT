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
 * Sintaxis esperada:
 *   ./cliente_c_mips [slots] [workers] [stack_kb] [host] [port]
 *
 * Todos los argumentos son opcionales; si se omiten se usan defaults.
 * Realiza resolución DNS del host al final para detectar errores temprano.
 */
int config_parse(Config *cfg, int argc, char *argv[])
{
    if (!cfg) {
        return -1;
    }

    /* ---- Valores por defecto ---- */
    cfg->slots      = 5;
    cfg->workers    = 2;
    cfg->stack_kb   = 128;
    cfg->port       = DEFAULT_PORT;
    cfg->addr       = NULL;
    strncpy(cfg->host, DEFAULT_HOST, sizeof(cfg->host) - 1);
    cfg->host[sizeof(cfg->host) - 1] = '\0';

    /* ---- Parseo de argumentos positionales ---- */
    if (argc > 1) cfg->slots    = atoi(argv[1]);
    if (argc > 2) cfg->workers  = atoi(argv[2]);
    if (argc > 3) cfg->stack_kb = atoi(argv[3]);
    if (argc > 4) {
        strncpy(cfg->host, argv[4], sizeof(cfg->host) - 1);
        cfg->host[sizeof(cfg->host) - 1] = '\0';
    }
    if (argc > 5) cfg->port = (uint16_t)atoi(argv[5]);

    /* ---- Validaciones básicas ---- */
    if (cfg->slots   <= 0 || cfg->slots   > MAX_SLOTS)   cfg->slots   = 5;
    if (cfg->workers <= 0 || cfg->workers > MAX_WORKERS)  cfg->workers = 2;
    if (cfg->stack_kb <= 0)                               cfg->stack_kb = 128;

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

    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║   Cliente C — Arquitectura MIPS 32-bit (Big-Endian)  ║\n");
    printf("║   Simulación de cliente botnet (benign benchmark)     ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Slots de conexión  : %-4d                           ║\n", cfg->slots);
    printf("║  Workers (hilos)    : %-4d                           ║\n", cfg->workers);
    printf("║  Pila por worker    : %-4d KB                        ║\n", cfg->stack_kb);
    printf("║  Destino            : %s:%-5u                 ║\n",    cfg->host, cfg->port);
    printf("║  DNS resuelto       : %s                           ║\n",
           cfg->addr ? "OK" : "FALLO");
    printf("╚══════════════════════════════════════════════════════╝\n");
    fflush(stdout);
}

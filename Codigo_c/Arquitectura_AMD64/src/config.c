/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: src/config.c
 * Descripción: Implementación del manejo y parseo de configuración.
 * ============================================================================
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

void config_init_defaults(Config *cfg) {
    if (!cfg) return;

    cfg->base_port         = DEFAULT_BASE_PORT;
    cfg->num_workers       = DEFAULT_NUM_WORKERS;
    cfg->simulated_slots   = DEFAULT_SIMULATED_SLOTS;
    cfg->heap_target_mb    = DEFAULT_HEAP_TARGET_MB;
    cfg->cpu_loop_work     = DEFAULT_CPU_LOOP_WORK;
    cfg->dial_timeout_ms   = DEFAULT_DIAL_TIMEOUT_MS;
    cfg->worker_delay_us   = DEFAULT_WORKER_DELAY_US;
    cfg->unreachable_ratio = DEFAULT_UNREACHABLE_RATIO;
    cfg->idle_seconds      = DEFAULT_IDLE_SECONDS;

    strncpy(cfg->target_host, DEFAULT_TARGET_HOST, sizeof(cfg->target_host) - 1);
    cfg->target_host[sizeof(cfg->target_host) - 1] = '\0';

    strncpy(cfg->unreachable_host, DEFAULT_UNREACHABLE_HOST, sizeof(cfg->unreachable_host) - 1);
    cfg->unreachable_host[sizeof(cfg->unreachable_host) - 1] = '\0';

    cfg->unreachable_port = DEFAULT_UNREACHABLE_PORT;
}

static void print_usage(const char *progname) {
    printf("Uso: %s [idle_sec] [heap_mb] [workers/slots]\n", progname);
    printf("     o usando opciones extendidas:\n");
    printf("  -i, --idle <sec>       Segundos en reposo (por defecto: %d)\n", DEFAULT_IDLE_SECONDS);
    printf("  -m, --heap <mb>        MB de RAM estática en Heap (por defecto: %d)\n", DEFAULT_HEAP_TARGET_MB);
    printf("  -w, --workers <num>    Cantidad de hilos POSIX (por defecto: %d)\n", DEFAULT_NUM_WORKERS);
    printf("  -s, --slots <num>      Slots lógicos simulados (por defecto: %d)\n", DEFAULT_SIMULATED_SLOTS);
    printf("  -p, --port <port>      Puerto del servidor mock (por defecto: %d)\n", DEFAULT_BASE_PORT);
    printf("  -h, --host <ip>        IP del servidor mock (por defecto: %s)\n", DEFAULT_TARGET_HOST);
    printf("  -u, --unreach-ratio <%%> Ratio de conexiones a timeout (por defecto: %d%%)\n", DEFAULT_UNREACHABLE_RATIO);
    printf("  --help                 Muestra este mensaje de ayuda\n");
}

int config_parse_args(Config *cfg, int argc, char *argv[]) {
    if (!cfg) return -1;

    // Verificar si se solicitó ayuda
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-?") == 0) {
            print_usage(argv[0]);
            return 1; // Indicador de salida inmediata no errónea
        }
    }

    // Comprobar si se proporcionan argumentos en formato de opciones o posicionales
    bool has_options = false;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            has_options = true;
            break;
        }
    }

    if (has_options) {
        for (int i = 1; i < argc; i++) {
            if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--idle") == 0) && i + 1 < argc) {
                cfg->idle_seconds = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--heap") == 0) && i + 1 < argc) {
                cfg->heap_target_mb = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--workers") == 0) && i + 1 < argc) {
                cfg->num_workers = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--slots") == 0) && i + 1 < argc) {
                cfg->simulated_slots = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
                cfg->base_port = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) && i + 1 < argc) {
                strncpy(cfg->target_host, argv[++i], sizeof(cfg->target_host) - 1);
                cfg->target_host[sizeof(cfg->target_host) - 1] = '\0';
            } else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--unreach-ratio") == 0) && i + 1 < argc) {
                cfg->unreachable_ratio = atoi(argv[++i]);
            }
        }
    } else {
        // Compatibilidad posicional directa con cliente.go:
        // argv[1] -> idle_seconds
        // argv[2] -> heap_target_mb
        // argv[3] -> num_workers / simulated_slots
        if (argc > 1) {
            int val = atoi(argv[1]);
            if (val >= 0) cfg->idle_seconds = val;
        }
        if (argc > 2) {
            int val = atoi(argv[2]);
            if (val >= 0) cfg->heap_target_mb = val;
        }
        if (argc > 3) {
            int val = atoi(argv[3]);
            if (val > 0) {
                // Si el argumento es grande (ej. 4000), ajusta simulated_slots
                // Si el argumento es pequeño (ej. 64), ajusta num_workers
                if (val >= 256) {
                    cfg->simulated_slots = val;
                } else {
                    cfg->num_workers = val;
                }
            }
        }
        if (argc > 4) {
            strncpy(cfg->target_host, argv[4], sizeof(cfg->target_host) - 1);
            cfg->target_host[sizeof(cfg->target_host) - 1] = '\0';
        }
    }

    // Resolver hostname a IPv4 si se pasa un nombre de contenedor Docker o dominio
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(cfg->target_host, NULL, &hints, &res) == 0 && res != NULL) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), cfg->target_host, sizeof(cfg->target_host));
        freeaddrinfo(res);
    }

    // Validaciones de seguridad para límites
    if (cfg->num_workers <= 0) cfg->num_workers = 1;
    if (cfg->simulated_slots < cfg->num_workers) {
        cfg->simulated_slots = cfg->num_workers;
    }
    if (cfg->unreachable_ratio < 0) cfg->unreachable_ratio = 0;
    if (cfg->unreachable_ratio > 100) cfg->unreachable_ratio = 100;
    if (cfg->heap_target_mb < 0) cfg->heap_target_mb = 0;

    return 0;
}

void config_print(const Config *cfg) {
    if (!cfg) return;

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores < 1) num_cores = 1;

    printf("=================================================================\n");
    printf(" BENCHMARK EMPIRICO DE C (CPU + HEAP MANUAL + POSIX SOCKETS)     \n");
    printf(" Cores CPU: %ld | Slots/Workers: %d/%d | RAM Base: %d MB | Host: %s:%d | SYN_SENT: %d%%\n",
           num_cores, cfg->simulated_slots, cfg->num_workers, cfg->heap_target_mb, cfg->target_host, cfg->base_port, cfg->unreachable_ratio);
    printf("=================================================================\n");
}


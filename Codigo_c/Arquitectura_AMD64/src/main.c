/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: src/main.c
 * Descripción: Punto de entrada principal, orquestación del ciclo de vida,
 *              manejo de señales POSIX y reporte experimental en CSV.
 * ============================================================================
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "config.h"
#include "memory.h"
#include "telemetry.h"
#include "worker.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

static volatile sig_atomic_t g_keep_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_keep_running = 0;
}

static void setup_signals(void) {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Ignorar SIGPIPE para evitar terminación abrupta en desconexiones remotas de sockets
    signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char *argv[]) {
    setup_signals();

    Config cfg;
    config_init_defaults(&cfg);

    int parse_res = config_parse_args(&cfg, argc, argv);
    if (parse_res > 0) {
        return EXIT_SUCCESS; // Se mostró la ayuda (--help)
    }
    if (parse_res < 0) {
        return EXIT_FAILURE;
    }

    config_print(&cfg);

    double idle_rss = telemetry_get_process_rss_mb();

    // ========================================================================
    // FASE 1: Medición en Reposo (Idle) para auditoría de memoria base
    // ========================================================================
    if (cfg.idle_seconds > 0) {
        printf("[*] FASE 1 (REPOSO / IDLE): Midiendo huella base del binario en C durante %d s...\n",
               cfg.idle_seconds);
        for (int s = cfg.idle_seconds; s > 0 && g_keep_running; s--) {
            idle_rss = telemetry_get_process_rss_mb();
            printf("\r[C IDLE]    Tiempo restante: %d s | RAM Física (RSS): %.2f MB   ",
                   s, idle_rss);
            fflush(stdout);
            sleep(1);
        }
        printf("\n");
    }

    if (!g_keep_running) {
        return EXIT_SUCCESS;
    }

    // ========================================================================
    // FASE 2: Retención controlada de Heap estático (homólogo a Go)
    // ========================================================================
    uint8_t **heap_chunks = NULL;
    if (cfg.heap_target_mb > 0) {
        if (memory_allocate_static_heap(cfg.heap_target_mb, &heap_chunks) != 0) {
            fprintf(stderr, "[-] Error crítico al reservar memoria estática en Heap.\n");
            return EXIT_FAILURE;
        }
    }

    // ========================================================================
    // FASE 3: Despliegue de Telemetría CSV y Pool de Workers POSIX (UCA)
    // ========================================================================
    telemetry_t telemetry;
    telemetry_init(&telemetry);

    const char *csv_path = getenv("CSV_PATH");
    const char *arch_env = getenv("BENCHMARK_ARCH");
    const char *mem_env  = getenv("BENCHMARK_MEM_MB");
    const char *dur_env  = getenv("BENCHMARK_DURATION");

    int mem_limit_mb = (mem_env && atoi(mem_env) > 0) ? atoi(mem_env) : 0;
    int max_duration = (dur_env && atoi(dur_env) > 0) ? atoi(dur_env) : 0;

    telemetry_setup_csv(&telemetry,
                        csv_path,
                        (arch_env && arch_env[0] != '\0') ? arch_env : "AMD64",
                        mem_limit_mb);

    telemetry_reporter_args_t reporter_args;
    reporter_args.metrics = &telemetry;
    reporter_args.keep_running = &g_keep_running;

    pthread_t reporter_thread;
    if (pthread_create(&reporter_thread, NULL, telemetry_reporter_thread, &reporter_args) != 0) {
        perror("pthread_create telemetry reporter");
        telemetry_close_csv(&telemetry);
        memory_free_static_heap(heap_chunks, cfg.heap_target_mb);
        return EXIT_FAILURE;
    }

    time_t start_time = time(NULL);

    worker_pool_t pool;
    if (worker_pool_start(&pool, &cfg, &telemetry, &g_keep_running) != 0) {
        fprintf(stderr, "[-] Error crítico al iniciar worker pool.\n");
        g_keep_running = 0;
        pthread_join(reporter_thread, NULL);
        telemetry_close_csv(&telemetry);
        memory_free_static_heap(heap_chunks, cfg.heap_target_mb);
        return EXIT_FAILURE;
    }

    // Esperar hasta registrar exactamente max_duration muestras en el CSV
    int elapsed_s = 0;
    while (g_keep_running) {
        usleep(200000);
        elapsed_s++;
        if (max_duration > 0 && (telemetry.sample_count >= max_duration || elapsed_s >= (max_duration + 8) * 5)) {
            g_keep_running = 0;
            break;
        }
    }

    // ========================================================================
    // FINALIZACIÓN CONTROLADA: Detención, unión de hilos y cierre de CSV
    // ========================================================================
    printf("\n[!] Finalizando test de C y cerrando registros...\n");

    time_t end_time = time(NULL);
    double duration = difftime(end_time, start_time);

    worker_pool_wait_and_stop(&pool);
    pthread_join(reporter_thread, NULL);
    telemetry_close_csv(&telemetry);
    memory_free_static_heap(heap_chunks, cfg.heap_target_mb);

    // Emisión del informe final con las 3 métricas consolidadas
    telemetry_print_final_summary(&telemetry, cfg.heap_target_mb, cfg.simulated_slots, idle_rss, duration);

    printf("[+] Prueba de C finalizada limpiamente.\n");
    return EXIT_SUCCESS;
}

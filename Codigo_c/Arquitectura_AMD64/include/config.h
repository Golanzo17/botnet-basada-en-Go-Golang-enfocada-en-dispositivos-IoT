/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: include/config.h
 * Descripción: Estructuras y prototipos para la configuración del benchmark,
 *              homólogo a las opciones de ejecución de cliente.go.
 * ============================================================================
 */

#ifndef BENCHMARK_CONFIG_H
#define BENCHMARK_CONFIG_H

#include <stddef.h>
#include <stdbool.h>

#define DEFAULT_BASE_PORT          9090
#define DEFAULT_NUM_WORKERS        64
#define DEFAULT_SIMULATED_SLOTS    64
#define DEFAULT_HEAP_TARGET_MB     2
#define DEFAULT_CPU_LOOP_WORK      250
#define DEFAULT_DIAL_TIMEOUT_MS    150
#define DEFAULT_WORKER_DELAY_US    1000
#define DEFAULT_UNREACHABLE_RATIO  0
#define DEFAULT_IDLE_SECONDS       2
#define DEFAULT_TARGET_HOST        "127.0.0.1"
#define DEFAULT_UNREACHABLE_HOST   "192.0.2.1"
#define DEFAULT_UNREACHABLE_PORT   9999

typedef struct {
    int  base_port;          // Puerto del listener local (ej. 9090)
    int  num_workers;        // Cantidad de hilos POSIX de trabajo (UCA)
    int  simulated_slots;    // Tareas / slots lógicos concurrentes (UCA objetivo)
    int  heap_target_mb;     // Memoria RAM estática retenida en el Heap (MB)
    int  cpu_loop_work;      // Trabajo de CPU por ciclo (iteraciones SHA-256)
    int  dial_timeout_ms;    // Timeout de sockets TCP en milisegundos
    int  worker_delay_us;    // Pausa entre ciclos de cada worker en microsegundos
    int  unreachable_ratio;  // Porcentaje de conexiones a IP de timeout (0 por defecto: solo mock)
    int  idle_seconds;       // Segundos iniciales en reposo antes de iniciar la carga
    char target_host[64];    // Dirección IP o hostname del servidor mock local
    char unreachable_host[64]; // Dirección IP de prueba para timeout (TEST-NET-1)
    int  unreachable_port;   // Puerto para la prueba de timeout
} Config;

/**
 * Inicializa la configuración con los valores por defecto.
 */
void config_init_defaults(Config *cfg);

/**
 * Procesa los argumentos de la línea de comandos.
 * Soporta modo posicional:
 *   ./cliente_c [idle_sec] [heap_mb] [slots/workers] [target_host]
 * y banderas opcionales:
 *   -h <ip> -p <port> -w <workers> -s <slots> -m <heap_mb> -i <idle_sec>
 */
int config_parse_args(Config *cfg, int argc, char *argv[]);

/**
 * Imprime en stdout el encabezado y resumen de configuración activa.
 */
void config_print(const Config *cfg);

#endif /* BENCHMARK_CONFIG_H */

/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura AMD64 / x86-64)
 * Archivo: include/telemetry.h
 * Descripción: Métricas globales y reporte en vivo de telemetría y uso de memoria.
 *              Utiliza tipos atómicos C11 (<stdatomic.h>) libres de bloqueos (lock-free).
 * ============================================================================
 */

#ifndef BENCHMARK_TELEMETRY_H
#define BENCHMARK_TELEMETRY_H

#include <stdatomic.h>
#include <signal.h>
#include <stdbool.h>

typedef struct {
    atomic_long active_slots;       // Concurrencia activa (slots)
    atomic_long active_sockets;     // Descriptores de socket actualmente abiertos
    atomic_long successful_dials;   // Conexiones TCP establecidas exitosamente
    atomic_long failed_dials;       // Intentos fallidos o rechazados
    atomic_long syn_sent_dials;     // Conexiones dirigidas a timeout (SYN_SENT)
    atomic_long bytes_transferred;  // Bytes transmitidos por la red
    atomic_long manual_frees;       // Liberaciones explícitas de memoria dinámica (malloc/free)
} telemetry_t;

typedef struct {
    telemetry_t                  *metrics;
    volatile sig_atomic_t        *keep_running;
} telemetry_reporter_args_t;

/**
 * Inicializa a cero los contadores de telemetría.
 */
void telemetry_init(telemetry_t *t);

/**
 * Consulta la memoria física real (Resident Set Size - RSS) en megabytes
 * leyendo directamente desde el pseudo-sistema de archivos del kernel Linux (/proc/self/statm).
 */
double telemetry_get_process_rss_mb(void);

/**
 * Hilo reportero periódico (frecuencia de 250 ms, homólogo al ticker de cliente.go).
 */
void *telemetry_reporter_thread(void *arg);

#endif /* BENCHMARK_TELEMETRY_H */


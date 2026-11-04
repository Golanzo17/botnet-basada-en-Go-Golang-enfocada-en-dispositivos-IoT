/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura ARMV7 / 32-bit ARM)
 * Archivo: include/telemetry.h
 * Descripción: Métricas globales y reporte en vivo de telemetría (RAM, Red y UCA).
 *              Utiliza tipos atómicos C11 (<stdatomic.h>) libres de bloqueos (lock-free)
 *              y genera registros temporales en formato CSV estandarizado.
 * ============================================================================
 */

#ifndef BENCHMARK_TELEMETRY_H
#define BENCHMARK_TELEMETRY_H

#include <stdatomic.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    // Métrica 3: Unidades Concurrentes de Ataque (UCA)
    atomic_long uca_active;          // Unidades concurrentes activas en este instante
    atomic_long uca_max;             // Máximo histórico de unidades activas simultáneamente
    atomic_long active_slots;        // Alias de compatibilidad para slots concurrentes
    atomic_long active_sockets;      // Descriptores de socket actualmente abiertos

    // Contadores de actividad y red por intervalo / acumulados
    atomic_long successful_dials;    // Conexiones TCP establecidas exitosamente (intervalo)
    atomic_long failed_dials;        // Intentos fallidos o rechazados (intervalo)
    atomic_long syn_sent_dials;      // Conexiones dirigidas a timeout (intervalo)
    atomic_long bytes_transferred;   // Bytes transmitidos por la red (intervalo)
    atomic_long bytes_received;      // Bytes recibidos por la red (intervalo)
    atomic_long manual_frees;        // Liberaciones explícitas de memoria dinámica (intervalo)

    // Acumuladores globales
    atomic_long total_successful_dials;
    atomic_long total_failed_dials;
    atomic_long total_syn_sent_dials;
    atomic_long total_bytes_transferred; // Total bytes enviados (TX) a nivel socket
    atomic_long total_bytes_received;    // Total bytes recibidos (RX) a nivel socket
    atomic_long total_manual_frees;

    // Acumuladores para promedios y máximos de las 3 métricas principales
    double      peak_rss_mb;         // Consumo máximo de RAM observado (MB)
    double      sum_rss_mb;          // Suma de observaciones de RAM para promedio
    double      sum_uca_active;      // Suma de observaciones de UCA activas para promedio
    long        sample_count;        // Número de observaciones registradas
    double      last_net_rx_mb;      // Último valor acumulado de RX de red (MB)
    double      last_net_tx_mb;      // Último valor acumulado de TX de red (MB)
    double      last_net_total_mb;   // Último valor acumulado de tráfico total (MB)

    // Metadatos administrativos para el archivo CSV
    char        architecture[32];    // Arquitectura del binario (ej. ARMV7)
    int         memory_limit_mb;     // Límite de memoria asignado al contenedor (MB)
    FILE       *csv_fp;              // Descriptor de archivo CSV abierto
    char        csv_path[512];       // Ruta del archivo CSV
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
 * Configura los metadatos del experimento y abre el archivo CSV de salida,
 * escribiendo la cabecera estandarizada.
 */
int telemetry_setup_csv(telemetry_t *t, const char *csv_path,
                        const char *arch, int memory_limit_mb);

/**
 * Cierra de forma segura el archivo CSV si se encuentra abierto.
 */
void telemetry_close_csv(telemetry_t *t);

/**
 * Actualiza de forma atómica el máximo histórico de UCA (uca_max)
 * si el valor actual supera al máximo registrado.
 */
void telemetry_update_uca_max(telemetry_t *t, long current_uca);

/**
 * Consulta la memoria RAM efectivamente utilizada (MB) distinguiendo entre el
 * consumo real del contenedor/proceso y el límite asignado.
 */
double telemetry_get_process_rss_mb(void);

/**
 * Hilo reportero periódico (muestreo cada 1 segundo para evolución temporal CSV).
 */
void *telemetry_reporter_thread(void *arg);

/**
 * Imprime un reporte resumen consolidado al finalizar la prueba centrado en
 * las tres métricas principales: RAM, Red y UCA.
 */
void telemetry_print_final_summary(const telemetry_t *t, int heap_target_mb,
                                   int simulated_slots, double idle_rss,
                                   double duration_sec);

#endif /* BENCHMARK_TELEMETRY_H */

/*
 * telemetry.h — Interfaz de telemetría y benchmark CSV (Arquitectura MIPS)
 *
 * Recolecta las tres métricas principales del estudio:
 *   1. Consumo de memoria RAM (usada, máxima y promedio en MB)
 *   2. Consumo de red (RX, TX y total acumulado en MB)
 *   3. Unidades Concurrentes de Ataque (UCA activas y máximo simultáneo)
 *
 * Compatible con MIPS 32-bit. Los contadores atómicos usan C11 <stdatomic.h>
 * que en MIPS se implementa con instrucciones LL/SC via -latomic.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Estructura de métricas atómicas (hilo-segura)
 * ------------------------------------------------------------------------- */

typedef struct {
    /* Contadores de operaciones (incrementados por workers/network) */
    atomic_long     total_dials;        /**< Total de intentos de conexión.  */
    atomic_long     total_timeouts;     /**< Total de timeouts TCP.           */
    atomic_long     total_bytes_sent;   /**< Total de bytes enviados (TX).    */
    atomic_long     total_bytes_recv;   /**< Total de bytes recibidos (RX).   */
    atomic_long     total_frees;        /**< Total de llamadas free() custom. */

    /* Métrica 3: Unidades Concurrentes de Ataque (UCA) */
    atomic_int      active_slots;       /**< UCA activas en este instante.    */
    atomic_int      active_workers;     /**< Workers corriendo ahora.         */
    atomic_int      uca_max;            /**< Máximo histórico de UCA activas. */

    /* Acumuladores para cálculo de promedios en el resumen final */
    atomic_long     sum_rss_mb;         /**< Suma de muestras RSS (MB * 100). */
    atomic_long     sum_uca_active;     /**< Suma de muestras de UCA activas. */
    atomic_long     sum_dials_per_s;    /**< Suma de muestras dials/s.        */
    atomic_long     sum_frees_per_s;    /**< Suma de muestras frees/s.        */
    atomic_long     sum_traffic_kbps;   /**< Suma de muestras tráfico KB/s.   */
    atomic_long     sample_count;       /**< Cantidad de muestras tomadas.    */
    atomic_long     peak_rss_mb;        /**< Pico máximo de RSS (MB * 100).   */
} Metrics;

/* -------------------------------------------------------------------------
 * Estructura de snapshot de telemetría (un período de 1s)
 * ------------------------------------------------------------------------- */

typedef struct {
    double  rss_mb;         /**< RAM utilizada actual en MB.                */
    double  ram_max_mb;     /**< RAM máxima observada hasta este instante.  */
    double  net_rx_mb;      /**< Datos recibidos acumulados en MB.          */
    double  net_tx_mb;      /**< Datos enviados acumulados en MB.           */
    double  net_total_mb;   /**< Tráfico total transferido acumulado en MB. */
    int     slots;          /**< UCA activas en este instante.              */
    int     uca_max;        /**< UCA máximo observado hasta este instante.  */
    double  dials_per_s;    /**< Conexiones intentadas por segundo.         */
    double  frees_per_s;    /**< Frees realizados por segundo.              */
    double  traffic_kbps;   /**< Tráfico del período en KB/s.               */
} TelemetrySnapshot;

/* -------------------------------------------------------------------------
 * Variables globales exportadas
 * ------------------------------------------------------------------------- */

/** Métricas globales (definidas en telemetry.c, usadas en todos los módulos). */
extern Metrics g_metrics;

/* -------------------------------------------------------------------------
 * Funciones públicas
 * ------------------------------------------------------------------------- */

void telemetry_init(void);

int telemetry_setup_csv(const char *csv_path, const char *arch, int memory_limit_mb);

void telemetry_close_csv(void);

void telemetry_update_uca_max(int current_uca);

void telemetry_snapshot(TelemetrySnapshot *snap,
                        long prev_dials, long prev_frees,
                        long prev_bytes, double elapsed_s,
                        double total_elapsed_s);

void telemetry_print(const TelemetrySnapshot *snap, double total_elapsed_s);

void telemetry_print_summary(int cfg_slots, int cfg_workers, double elapsed_s);

#endif /* TELEMETRY_H */

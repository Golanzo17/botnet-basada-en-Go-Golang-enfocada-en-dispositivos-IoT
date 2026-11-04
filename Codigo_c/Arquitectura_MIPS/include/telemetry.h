/*
 * telemetry.h — Interfaz de telemetría y benchmark (Arquitectura MIPS)
 *
 * Recolecta métricas de rendimiento: uso de RAM (RSS), tasa de dials,
 * timeouts, tráfico de red, frees por segundo, etc.
 *
 * Al finalizar (Ctrl+C), imprime una tabla resumen consolidada y guarda
 * el archivo benchmark_resultado.txt en la ruta del volumen Docker.
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

/* -------------------------------------------------------------------------
 * Estructura de métricas atómicas (hilo-segura)
 * ------------------------------------------------------------------------- */

/**
 * Métricas globales compartidas entre todos los módulos.
 * Todos los campos son atómicos para acceso concurrente seguro sin mutex.
 * En MIPS 32-bit, los atómicos de 64-bit se emulan con LL/SC (-latomic).
 */
typedef struct {
    /* Contadores de operaciones (incrementados por workers/network) */
    atomic_long     total_dials;        /**< Total de intentos de conexión.  */
    atomic_long     total_timeouts;     /**< Total de timeouts TCP.           */
    atomic_long     total_bytes_sent;   /**< Total de bytes enviados.         */
    atomic_long     total_bytes_recv;   /**< Total de bytes recibidos.        */
    atomic_long     total_frees;        /**< Total de llamadas free() custom. */

    /* Estado instantáneo */
    atomic_int      active_slots;       /**< Slots de conexión activos ahora. */
    atomic_int      active_workers;     /**< Workers corriendo ahora.         */

    /* Acumuladores para cálculo de promedios en el resumen final */
    atomic_long     sum_rss_mb;         /**< Suma de muestras RSS (MB).       */
    atomic_long     sum_dials_per_s;    /**< Suma de muestras dials/s.        */
    atomic_long     sum_frees_per_s;    /**< Suma de muestras frees/s.        */
    atomic_long     sum_traffic_kbps;   /**< Suma de muestras tráfico KB/s.   */
    atomic_long     sample_count;       /**< Cantidad de muestras tomadas.    */
    atomic_long     peak_rss_mb;        /**< Pico máximo de RSS observado.    */
} Metrics;

/* -------------------------------------------------------------------------
 * Estructura de snapshot de telemetría (un período)
 * ------------------------------------------------------------------------- */

/**
 * Snapshot de métricas calculadas para un intervalo de telemetría.
 * Se usa para imprimir una línea de telemetría y acumular promedios.
 */
typedef struct {
    double  rss_mb;         /**< RSS actual en MB.                          */
    double  dials_per_s;    /**< Conexiones intentadas por segundo.         */
    double  frees_per_s;    /**< Frees realizados por segundo.              */
    double  traffic_kbps;   /**< Tráfico total (enviado+recibido) en KB/s.  */
    int     slots;          /**< Slots activos en este instante.            */
} TelemetrySnapshot;

/* -------------------------------------------------------------------------
 * Variables globales exportadas
 * ------------------------------------------------------------------------- */

/** Métricas globales (definidas en telemetry.c, usadas en todos los módulos). */
extern Metrics g_metrics;

/* -------------------------------------------------------------------------
 * Funciones públicas
 * ------------------------------------------------------------------------- */

/**
 * telemetry_init() — Inicializa las métricas globales a cero.
 * Debe llamarse antes de arrancar cualquier worker.
 */
void telemetry_init(void);

/**
 * telemetry_snapshot() — Calcula un snapshot del período actual.
 *
 * Calcula tasas por segundo respecto al snapshot anterior y el tiempo
 * transcurrido. Acumula en los sumadores para el resumen final.
 *
 * @param snap      Puntero al snapshot a llenar.
 * @param prev_dials    Dials al inicio del período.
 * @param prev_frees    Frees al inicio del período.
 * @param prev_bytes    Bytes al inicio del período.
 * @param elapsed_s     Segundos transcurridos.
 */
void telemetry_snapshot(TelemetrySnapshot *snap,
                        long prev_dials, long prev_frees,
                        long prev_bytes, double elapsed_s);

/**
 * telemetry_print() — Imprime una línea de métricas al stdout.
 *
 * Formato: [MIPS METRICS] Slots: X | RAM (RSS): X MB | Frees/s: X |
 *                         Dials/s: X | Timeouts/s: X | Traffic: X KB/s
 *
 * @param snap  Snapshot calculado por telemetry_snapshot().
 */
void telemetry_print(const TelemetrySnapshot *snap);

/**
 * telemetry_save_results() — Guarda el resumen final en benchmark_resultado.txt.
 *
 * Intenta guardar en este orden:
 *   1. host/benchmark_resultado.txt  (volumen Docker montado)
 *   2. out/benchmark_resultado.txt   (directorio de salida alternativo)
 *   3. benchmark_resultado.txt       (directorio actual)
 *
 * @param cfg_slots    Slots configurados.
 * @param cfg_workers  Workers configurados.
 * @param elapsed_s    Duración total del benchmark en segundos.
 */
void telemetry_save_results(int cfg_slots, int cfg_workers, double elapsed_s);

/**
 * telemetry_print_summary() — Imprime la tabla resumen al stdout (stderr).
 *
 * @param cfg_slots    Slots configurados.
 * @param cfg_workers  Workers configurados.
 * @param elapsed_s    Duración total del benchmark en segundos.
 */
void telemetry_print_summary(int cfg_slots, int cfg_workers, double elapsed_s);

#endif /* TELEMETRY_H */


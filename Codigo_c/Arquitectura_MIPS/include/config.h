/*
 * config.h — Configuración global del cliente C (Arquitectura MIPS)
 *
 * Define las constantes de comportamiento del cliente: slots de conexión,
 * workers, intervalos de telemetría, dimensiones de buffers, etc.
 *
 * Compatible con MIPS 32-bit (big-endian), compilado con mips-linux-gnu-gcc.
 * No contiene dependencias de plataforma; es puro C11 estándar.
 *
 * Autoría académica — Simulación benigna para análisis comparativo C vs Go.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <netdb.h>      /* struct addrinfo */

/* -------------------------------------------------------------------------
 * Límites de red y workers
 * ------------------------------------------------------------------------- */

/** Número máximo de slots de conexión concurrentes permitidos. */
#define MAX_SLOTS           256

/** Número máximo de workers (hilos de ataque). */
#define MAX_WORKERS         64

/** Número máximo de intentos de reconexión antes de descartar un slot. */
#define MAX_RETRIES         5

/** Tamaño del buffer de red por conexión (en bytes). */
#define BUFFER_SIZE         4096

/** Tamaño de la pila por hilo worker (128 KB), explícito para MIPS. */
#define WORKER_STACK_SIZE   (128 * 1024)

/* -------------------------------------------------------------------------
 * Intervalos de tiempo (en segundos)
 * ------------------------------------------------------------------------- */

/** Intervalo entre reportes de telemetría (segundos). */
#define TELEMETRY_INTERVAL  2

/** Timeout de conexión TCP (segundos). */
#define CONNECT_TIMEOUT     5

/** Intervalo de idle antes de arrancar workers (segundos). */
#define IDLE_DURATION       5

/* -------------------------------------------------------------------------
 * Puertos y protocolos
 * ------------------------------------------------------------------------- */

/** Puerto del servidor mock por defecto. */
#define DEFAULT_PORT        9090

/** Nombre/IP del servidor mock por defecto. */
#define DEFAULT_HOST        "127.0.0.1"

/* -------------------------------------------------------------------------
 * Telemetría y benchmark
 * ------------------------------------------------------------------------- */

/** Prefijo de líneas de telemetría para identificar la arquitectura. */
#define ARCH_PREFIX         "[MIPS METRICS]"

/** Prefijo de líneas de idle. */
#define IDLE_PREFIX         "[MIPS IDLE]"

/** Nombre de la arquitectura (para el resumen final). */
#define ARCH_NAME           "MIPS_32bit"

/** Encabezado del resumen final consolidado. */
#define SUMMARY_HEADER      "RESUMEN FINAL CONSOLIDADO DEL BENCHMARK (MIPS - C)"

/* -------------------------------------------------------------------------
 * Estructura de configuración en tiempo de ejecución
 * ------------------------------------------------------------------------- */

/**
 * Parámetros de ejecución parseados de argv[] o con valores por defecto.
 * Se pasan como puntero constante a todos los módulos.
 */
typedef struct {
    int         slots;          /**< Slots de conexión concurrentes.       */
    int         workers;        /**< Cantidad de worker threads.            */
    int         stack_kb;       /**< Tamaño de pila por worker (KB).        */
    uint16_t    port;           /**< Puerto TCP del servidor destino.       */
    char        host[256];      /**< Hostname o IP del servidor destino.    */
    struct addrinfo *addr;      /**< Resultado de resolución DNS (addrinfo).*/
} Config;

/* -------------------------------------------------------------------------
 * Funciones públicas
 * ------------------------------------------------------------------------- */

/**
 * config_parse() — Parsea argv[] y llena la estructura Config.
 *
 * Uso esperado: ./cliente_c_mips <slots> <workers> <stack_kb> [host] [port]
 *
 * @param cfg   Puntero a la estructura Config a llenar.
 * @param argc  Cantidad de argumentos de línea de comandos.
 * @param argv  Vector de argumentos de línea de comandos.
 * @return      0 si OK, -1 si error de argumentos.
 */
int config_parse(Config *cfg, int argc, char *argv[]);

/**
 * config_free() — Libera recursos internos de la Config (addrinfo).
 *
 * @param cfg   Puntero a la estructura Config a liberar.
 */
void config_free(Config *cfg);

/**
 * config_print() — Imprime la configuración activa por stdout.
 *
 * @param cfg   Puntero a la Config a imprimir (read-only).
 */
void config_print(const Config *cfg);

#endif /* CONFIG_H */

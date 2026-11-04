/**
 * ============================================================================
 * Proyecto: Cliente Benigno de Referencia en C (Arquitectura ARMV7 / 32-bit ARM)
 * Archivo: include/network.h
 * Descripción: Manejo de red de bajo nivel mediante sockets POSIX no bloqueantes
 *              y multiplexación epoll con timeout estricto (estilo Mirai en ARM).
 * ============================================================================
 */

#ifndef BENCHMARK_NETWORK_H
#define BENCHMARK_NETWORK_H

#include <stddef.h>
#include <stdint.h>
#include "telemetry.h"

/**
 * Realiza un intento de conexión TCP con timeout estricto hacia una dirección IP.
 * Utiliza sockets no bloqueantes (SOCK_NONBLOCK) y epoll_wait() para evitar
 * el bloqueo de hilos y reproducir fielmente estados TCP (ESTABLISHED y SYN_SENT).
 *
 * @param ip Dirección IPv4 destino (ej. "127.0.0.1" o "192.0.2.1").
 * @param port Puerto TCP destino.
 * @param timeout_ms Tiempo máximo de espera en milisegundos.
 * @param payload Datos de prueba a transmitir si la conexión se establece.
 * @param payload_len Longitud del payload.
 * @param t Puntero a la estructura de telemetría para actualización atómica de métricas.
 * @return 0 en caso de conexión exitosa, -1 en caso de error o timeout.
 */
int network_dial_tcp_timeout(const char *ip, int port, int timeout_ms,
                             const uint8_t *payload, size_t payload_len,
                             telemetry_t *t);

#endif /* BENCHMARK_NETWORK_H */


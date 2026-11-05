# Documentación Técnica y Metodológica — Benchmarks en Go (`BotenaGo/`)

Este directorio contiene la implementación de referencia en **Go (Golang)** inspirada en el paradigma arquitectónico de **BotenaGo** (concurrencia masiva mediante *goroutines* sobre el planificador M:N del runtime de Go y gestión automática de memoria con *Garbage Collector*), junto con el sistema automatizado de benchmarking multi-arquitectura sobre contenedores Docker restringidos por *cgroups*.

---

## 1. Componentes del Directorio

| Archivo / Directorio | Descripción Técnica |
| :--- | :--- |
| [`botena.go`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/botena.go) | Código fuente del cliente benigno en Go (retención de 2 MB de heap base, 4.000 goroutines concurrentes, asignación dinámica de 4–8 KB por ciclo, 250 iteraciones de SHA-256 por envío y telemetría atómica cada 100 ms). |
| [`bot_x86_64`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/bot_x86_64) | Ejecutable ELF 64-bit LSB estático para arquitectura `x86_64` / `AMD64` (`1,88 MB`, compilado con `GOARCH=amd64 CGO_ENABLED=0` y `-ldflags="-s -w"`). |
| [`bot_armv7`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/bot_armv7) | Ejecutable ELF 32-bit LSB EABI5 estático para arquitectura `ARMv7` (`1,90 MB`, compilado con `GOARCH=arm GOARM=7 CGO_ENABLED=0` y `-ldflags="-s -w"`). |
| [`bot_mipsle`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/bot_mipsle) | Ejecutable ELF 32-bit LSB MIPS32 rel2 estático para arquitectura `MIPSLE` (`2,16 MB`, compilado con `GOARCH=mipsle CGO_ENABLED=0` y `-ldflags="-s -w"`). |
| [`qemu-arm-static`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/qemu-arm-static) / [`qemu-mipsel-static`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/qemu-mipsel-static) | Emuladores de espacio de usuario estáticos autocontenidos que permiten ejecutar `bot_armv7` y `bot_mipsle` de forma transparente en cualquier host Linux `x86_64` sin requerir configuración manual de `binfmt_misc`. |
| [`ejecutar_prueba_botena.sh`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/ejecutar_prueba_botena.sh) | Script automatizado que compila e inicia el servidor mock `epoll` (`servidor_mock`), aplica restricciones de CPU (`--cpus=1.0`, `GOMAXPROCS=1`) y RAM (`--memory`, `--memory-swap`), y registra las series temporales en CSV. |
| [`BenchmarksGo/`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/BenchmarksGo) | Directorio de salida organizado por `<binario>-<memoria>m/<binario>-<memoria>m-log-num-<X>.csv`. |

---

## 2. Uso del Script de Benchmarking

```bash
cd BotenaGo
./ejecutar_prueba_botena.sh "<binario>" "<limite_memoria>" [duracion_segundos]
```

### Ejemplos de ejecución
```bash
# Prueba en arquitectura AMD64 (x86_64) con 32, 64, 128 y 256 MB
./ejecutar_prueba_botena.sh bot_x86_64 32m
./ejecutar_prueba_botena.sh bot_x86_64 64m
./ejecutar_prueba_botena.sh bot_x86_64 128m
./ejecutar_prueba_botena.sh bot_x86_64 256m

# Prueba en arquitectura ARMv7 (bajo emulación QEMU user-mode en host x86_64)
./ejecutar_prueba_botena.sh bot_armv7 32m
./ejecutar_prueba_botena.sh bot_armv7 64m
./ejecutar_prueba_botena.sh bot_armv7 128m
./ejecutar_prueba_botena.sh bot_armv7 256m

# Prueba en arquitectura MIPS32 Little-Endian (bajo emulación QEMU user-mode en host x86_64)
./ejecutar_prueba_botena.sh bot_mipsle 32m
./ejecutar_prueba_botena.sh bot_mipsle 64m
./ejecutar_prueba_botena.sh bot_mipsle 128m
./ejecutar_prueba_botena.sh bot_mipsle 256m
```

---

## 3. Esquema Estándar de Archivos CSV

Todos los archivos CSV generados en `BenchmarksGo/` respetan exactamente el mismo esquema de 11 columnas que `Benchmarks_c/`:

```csv
timestamp,elapsed_seconds,architecture,memory_limit_mb,ram_used_mb,ram_max_mb,network_rx_mb,network_tx_mb,network_total_mb,uca_active,uca_max
```

1. **`timestamp`**: Fecha y hora local de la muestra (`YYYY-MM-DD HH:MM:SS`).
2. **`elapsed_seconds`**: Tiempo transcurrido desde el inicio de la prueba (en segundos).
3. **`architecture`**: Arquitectura evaluada (`AMD64`, `ARMV7` o `MIPS`).
4. **`memory_limit_mb`**: Tope estricto de memoria RAM y swap impuesto al cgroup del contenedor (`32`, `64`, `128`, `256`, `512`).
5. **`ram_used_mb`**: Memoria RAM utilizada por el contenedor en el instante de muestreo (MB).
6. **`ram_max_mb`**: Pico máximo de memoria RAM registrado hasta ese instante (MB).
7. **`network_rx_mb`**: Tráfico acumulado recibido desde `servidor_mock` (`ACK_OK\n` de 7 bytes por transacción) en MB.
8. **`network_tx_mb`**: Tráfico acumulado enviado hacia `servidor_mock` (payload de 48 bytes por transacción) en MB.
9. **`network_total_mb`**: Suma acumulada `network_rx_mb + network_tx_mb` en MB.
10. **`uca_active`**: Cantidad de goroutines concurrentes activas en el ciclo de trabajo/red en el instante de muestreo.
11. **`uca_max`**: Máximo histórico simultáneo de goroutines activas alcanzado durante la prueba.

---

## 4. Resumen Empírico de Resultados en Go (`UCA = 64`, `UnreachableRatio = 0%`, `--cpus="1.0"`, `servidor_mock`)

Al homologar exactamente los parámetros de carga con las pruebas de C (`BENCHMARK_UCA=64`, `BENCHMARK_UNREACHABLE_RATIO=0`, `--cpus="1.0"`, `servidor_mock` y muestreo directo desde `/sys/fs/cgroup` + `/proc/<pid>/statm` a `1.0 Hz`), se obtienen los siguientes resultados:

| Binario (`ARCH`) | Límite RAM | Muestras (`N`) | RAM Media ± DE (`MB`) | RAM Pico (`MB`) | Red RX L7 (`MB`) | Red TX L7 (`MB`) | Red Total L7 (`MB`) | UCA Media (Máx) | Estado (`UCA=64`) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`bot_x86_64` (`AMD64`)** | **32 MB** | 15 | 16.57 ± 0.57 | 17.52 | 0.3973 | 2.7240 | 3.1213 | 37.0 (60) | Estable |
| **`bot_x86_64` (`AMD64`)** | **64 MB** | 15 | 16.11 ± 0.72 | 17.30 | 0.4025 | 2.7602 | 3.1627 | 42.6 (64) | Estable |
| **`bot_x86_64` (`AMD64`)** | **128 MB** | 15 | 16.00 ± 0.55 | 16.96 | 0.4196 | 2.8771 | 3.2967 | 44.1 (64) | Estable |
| **`bot_x86_64` (`AMD64`)** | **256 MB** | 15 | 14.75 ± 0.92 | 16.29 | 0.4253 | 2.9165 | 3.3418 | 42.4 (64) | Estable |
| **`bot_armv7` (`ARMV7`)** | **32 MB** | 15 | 32.35 ± 0.30 | 32.98 | 0.0249 | 0.1704 | 0.1953 | 44.0 (64) | Al límite de saturación |
| **`bot_armv7` (`ARMV7`)** | **64 MB** | 15 | 33.29 ± 2.07 | 35.70 | 0.0311 | 0.2134 | 0.2445 | 44.9 (61) | Estable |
| **`bot_armv7` (`ARMV7`)** | **128 MB** | 15 | 34.87 ± 2.27 | 37.46 | 0.0293 | 0.2009 | 0.2302 | 38.3 (64) | Estable |
| **`bot_armv7` (`ARMV7`)** | **256 MB** | 15 | 35.05 ± 0.80 | 35.90 | 0.0310 | 0.2123 | 0.2433 | 48.3 (61) | Estable |
| **`bot_mipsle` (`MIPS`)** | **32 MB** | 15 | 26.01 ± 1.44 | 28.37 | 0.0378 | 0.2594 | 0.2972 | 43.3 (63) | Estable |
| **`bot_mipsle` (`MIPS`)** | **64 MB** | 15 | 27.63 ± 0.91 | 28.12 | 0.0388 | 0.2661 | 0.3049 | 38.4 (63) | Estable |
| **`bot_mipsle` (`MIPS`)** | **128 MB** | 15 | 27.52 ± 1.18 | 28.34 | 0.0392 | 0.2691 | 0.3083 | 39.1 (61) | Estable |
| **`bot_mipsle` (`MIPS`)** | **256 MB** | 15 | 27.01 ± 2.07 | 28.37 | 0.0361 | 0.2477 | 0.2838 | 43.6 (61) | Estable |

> **Nota para pruebas de estrés de concurrencia masiva (OOM Killer):** Si se desea evaluar el régimen de estrés masivo con `4000` goroutines (donde `bot_x86_64` colapsa por OOM en `32m`, `64m` y `128m` y se estabiliza en `256m` con `~136.9 MB`), basta con ejecutar:
> ```bash
> BENCHMARK_UCA=4000 ./ejecutar_prueba_botena.sh bot_x86_64 32m
> ```



# Guía de Benchmarking y Ejecución Automatizada para las Implementaciones en C

---

## A. Introducción

### 1. Objetivo del Benchmarking
El presente sistema de benchmarking tiene como propósito evaluar de forma empírica, automatizada y reproducible el comportamiento de las implementaciones en lenguaje **C (estándar C11)** dentro de contenedores Docker con restricciones estrictas de recursos que emulan dispositivos IoT reales (32 MB, 64 MB y 128 MB de memoria RAM).

### 2. Alcance de las Pruebas
Este sistema abarca **exclusivamente las implementaciones desarrolladas en C** para las tres arquitecturas de procesador del estudio:
- **AMD64 (`x86_64`)**: Ejecución nativa de 64 bits.
- **ARMV7 (`armhf` 32 bits)**: Arquitectura común en gateways, cámaras IP y SBCs IoT.
- **MIPS (`mips` 32 bits Big-Endian)**: Arquitectura típica de routers domésticos y CPEs embebidos (ej. SoCs Atheros/Realtek).

Los códigos, scripts y configuraciones correspondientes a Go permanecen completamente independientes e inalterados.

### 3. Diferencia entre los Simuladores C y las Muestras Originales de Malware
Los programas evaluados en este repositorio son **simuladores académicos completamente benignos**:
- **Sin rutinas ofensivas**: No contienen vectores de explotación, fuerza bruta de credenciales, escaneo de redes externas ni mecanismos de propagación o persistencia.
- **Tráfico estrictamente local y aislado**: Toda actividad TCP se dirige únicamente a un contenedor sumidero local (`servidor_mock` en el puerto `9090`) sobre una red puente interna de Docker (`--internal`), sin posibilidad técnica de enrutar paquetes hacia Internet ni hacia dispositivos externos.
- **Fidelidad estructural**: Conservan los patrones arquitectónicos de concurrencia y gestión manual de memoria (`malloc`/`free`, hilos POSIX con pila reducida de 128 KB, sockets no bloqueantes con `epoll` y cómputo intensivo SHA-256) necesarios para medir la huella real del lenguaje C en entornos restringidos.

### 4. Las Tres Métricas Principales del Estudio
Todas las pruebas giran exclusivamente alrededor de tres parámetros fundamentales:

1. **Métrica 1 — Consumo de Memoria RAM (`MB`)**:
   Registra la evolución temporal de la memoria RAM efectivamente utilizada por el contenedor (`ram_used_mb`) y el pico máximo alcanzado (`ram_max_mb`), distinguiendo claramente entre el consumo real y el límite asignado al contenedor (`memory_limit_mb`). Permite calcular el consumo promedio y máximo de cada ejecución.
2. **Métrica 2 — Consumo de Red (`MB`)**:
   Registra el volumen acumulado de datos recibidos (`network_rx_mb`), datos enviados (`network_tx_mb`) y el tráfico total transferido (`network_total_mb`) desde la perspectiva de la interfaz de red del contenedor cliente en su comunicación exclusiva con `servidor_mock`.
3. **Métrica 3 — Unidades Concurrentes de Ataque (`UCA`)**:
   Representa las unidades de trabajo benignas y simuladas que ejecutan actividades concurrentes dentro del cliente C (trabajadores POSIX / slots de conexión concurrentes activos). Mide tanto la cantidad instantánea de unidades activas (`uca_active`) como el máximo simultáneo observado durante la prueba (`uca_max`).

---

## B. Requisitos y Dependencias

### Dependencias del Sistema
Para ejecutar las pruebas de manera automatizada se requiere:
- **Sistema Operativo**: Ubuntu 22.04 LTS o Ubuntu 24.04 LTS (arquitectura `x86_64`).
- **Docker Engine**: Versión 20.10 o superior (con permisos para el usuario actual en el grupo `docker`).
- **Bash**: Intérprete de comandos estándar (`/usr/bin/env bash`) junto con utilidades base de GNU (`awk`, `sed`, `grep`, `flock` de `util-linux`).
- **GCC y Make** *(para compilación local opcional)*:
  - `build-essential`, `gcc`, `make`, `libc6-dev`
  - Toolchains de compilación cruzada (utilizadas automáticamente dentro de los Dockerfiles multi-etapa): `gcc-arm-linux-gnueabihf`, `gcc-mips-linux-gnu`.
- **Soporte de emulación multi-arquitectura (QEMU binfmt_misc)**:
  - Para ejecutar binarios `ARMV7` y `MIPS` dentro de Docker sobre un host `x86_64`, el kernel utiliza `binfmt_misc` con `tonistiigi/binfmt` (el script `ejecutar_prueba_c.sh` lo verifica y registra automáticamente si no está activo).

### Instrucciones de Instalación en Ubuntu
Ejecute los siguientes comandos en una terminal de Ubuntu para instalar todas las dependencias necesarias:

```bash
# 1. Actualizar índice de paquetes e instalar herramientas base y compiladores
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    gcc \
    make \
    binutils \
    util-linux \
     gawk \
    ca-certificates \
    curl

# 2. Instalar Docker Engine (si aún no está instalado)
sudo apt-get install -y docker.io

# 3. Habilitar el servicio Docker y agregar su usuario al grupo docker
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
newgrp docker

# 4. (Opcional) Instalar toolchains de compilación cruzada en el host
sudo apt-get install -y \
    gcc-arm-linux-gnueabihf \
    libc6-dev-armhf-cross \
    gcc-mips-linux-gnu \
    libc6-dev-mips-cross
```

---

## C. Organización del Repositorio

La estructura del repositorio relacionada con el sistema de benchmarking en C es la siguiente:

```text
REPOSITORIO/
│
├── Benchmarks_c/                         # Directorio principal de resultados en C
│   ├── AMD64_32mb/
│   │   ├── AMD64_32mb_log_num_1.csv
│   │   └── AMD64_32mb_log_num_2.csv
│   ├── AMD64_64mb/
│   ├── AMD64_128mb/
│   ├── ARMV7_32mb/
│   ├── ARMV7_64mb/
│   ├── ARMV7_128mb/
│   ├── MIPS_32mb/
│   ├── MIPS_64mb/
│   ├── MIPS_128mb/
│   └── README_benchmarks_c.md            # Esta documentación
│
├── Codigo_c/                             # Implementaciones modulares en C por arquitectura
│   ├── Arquitectura_AMD64/               # Código fuente, Makefile y Dockerfile para x86_64
│   │   ├── Dockerfile
│   │   ├── Makefile
│   │   ├── include/                      # Cabeceras (config.h, telemetry.h, worker.h, etc.)
│   │   ├── src/                          # Código fuente C (main.c, telemetry.c, worker.c, etc.)
│   │   └── bin/                          # Binarios compilados (cliente_c, cliente_c_stripped)
│   │
│   ├── Arquitectura_ARMV7/               # Código fuente, Makefile y Dockerfile para ARMv7 32-bit
│   │   ├── Dockerfile
│   │   ├── Makefile
│   │   ├── include/
│   │   ├── src/
│   │   └── bin/                          # Binarios (cliente_c_armv7, cliente_c_armv7_stripped)
│   │
│   └── Arquitectura_MIPS/                # Código fuente, Makefile y Dockerfile para MIPS 32-bit
│       ├── Dockerfile
│       ├── Makefile
│       ├── include/
│       ├── src/
│       └── bin/                          # Binarios (cliente_c_mips, cliente_c_mips_stripped)
│
├── servidor_mock.c                       # Código fuente C del servidor TCP sumidero (puerto 9090)
├── Dockerfile.mock                       # Dockerfile para construir la imagen servidor_mock_img
└── ejecutar_prueba_c.sh                  # Script maestro de ejecución y benchmarking automatizado
```

---

## D. Ejecución de Pruebas

Para ejecutar cualquier prueba, sitúese en la raíz del repositorio y utilice **exclusivamente** el script `ejecutar_prueba_c.sh`:

```bash
chmod +x ejecutar_prueba_c.sh
./ejecutar_prueba_c.sh "nombre_del_archivo_del_bot" "cantidad_de_memoria"
```

### Ejemplos Reales de Ejecución

1. **Ejecutar el cliente C de arquitectura AMD64 con límite de 32 MB de RAM:**
   ```bash
   ./ejecutar_prueba_c.sh "bot_amd64" "32"
   ```
   *(Genera automáticamente: `Benchmarks_c/AMD64_32mb/AMD64_32mb_log_num_1.csv`)*

2. **Ejecutar el cliente C de arquitectura MIPS con límite de 64 MB de RAM:**
   ```bash
   ./ejecutar_prueba_c.sh "bot_mips" "64"
   ```
   *(Genera automáticamente: `Benchmarks_c/MIPS_64mb/MIPS_64mb_log_num_1.csv`)*

3. **Ejecutar el cliente C de arquitectura ARMV7 con límite de 128 MB de RAM:**
   ```bash
   ./ejecutar_prueba_c.sh "bot_armv7" "128"
   ```
   *(Genera automáticamente: `Benchmarks_c/ARMV7_128mb/ARMV7_128mb_log_num_1.csv`)*

> **Nota sobre identificadores aceptados:** Además de `"bot_amd64"`, `"bot_armv7"` y `"bot_mips"`, el primer argumento también acepta los nombres de binarios compilados (`"cliente_c"`, `"cliente_c_armv7"`, `"cliente_c_mips"`) o el nombre de la arquitectura (`"amd64"`, `"armv7"`, `"mips"`). Los límites de memoria válidos son potencias de 2 hasta $2^9$ (`"8"`, `"16"`, `"32"`, `"64"`, `"128"`, `"256"`, `"512"` MB).

---

## E. Funcionamiento Interno

Cuando el usuario ejecuta `./ejecutar_prueba_c.sh`, el script realiza de forma totalmente automática las siguientes etapas:

1. **Validación y Control de Concurrencia**:
   - Verifica que se hayan suministrado los dos argumentos obligatorios.
   - Comprueba que la implementación C solicitada exista en `Codigo_c/Arquitectura_<ARCH>/` y que el límite de memoria sea un valor permitido (`32`, `64` o `128` MB).
   - Adquiere un cerrojo exclusivo (`flock` sobre `/tmp/ejecutar_prueba_c.lock`) para impedir que dos pruebas se ejecuten simultáneamente y compitan por CPU o red.
2. **Determinación de Almacenamiento Secuencial**:
   - Crea la carpeta `Benchmarks_c/<ARCH>_<MEM>mb/` si aún no existe.
   - Inspecciona los archivos existentes con patrón `<ARCH>_<MEM>mb_log_num_X.csv` y calcula el siguiente entero `X` disponible (`1, 2, 3...`), garantizando que jamás se sobrescriba un resultado previo.
3. **Preparación de Docker y Aislamiento de Red**:
   - Crea la red puente interna `red_simulacion` con la bandera `--internal` de Docker, bloqueando cualquier salida a Internet y permitiendo únicamente la comunicación entre el contenedor cliente y `servidor_mock`.
   - Si la arquitectura es `ARMV7` o `MIPS` sobre un host `x86_64`, verifica que el intérprete `binfmt_misc` de QEMU esté activo.
   - Construye las imágenes `servidor_mock_img:latest` y `cliente_c_<arch>_img:latest`.
4. **Inicio y Verificación del Servidor Mock**:
   - Inicia el contenedor `servidor_mock` conectado a `red_simulacion` escuchando en el puerto TCP `9090` y verifica mediante `docker inspect` que su estado sea `Running`.
5. **Ejecución Restringida del Cliente C**:
   - Lanza el contenedor del cliente aplicando:
     - `--memory="<MEM>m" --memory-swap="<MEM>m"` (límite estricto de RAM física sin paginación a swap).
     - `--cpus="1.0"` (asignación constante de CPU para todas las pruebas).
     - Sin `--privileged` y sin `--network=host`.
     - Montaje exclusivo de la subcarpeta de destino (`-v ".../Benchmarks_c/<ARCH>_<MEM>mb:/app/benchmarks"`).
6. **Recopilación de Métricas y Separación del Tráfico de Instrumentación**:
   - Durante la prueba (por defecto 30 segundos de carga activa, configurable mediante la variable de entorno `BENCHMARK_DURATION`), el hilo de telemetría del cliente C muestrea cada **1 segundo**:
     - **RAM**: Lee el consumo efectivo del grupo de control del contenedor (`/sys/fs/cgroup/memory.current` descontando caché inactiva) y el RSS físico del proceso (`/proc/self/statm`).
     - **Red**: Lee los contadores acumulados de bytes recibidos (`RX`) y enviados (`TX`) de la interfaz de red del contenedor (`/proc/net/dev` en `eth0`, excluyendo loopback) y de los sockets activos.
     - **UCA**: Lee mediante operaciones atómicas C11 (`memory_order_seq_cst` / LL-SC) el contador de unidades concurrentes activas (`uca_active`) y su máximo histórico (`uca_max`).
   - **Separación estricta entre instrumentación y tráfico de red**: Las observaciones se escriben directamente en el archivo CSV a través del descriptor de archivo local montado en `/app/benchmarks` (con `fflush()` inmediato en cada fila) y el estado en vivo se imprime por `stdout`. De esta manera, **la telemetría genera exactamente 0 bytes de tráfico de red** y no distorsiona en absoluto los contadores `network_rx_mb` ni `network_tx_mb` de la interfaz `eth0`.
7. **Limpieza y Resumen**:
   - Al finalizar la ejecución (o si el usuario presiona `Ctrl+C`), detiene y elimina los contenedores `cliente_c_*` y `servidor_mock`, cierra el archivo CSV conservando todas las muestras registradas y muestra en pantalla el resumen estadístico junto con la ruta exacta del CSV.

---

## F. Interpretación de Resultados

Cada archivo CSV generado utiliza el siguiente esquema uniforme de 11 columnas:

```csv
timestamp,elapsed_seconds,architecture,memory_limit_mb,ram_used_mb,ram_max_mb,network_rx_mb,network_tx_mb,network_total_mb,uca_active,uca_max
```

### Detalle de Columnas

| Columna | Unidad / Formato | Descripción e Interpretación |
| :--- | :--- | :--- |
| `timestamp` | `YYYY-MM-DD HH:MM:SS` | Fecha y hora exacta en que se tomó la observación temporal. |
| `elapsed_seconds` | Segundos (`0.00`) | Tiempo transcurrido desde el inicio de la fase activa de carga. |
| `architecture` | Texto (`AMD64`, `ARMV7`, `MIPS`) | Arquitectura del binario C evaluado. |
| `memory_limit_mb` | `MB` (entero) | Límite máximo de RAM impuesto por Docker al contenedor (`32`, `64` o `128`). |
| `ram_used_mb` | `MB` (`0.00`) | Memoria RAM efectivamente utilizada por el contenedor en ese segundo. Para obtener el **consumo promedio de RAM**, se promedia esta columna a lo largo de todas las filas del CSV. |
| `ram_max_mb` | `MB` (`0.00`) | Valor máximo de RAM observado desde el inicio de la prueba hasta ese instante (monótonamente creciente). El valor de la última fila representa el **pico máximo de RAM** de toda la prueba. |
| `network_rx_mb` | `MB` (`0.0000`) | Megabytes recibidos acumulados desde el inicio de la prueba (respuestas `ACK_OK` y paquetes TCP provenientes de `servidor_mock`). |
| `network_tx_mb` | `MB` (`0.0000`) | Megabytes enviados acumulados desde el inicio de la prueba (payloads SHA-256 y segmentos TCP transmitidos hacia `servidor_mock`). |
| `network_total_mb` | `MB` (`0.0000`) | Suma acumulada total transferida (`network_rx_mb + network_tx_mb`). |
| `uca_active` | Unidades (entero) | Cantidad de Unidades Concurrentes de Ataque (trabajadores/slots) activas ejecutando tareas en el instante del muestreo. |
| `uca_max` | Unidades (entero) | Cantidad máxima de unidades concurrentes activas simultáneamente observada hasta ese instante. |

---

## G. Reproducibilidad

### Cómo Repetir Pruebas sin Sobrescribir Resultados
Para realizar múltiples repeticiones experimentales del mismo escenario (por ejemplo, 5 iteraciones de `MIPS` con `32 MB`), simplemente ejecute el mismo comando consecutivamente:

```bash
./ejecutar_prueba_c.sh "bot_mips" "32"
./ejecutar_prueba_c.sh "bot_mips" "32"
./ejecutar_prueba_c.sh "bot_mips" "32"
```

El script detectará automáticamente los archivos previos y creará:
- `Benchmarks_c/MIPS_32mb/MIPS_32mb_log_num_1.csv`
- `Benchmarks_c/MIPS_32mb/MIPS_32mb_log_num_2.csv`
- `Benchmarks_c/MIPS_32mb/MIPS_32mb_log_num_3.csv`

### Condiciones Constantes para Comparación Justa
Todas las ejecuciones mantienen constantes los parámetros metodológicos:
- **Asignación de CPU**: `1.0` core (`--cpus="1.0"`).
- **Duración de carga**: `30` segundos de fase activa (`BENCHMARK_DURATION=30`) precedidos por `2` segundos de medición base en reposo (`BENCHMARK_IDLE=2`).
- **Carga de trabajo**: `2 MB` de heap estático retenido y `64` unidades concurrentes objetivo (`BENCHMARK_UCA=64`) atacando exclusivamente a `servidor_mock:9090`.
- **Ejecución exclusiva**: El bloqueo mediante `flock` garantiza que dos pruebas nunca compitan por recursos del host.

---

## H. Limitaciones

1. **Diferencias entre Simulación Benigna y Malware Real**:
   Los clientes C implementan una carga sintética controlada (cálculo SHA-256, reserva/liberación dinámica y conexiones TCP sobre red local). No realizan escaneo masivo de subredes públicas ni ataques volumétricos reales.
2. **Emulación QEMU en Arquitecturas Cruzadas (`ARMV7` y `MIPS`)**:
   Cuando las pruebas de `ARMV7` y `MIPS` se ejecutan sobre un procesador anfitrión `x86_64`, Docker utiliza emulación en espacio de usuario (`qemu-arm-static` / `qemu-mips-static` vía `binfmt_misc`). La traducción dinámica de instrucciones de QEMU introduce una sobrecarga adicional tanto en memoria RSS (el propio intérprete QEMU reside en el espacio de memoria del contenedor) como en ciclos de CPU respecto a un procesador físico nativo ARMv7 o MIPS. Por ello, los resultados de `ARMV7` y `MIPS` obtenidos bajo emulación deben analizarse reconociendo explícitamente la capa de traducción QEMU y no como equivalentes directos a silicio nativo.
3. **Granularidad Temporal y Sobrecarga de Contenedores**:
   El muestreo periódico de 1 segundo captura la evolución macroscópica de RAM, red y UCA con impacto despreciable sobre el rendimiento, aunque picos transitorios de sub-milisegundos entre un `malloc()` y su `free()` inmediato ocurren a mayor frecuencia que el intervalo de muestreo.


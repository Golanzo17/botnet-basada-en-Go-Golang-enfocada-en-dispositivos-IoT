# Cliente C — Arquitectura MIPS 32-bit (Big-Endian)

Implementación en C11 del cliente de simulación de botnet para **MIPS 32-bit big-endian**, replicando el comportamiento del cliente original en Go estudiado en el paper académico.

## Contexto Arquitectural

### ¿Por qué MIPS?

MIPS (Microprocessor without Interlocked Pipeline Stages) fue una de las arquitecturas más presentes en dispositivos IoT vulnerables. El botnet **Mirai** (2016) fue el primer gran botnet en apuntar específicamente a dispositivos MIPS:

- **Routers domésticos**: TP-Link TL-WR740N, TL-WR841N (Atheros AR9331)
- **Dispositivos de red**: Netgear R7000, R6400
- **DVRs y cámaras IP**: Muchos basados en MIPS con firmware Linux embebido
- **NAS de bajo costo**: Algunos WD My Cloud, QNAP

Mirai apuntaba específicamente a 10 arquitecturas, siendo **MIPS** y **MIPSEL** (little-endian) las más prevalentes en su botnet.

### Características de MIPS 32-bit

| Característica | Valor |
|---|---|
| Ancho de palabra | 32 bits |
| Punteros | 32 bits (4 bytes) |
| Endianness | Big-endian (MIPS), little-endian (MIPSEL) |
| ABI estándar | O32 |
| FPU | Opcional (usamos `-msoft-float`) |
| Instrucciones atómicas | LL/SC (Load-Linked/Store-Conditional) |
| ISA | MIPS32 Release 1 (compatible con toda la gama) |

### MIPS vs x86 y ARM para IoT

```
Arquitectura  | Endian       | sizeof(ptr) | Atómicos   | FPU
MIPS 32-bit   | Big-endian   | 4 bytes     | LL/SC      | Soft (típico)
ARMv7         | Little-endian| 4 bytes     | LDREX/STREX| VFPv3
AMD64/x86_64  | Little-endian| 8 bytes     | LOCK CMPXCHG| SSE2
```

## Estructura del Código

```
Arquitectura_MIPS/
├── include/
│   ├── config.h       ← Constantes y struct Config
│   ├── sha256.h       ← Interfaz SHA-256 (puro C11, sin dependencias)
│   ├── telemetry.h    ← Métricas atómicas y benchmark
│   ├── memory.h       ← Allocator custom con seguimiento de frees
│   ├── network.h      ← Pool de conexiones TCP + epoll
│   └── worker.h       ← Pool de workers pthread
├── src/
│   ├── config.c       ← Parseo de args + resolución DNS
│   ├── sha256.c       ← SHA-256 portable (big/little endian)
│   ├── telemetry.c    ← Recolección, print y guardado de métricas
│   ├── memory.c       ← malloc/free wrapper + lectura RSS
│   ├── network.c      ← Sockets no-bloqueantes + epoll
│   ├── worker.c       ← pthread workers con loop epoll
│   └── main.c         ← Orquestador: idle → heap → workers → resumen
├── Makefile           ← Cross-compile con mips-linux-gnu-gcc
├── Dockerfile         ← Multi-stage: build en ubuntu + runtime en debian
├── .gitignore
└── README.md
```

## Compilación

### Requisitos (Ubuntu/Debian)

```bash
# Instalar toolchain de cross-compilación MIPS
sudo apt-get install gcc-mips-linux-gnu binutils-mips-linux-gnu libc6-dev-mips-cross
```

### Cross-compilar para MIPS (producción)

```bash
# Desde la carpeta Arquitectura_MIPS/
make clean && make
```

Genera:
- `bin/cliente_c_mips` — binario con símbolos de debug
- `bin/cliente_c_mips_stripped` — binario sin símbolos (más pequeño para IoT)

### Compilar nativamente en x86 (para tests)

```bash
make CROSS_COMPILE= ARCH_FLAGS="" CC=gcc STRIP=strip all
```

> [!NOTE]
> La compilación nativa en x86 compila el código C11 correctamente pero el binario resultante corre en x86. Para ejecutar el binario MIPS real en x86 se necesita QEMU.

### Verificar la arquitectura del binario

```bash
file bin/cliente_c_mips
# Salida esperada:
# bin/cliente_c_mips: ELF 32-bit MSB executable, MIPS, MIPS32 version 1 (SYSV), ...
```

## Ejecución con Docker

### Paso 0: Habilitar emulación QEMU MIPS (solo la primera vez)

```bash
docker run --privileged --rm tonistiigi/binfmt --install mips
```

### Paso 1: Build de la imagen

```bash
docker build -t cliente_c_mips .
```

### Paso 2: Crear red Docker (si no existe)

```bash
docker network create red_simulacion
```

### Paso 3: Lanzar servidor mock

```bash
docker run --rm -d \
    --name servidor_mock \
    --network red_simulacion \
    servidor_mock_img
```

### Paso 4: Lanzar cliente MIPS

```bash
docker run --rm -it \
    --name cliente_mips \
    --network red_simulacion \
    --memory=32m --memory-swap=32m \
    -v "$(pwd):/app/host" \
    cliente_c_mips 5 2 64 servidor_mock
```

Parámetros: `<slots> <workers> <stack_kb> <host> [port]`

### Paso 5: Detener y obtener resultados

Presionar `Ctrl+C`. El cliente imprime el resumen y guarda:
```
./benchmark_resultado.txt  ← en tu directorio actual (via volumen Docker)
```

## Telemetría en tiempo real

Cada `TELEMETRY_INTERVAL` segundos (default: 2s) se imprime:

```
[MIPS METRICS] Slots: 5 | RAM (RSS): 2.34 MB | Frees/s: 142 | Dials/s: 87 | Timeouts/s: 3 | Traffic: 1.23 KB/s
```

## Detalles de Implementación MIPS

### SHA-256 y endianness

MIPS es **big-endian por defecto**: el byte más significativo está en la dirección más baja. El SHA-256 internamente trabaja en big-endian (el estándar SHA es big-endian). Esto significa:

- En MIPS big-endian: **SHA-256 es perfectamente eficiente** sin conversiones
- La implementación usa macros `BE32()` que extraen bytes uno a uno (portable)
- Funciona correctamente en big-endian y little-endian sin `#ifdef`

### Operaciones Atómicas con LL/SC

MIPS implementa atomicidad con instrucciones **Load-Linked / Store-Conditional**:

```asm
# Ejemplo conceptual de atomic_fetch_add en MIPS32
loop:
    ll    $t0, offset($s0)   # Load-Linked (marca la línea de caché)
    addu  $t0, $t0, $a0      # Sumar el valor
    sc    $t0, offset($s0)   # Store-Conditional (falla si hubo write-conflict)
    beqz  $t0, loop          # Reintentar si SC falló
```

El compilador genera esto cuando usas `atomic_fetch_add()` de `<stdatomic.h>`. La opción **`-latomic`** es necesaria para operaciones de 64-bit sobre hardware de 32-bit.

### Soft-Float y compatibilidad IoT

Los routers MIPS embebidos frecuentemente **no tienen FPU hardware**. Con `-msoft-float`:
- Las operaciones `double` en telemetría se emulan en software
- El binario es compatible con **toda la gama de MIPS**, con y sin FPU
- Ligero overhead en telemetría (cálculos `double`), imperceptible en el benchmark

### Stack por worker: 128 KB

```c
pthread_attr_setstacksize(&attr, WORKER_STACK_SIZE);  // 128 KB
```

En MIPS O32 ABI, el stack crece hacia abajo. 128 KB es suficiente para:
- Frame de `worker_thread()`: ~1 KB
- Frame de `net_pool_tick()` + `epoll_wait()`: ~2 KB  
- Resto de cadena de llamadas: ~5 KB
- Margen de seguridad: ~120 KB

## Flags de Compilación Explicados

```makefile
-std=c11          → Estándar C11 (obligatorio para <stdatomic.h>)
-Wall -Wextra     → Todos los warnings habilitados
-Wpedantic        → Estricto conformance con el estándar
-O2               → Optimización nivel 2 (buena relación velocidad/debuggabilidad)
-pthread          → Habilita soporte de hilos POSIX
-march=mips32     → Genera código para MIPS32 (compatible con toda la gama)
-mabi=32          → ABI O32 (estándar para Linux MIPS 32-bit)
-msoft-float      → Emulación FPU en software (máxima compatibilidad)
-Iinclude         → Headers en ./include/
-D_GNU_SOURCE     → Habilita extensiones GNU (epoll, clock_gettime, etc.)
-latomic          → Librería para atómicos de 64-bit en hardware 32-bit
```

## Comparación con la implementación Go

| Aspecto | Go (original) | C (MIPS) |
|---|---|---|
| Gestión de memoria | GC automático | Manual (malloc/free) |
| Concurrencia | Goroutines | POSIX pthreads |
| Multiplex I/O | Scheduler Go | epoll (Linux) |
| Atómicos | sync/atomic | C11 stdatomic + LL/SC |
| RAM esperada | ~8-15 MB | ~2-5 MB |
| Footprint binario | ~6 MB | ~50 KB (stripped) |
| SHA-256 | stdlib | Autocontenida (C11) |
| Endianness | Nativo host | Portable (big/little) |

## Notas Académicas

Este código simula el **patrón de conectividad** de un botnet cliente tipo Mirai, pero:

- ✅ No realiza ningún ataque real
- ✅ No tiene capacidad de propagación
- ✅ El servidor destino es siempre el mock local
- ✅ El objetivo es **medir el overhead de recursos** (RAM, CPU, red)
- ✅ Comparar C vs Go en condiciones equivalentes de hardware embebido


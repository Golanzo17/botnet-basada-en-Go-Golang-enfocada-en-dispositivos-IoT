# Implementación Benigna en C para AMD64 (x86-64 Linux)
## Banco de Pruebas Empírico: Arquitectura de Referencia IoT (Inspiración Mirai)

Este submódulo contiene la implementación en lenguaje **C (estándar C11)** orientada específicamente a la arquitectura **AMD64 (x86-64) sobre Linux**, homóloga en comportamiento, parámetros y métricas al cliente de pruebas desarrollado en **Go (`cliente.go`)**.

El objetivo de esta implementación es servir de base experimental para el análisis comparativo de huella de memoria RAM (RSS), uso de CPU, tamaño de binario y comportamiento bajo límites estrictos de memoria en contenedores Docker (32 MB, 64 MB y 128 MB).

> **Aviso Académico:** Este desarrollo es estrictamente benigno e instrumental. No implementa mecanismos de ataque, denegación de servicio, infección, persistencia ni evasión. Las conexiones de red se dirigen exclusivamente al servidor local de pruebas (`servidor_mock` en `127.0.0.1:9090`) y a direcciones del bloque de documentación RFC 5737 (`192.0.2.1:9999`) para emular estados de espera TCP (`SYN_SENT`) de manera segura y controlada.

---

## 1. Organización del Proyecto

El código fuente sigue una arquitectura modular en C, con separación de responsabilidades, control riguroso de límites de memoria y compilación libre de dependencias externas:

```text
Codigo_c/Arquitectura_AMD64/
├── Dockerfile                  # Receta de empaquetado y ejecución sobre Ubuntu 22.04 LTS (AMD64)
├── Makefile                    # Reglas de compilación para GCC (all, native, stripped, static, clean)
├── README.md                   # Documentación técnica, decisiones de diseño y guía de uso
├── include/                    # Cabeceras públicas (.h)
│   ├── config.h                # Definición de parámetros del benchmark y parseo de argumentos CLI
│   ├── memory.h                # Gestión y retención de memoria en Heap (homólogo a Go allocateStaticHeap)
│   ├── network.h               # Sockets POSIX no bloqueantes, multiplexación epoll y rotación IP
│   ├── sha256.h                # Implementación autónoma de SHA-256 (FIPS 180-4, sin OpenSSL)
│   ├── telemetry.h             # Métricas globales atómicas (C11 stdatomic) y lectura de /proc/self/statm
│   └── worker.h                # Definición del worker concurrente y administración del pool de hilos
└── src/                        # Implementación (.c)
    ├── config.c                # Lógica de configuración por defecto y parseo de CLI
    ├── main.c                  # Coordinación de ciclo de vida, señales POSIX y fases experimentales
    ├── memory.c                # Reserva determinista de memoria fija en Heap con page-touching
    ├── network.c               # Conexión TCP con timeout estricto mediante epoll_wait
    ├── sha256.c                # Cómputo criptográfico de SHA-256 de alto rendimiento en C puro
    ├── telemetry.c             # Hilo reportero (250 ms) y consulta de RSS físico del proceso
    └── worker.c                # Bucle de trabajo concurrente, carga CPU y alloc/free dinámico
```

---

## 2. Análisis Previo de la Implementación en Go y Decisiones en C

### 2.1 Flujo y Equivalencia Funcional
El cliente original en Go (`cliente.go`) ejecuta un flujo de tres fases:
1. **Fase 1 (Reposo / Idle):** Espera durante `IdleSeconds` (por defecto 5 s) para medir la huella base del proceso y su runtime en memoria física (RSS).
2. **Fase 2 (Reserva de Heap Estático):** Asigna `HeapTargetMB` (por defecto 2 MB) en memoria dinámica, tocando cada byte para forzar el mapeo de páginas físicas en el kernel.
3. **Fase 3 (Despliegue Concurrente y Telemetría):**
   - Lanza 4000 goroutines concurrentes coordinadas por el runtime $M:N$.
   - Cada worker ejecuta:
     - Asignación dinámica volátil (4 KB a 8 KB) para estresar el recolector de basura.
     - Cómputo intensivo de CPU (250 rondas de SHA-256 sobre una firma fija).
     - Selección de IP con rotación: 30% a `192.0.2.1:9999` (simulando timeout `SYN_SENT`), 70% a `127.0.0.1:9090`.
     - Conexión TCP con timeout de 150 ms, envío de firma y lectura de acuse.
     - Pausa de 1 ms (`WorkerDelay`).
   - Un hilo reportero emite métricas cada 250 ms en stdout.
   - Manejo de señales (`SIGINT`, `SIGTERM`) para cierre ordenado.

### 2.2 Decisiones de Arquitectura e Inspiración en Mirai
Para reproducir estas funciones en C sin la sobrecarga de un runtime de alto nivel:
* **Modelo de Concurrencia:** En Go, 4000 goroutines consumen inicialmente $\approx 2\text{ KB}$ cada una gracias a sus stacks dinámicos. En C/Linux, cada hilo POSIX (`pthread`) reserva por defecto 8 MB de stack virtual. Para modelar 4000 slots concurrentes de forma realista y apta para entornos embebidos o restringidos (32 MB RAM), la implementación utiliza un **pool de 64 hilos POSIX con stack configurado explícitamente a 128 KB** (`pthread_attr_setstacksize`), multiplexando los 4000 slots lógicos de trabajo.
* **Entrada/Salida No Bloqueante (Sockets POSIX + epoll):** Siguiendo la arquitectura de Mirai, las conexiones TCP se crean con la bandera `SOCK_NONBLOCK`. Para gestionar el timeout de 150 ms sin bloquear el hilo, se utiliza `epoll_create1` y `epoll_wait`. Esto permite capturar el estado `SYN_SENT` de las conexiones no respondidas sin dejar descriptores colgados.
* **Gestión Manual de Memoria (malloc / free inmediato):** Frente a las asignaciones de Go que acumulan basura en el Heap hasta que interviene el Garbage Collector, el worker en C realiza `malloc()` y `free()` inmediatos de los buffers de 4 a 8 KB. La memoria se devuelve directamente al asignador de la biblioteca estándar (glibc), logrando una huella de memoria plana y constante.
* **Independencia de Bibliotecas Externas (SHA-256 autónomo):** Para evitar la dependencia de OpenSSL (`libssl` / `libcrypto`) que inflaría el tamaño del ejecutable y requeriría paquetes adicionales en el contenedor, se implementó un motor SHA-256 puro conforme al estándar FIPS 180-4.
* **Sincronización Lock-Free con C11 `<stdatomic.h>`:** Los contadores de telemetría utilizan variables atómicas estándar (`atomic_long` con `atomic_fetch_add` y `atomic_exchange`), evitando la contención de mutexes pesados.

---

## 3. Tabla de Equivalencias entre Go y C

| Componente / Responsabilidad | Implementación en Go (`cliente.go`) | Implementación en C (`Codigo_c/Arquitectura_AMD64`) |
| :--- | :--- | :--- |
| **Entorno de Ejecución** | Runtime propio de Go con recolector de basura y scheduler $M:N$ | C puro (C11) sobre glibc/Linux directo, sin runtime intermedio |
| **Concurrencia de Red** | 4000 goroutines planificadas sobre núcleos disponibles | Pool de hilos POSIX con stack reducido (128 KB) multiplexando 4000 slots |
| **Manejo de Sockets** | `net.Dialer` con `context.WithTimeout` (Goroutine bloqueada lógicamente) | Socket POSIX `SOCK_NONBLOCK` + `epoll_wait` con timeout explícito (estilo Mirai) |
| **Memoria Estática Retenida** | Slices de bytes (`[][]byte`) retenidos en variable global de Heap | Punteros `uint8_t**` asignados con `malloc()`, paginados mediante escritura |
| **Memoria Dinámica Volátil** | `make([]byte, size)` delegado al Garbage Collector periódico | `malloc()` y `free()` explícito e inmediato por ciclo |
| **Cómputo CPU** | `crypto/sha256.Sum256` (librería estándar de Go) | `sha256_compute` (implementación FIPS 180-4 pura sin dependencias) |
| **Medición de Memoria Física** | Lectura de `/proc/self/statm` + `runtime.ReadMemStats` | Lectura directa de `/proc/self/statm` y cálculo mediante `sysconf(_SC_PAGESIZE)` |
| **Telemetría y Métricas** | Goroutine con `time.NewTicker(250ms)` y `sync/atomic` | Hilo POSIX con `nanosleep(250ms)` y tipos atómicos C11 (`<stdatomic.h>`) |
| **Manejo de Señales** | Canal `chan os.Signal` con `signal.Notify` + `context.Cancel` | `sigaction()` capturando `SIGINT` y `SIGTERM`, ignorando `SIGPIPE` |
| **Tamaño Típico del Binario** | ~1.7 MB (stripped) a ~2.8 MB (nativo) | ~27 KB (stripped) a ~31 KB (nativo) |

---

## 4. Requisitos y Dependencias

### Entorno del Host
* Sistema Operativo: Linux (x86_64 / AMD64).
* Compilador: GCC 9 o superior (soporte completo de C11 y POSIX.1-2008).
* Utilidades del sistema: GNU Make, `strip` (`binutils`).
* Bibliotecas: glibc (`libc.so`, `libpthread.so`), provistas por defecto en cualquier distribución Linux estándar.
* Para pruebas en contenedor: Docker 20.10+ con soporte de cgroups v1 o v2.

---

## 5. Instrucciones de Compilación

Desde el directorio de la arquitectura:

```bash
cd Codigo_c/Arquitectura_AMD64
```

### Compilar todas las variantes (Nativa y Stripped)
```bash
make all
```
Esto genera en el directorio `bin/`:
* `bin/cliente_c`: Binario ELF AMD64 con tabla de símbolos estándar (~31 KB).
* `bin/cliente_c_stripped`: Binario optimizado y depurado con `strip --strip-all` (~27 KB).

### Compilar variante estática pura (opcional)
```bash
make static
```
Genera `bin/cliente_c_static`, independiente de la versión de glibc del contenedor huésped (requiere `libc6-dev` estática instalada).

### Limpiar artefactos de compilación
```bash
make clean
```

---

## 6. Instrucciones de Ejecución

### 6.1 Ejecución Local Nativa
1. Iniciar previamente el servidor mock de pruebas en otra terminal:
   ```bash
   gcc -O2 -pthread servidor_mock.c -o servidor_mock
   ./servidor_mock
   ```

2. Ejecutar el cliente en C (con argumentos por defecto: 5 s idle, 2 MB heap, 64 workers/4000 slots):
   ```bash
   ./Codigo_c/Arquitectura_AMD64/bin/cliente_c_stripped
   ```

3. Personalizar la carga por línea de comandos:
   ```bash
   # Formato posicional idéntico a Go: [idle_sec] [heap_mb] [workers/slots]
   ./Codigo_c/Arquitectura_AMD64/bin/cliente_c_stripped 2 4 64

   # O mediante banderas extendidas:
   ./Codigo_c/Arquitectura_AMD64/bin/cliente_c_stripped --idle 3 --heap 2 --workers 64 --slots 4000 --port 9090
   ```

### 6.2 Ejecución en Contenedores Docker con Límites de Memoria

Para reproducir los experimentos de la **Tabla 2** del informe académico (emulación de dispositivos IoT con 32 MB, 64 MB y 128 MB de RAM):

#### 1. Construir la imagen Docker:
```bash
cd Codigo_c/Arquitectura_AMD64
docker build -t cliente_c_amd64 .
```

#### 2. Ejecutar con límite estricto de 32 MB (sin swap):
```bash
docker run --rm -it \
    --name iot_test_c_32m \
    --network=host \
    --memory=32m \
    --memory-swap=32m \
    cliente_c_amd64 5 2 64
```

#### 3. Ejecutar con límite de 64 MB:
```bash
docker run --rm -it \
    --name iot_test_c_64m \
    --network=host \
    --memory=64m \
    --memory-swap=64m \
    cliente_c_amd64 5 2 64
```

#### 4. Ejecutar con límite de 128 MB:
```bash
docker run --rm -it \
    --name iot_test_c_128m \
    --network=host \
    --memory=128m \
    --memory-swap=128m \
    cliente_c_amd64 5 2 64
```

---

## 7. Limitaciones Conocidas

1. **Simulación de SYN_SENT:** Para que las conexiones a `192.0.2.1:9999` permanezcan efectivamente en estado `SYN_SENT` hasta agotar el timeout de 150 ms sin que el host responda con un paquete ICMP *Destination Unreachable*, se recomienda aplicar una regla de descarte en el firewall del host:
   ```bash
   sudo iptables -A OUTPUT -d 192.0.2.1 -p tcp --dport 9999 -j DROP
   ```
2. **Límite de Descriptores de Archivo (`ulimit -n`):** Bajo altas tasas de conexión concurrente, el sistema operativo puede alcanzar el límite por defecto de descriptores (`ulimit -n 1024`). Se recomienda elevarlo antes de pruebas masivas:
   ```bash
   ulimit -n 65535
   ```
3. **Pila de Hilos en C vs Stacks Segmentados:** Aunque el stack de cada hilo se redujo a 128 KB, en C el stack tiene un límite fijo asignado por el kernel en el momento de crear el hilo, a diferencia de Go, cuyos stacks crecen dinámicamente según la profundidad de llamadas.


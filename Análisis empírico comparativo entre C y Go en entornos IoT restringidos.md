# **Análisis empírico comparativo entre C y Go en entornos IoT restringidos: arquitecturas de referencia en sistemas embebidos**

Gonzalo M. Fernández Gómez, Erck Luciano Andrés, Juan Roman Zacarias, Fernando Antonio Arce Obregon¹  
Fac. de Ciencias Exactas Naturales y Agrimensura, Universidad Nacional del Nordeste, Corrientes  
\[correos electrónicos de los integrantes\]@comunidad.unne.edu.ar

**Resumen.** El desarrollo de agentes de red orientados a dispositivos de Internet de las Cosas (IoT) ha migrado progresivamente del lenguaje C, tradicional y de bajo nivel, hacia Go, un lenguaje moderno con entorno de ejecución (*runtime*) incorporado. En el marco del trabajo de investigación en equipo de la asignatura Redes de Datos, cuarto año de la Carrera LSI, proponemos una comparación empírica del costo en memoria RAM, caudal de red, unidades concurrentes activas (UCA) y tamaño de binario entre ambos lenguajes sobre entornos IoT restringidos. Para ello empleamos dos clientes de red benignos y equivalentes en carga de trabajo por ciclo —uno en C11 con hilos POSIX de pila acotada y sockets no bloqueantes con `epoll`, y otro en Go con *goroutines* y *netpoller*— ejecutados en contenedores Docker con cuota fija de procesador (`1.0` CPU) y límites estrictos de memoria RAM (`32`, `64`, `128` y `256` MB sin *swap*) sobre tres arquitecturas de referencia (`AMD64` nativa, `ARMv7` y `MIPS`/`MIPSLE` bajo emulación `qemu-user-static`). Tomando como casos de estudio de inspiración arquitectónica a Mirai (C) y BotenaGo (Go), demostramos empíricamente que: (1) bajo concurrencia homologada (`UCA = 64`), Go consume aproximadamente el doble de memoria RAM que C (`~16.1 MB` vs. `~8.9 MB` en `AMD64` nativo) y alcanza el umbral crítico de saturación en `32 MB` sobre `ARMv7` (`32.98 MB` pico), y (2) al escalar a concurrencia masiva no acotada (`UCA = 4000`), el recolector de basura y el *runtime* de Go en 64 bits provocan la detención por falta de memoria (*OOM Kill*) en `32`, `64` y `128` MB, estabilizándose recién en `256` MB (`136.90 MB` promedio).

Palabras clave: IoT, sistemas distribuidos, Go, C, huella de memoria, arquitecturas embebidas, software libre.

**Abstract.** Network agents targeting Internet of Things (IoT) devices have progressively migrated from C, a traditional low-level language, towards Go, a modern language with an embedded runtime. In the context of a team research project for the "Redes de Datos" course, in the fourth year of the LSI degree program, we propose an empirical comparison of RAM footprint, network throughput, active concurrent units (UCA), and binary size between both languages on constrained IoT environments. To this end, two benign test network clients with unified per-cycle synthetic workloads —one in C11 using bounded POSIX threads and non-blocking `epoll` sockets, and one in Go using goroutines— are executed inside Docker containers under fixed CPU quotas (`1.0` CPU) and strict memory limits (`32`, `64`, `128`, and `256` MB without swap) across three reference architectures (`AMD64` native, `ARMv7`, and `MIPS`/`MIPSLE` under `qemu-user-static` emulation). Taking the Mirai (C) and BotenaGo (Go) architectures as reference cases from the literature, we show that under equal concurrency (`UCA = 64`), Go requires roughly twice the RAM of C (`~16.1 MB` vs. `~8.9 MB` on native `AMD64`), while under massive unbounded concurrency (`UCA = 4000`), Go's runtime and garbage collector trigger kernel Out-Of-Memory (OOM) termination across `32`, `64`, and `128` MB containers.

**Keywords:** IoT, distributed systems, Go, C, memory footprint, embedded architectures, open-source software.

# **1 Introducción**

En los últimos años, ha habido un crecimiento exponencial en la cantidad de dispositivos conectados a Internet. Los dispositivos del Internet de las Cosas (IoT) —tales como enrutadores domésticos, cámaras IP y grabadores de video digital (DVR)— se caracterizan por sus recursos limitados: poca memoria RAM, baja capacidad de procesamiento y, con frecuencia, firmware desactualizado \[1, 7\]. Estas limitaciones requieren un análisis detallado cuando los equipos forman parte de redes de nodos distribuidos que un servidor central gestiona de forma remota bajo condiciones de alta concurrencia de tráfico \[6, 7\], (fig.1).

El caso de referencia histórico en sistemas embebidos de red es la arquitectura Mirai, documentada en 2016 y cuyo cliente fue escrito en lenguaje C \[1\]. Su código de bajo nivel y binario compacto se adapta de manera óptima a dispositivos Linux embebidos de muy pocos recursos. En los últimos años, sin embargo, se observa una migración progresiva hacia Go (Golang), un lenguaje moderno con un entorno de ejecución (*runtime*) incorporado que facilita enormemente la compilación cruzada para múltiples arquitecturas de procesador y el manejo de concurrencia masiva \[2, 8\]. Arquitecturas analizadas en la literatura reciente como BotenaGo \[2\], HinataBot \[4\] o NoaBot \[3, 5\] son ejemplos claros de esta tendencia tecnológica.

La migración de C hacia Go ofrece grandes ventajas al desarrollador, pero el *runtime* de Go tiene un costo en memoria RAM que resulta crítico en hardware de muy bajos recursos. Dado que la literatura técnica analiza ambos modelos por separado \[1, 2\], este trabajo plantea una comparación empírica directa de la huella de memoria, el tráfico de red efectivo, la concurrencia alcanzada y el tamaño de binario entre C y Go bajo las mismas condiciones de contenedorización. El trabajo es de carácter estrictamente académico y experimental: la medición se realiza sobre dos simuladores de red propios y completamente benignos en un entorno local aislado.

## **Definiciones**

* **Internet de las Cosas (IoT):** Se refiere a la interconexión digital de objetos físicos cotidianos o industriales con Internet, los cuales operan habitualmente sobre sistemas operativos Linux embebidos con fuertes restricciones de memoria, almacenamiento y procesador \[1, 7\].  
* **Red de nodos distribuidos (Arquitectura Cliente-Control):** Infraestructura compuesta por múltiples dispositivos clientes (nodos) que mantienen sesiones persistentes y son coordinados de forma remota por un servidor central para ejecutar tareas concurrentes de red \[6, 7\].  
* **Entorno de ejecución (*Runtime*) de Go:** Es el conjunto de componentes de software integrados directamente dentro de cada archivo ejecutable compilado en Go que se encarga de administrar automáticamente la memoria, ejecutar el recolector de basura (*Garbage Collector*) y planificar la concurrencia de tareas (*goroutines*) \[8\].  
* **Grupos de control (*cgroups*):** Es una funcionalidad del kernel de Linux que permite limitar, aislar y medir el uso de recursos del sistema (como memoria RAM y tiempo de CPU) asignados a un grupo de procesos o contenedores \[9\].
* **Unidades Concurrentes de Actividad/Ataque (UCA):** Métrica operativa que cuantifica la cantidad instantánea (`uca_active`) y máxima (`uca_max`) de unidades de concurrencia reales (sockets/trabajadores en C o *goroutines* en Go) que se encuentran ejecutando activamente ciclos de cómputo y E/S de red en un instante dado.

Los paquetes de software utilizados en esta propuesta son de naturaleza de código abierto (*Open Source*), y lo integran los siguientes productos:

* de sistema operativo GNU/Linux (Debian / Ubuntu 22.04 LTS),  
* de manejo de contenedores y límites de recursos Docker Engine (`cgroups` v2),  
* de compilación nativa y cruzada en lenguaje C (`gcc`, `gcc-arm-linux-gnueabihf`, `gcc-mips-linux-gnu`) y lenguaje Go (`go` toolchain 1.22),  
* de emulación multi-arquitectura en espacio de usuario (`qemu-user-static` vía `binfmt_misc`),  
* de monitoreo de procesos y auditoría de memoria del kernel (`/sys/fs/cgroup`, `/proc`, `docker stats`).

La actividad principal consiste en la prueba y análisis comparativo de un cliente de conexiones concurrentes programado en lenguaje C y otro equivalente en lenguaje Go, evaluando su consumo de recursos y viabilidad dentro de contenedores restringidos a `32 MB`, `64 MB`, `128 MB` y `256 MB` de memoria RAM sobre las arquitecturas `AMD64`, `ARMv7` y `MIPS`/`MIPSLE`.

# **2 Instrumentación y Equivalencia Experimental**

Para el despliegue del estudio en cuestión se dispuso de los siguientes paquetes de software y hardware:

* Computadora con microprocesador de múltiples núcleos `x86_64` y 16 GB de memoria RAM  
* Sistema operativo GNU/Linux de 64 bits con imagen base de contenedores Ubuntu 22.04 LTS  
* Docker Engine (24.0+) con soporte de Linux `cgroups` v2 como manejador de contenedores  
* Compiladores GNU C (`gcc` nativo y *toolchains* cruzadas para `armhf` y `mips`) y compilador oficial de Go (`go1.22.2`)  
* Intérpretes estáticos QEMU user-mode (`qemu-arm-static` y `qemu-mips-static`/`qemu-mipsel-static`) para la ejecución de binarios de 32 bits (`ARMv7` y `MIPS`) sobre el host `x86_64`  
* Herramientas de telemetría de baja sobrecarga: lecturas directas de `/sys/fs/cgroup/memory.current`, `memory.stat` (`inactive_file`), `/proc/<pid>/statm`, `/proc/net/dev` y contadores atómicos C11/Go

## **Componentes y homologación de la carga de trabajo**

Para garantizar una comparación científicamente válida y libre de sesgos entre C y Go, ambos subsistemas fueron homologados en sus parámetros operativos:

1. **Contenedores Docker con límites idénticos de memoria y CPU:** Se aplican topes estrictos de `32 MB`, `64 MB`, `128 MB` y `256 MB` de RAM sin memoria de intercambio (`--memory-swap` igual a `--memory`) y una cuota constante de procesador de `1.0` CPU (`--cpus="1.0"`) tanto en C como en Go, forzando a que el kernel de Linux actúe mediante su *Out-Of-Memory (OOM) Killer* si el contenedor excede la memoria física asignada (fig.2).
2. **Mismo servidor sumidero local (`servidor_mock`):** Tanto las pruebas de C como las de Go utilizan exclusivamente el servidor TCP no bloqueante basado en `epoll` ([`servidor_mock.c`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/servidor_mock.c), escuchando en el puerto `9090` con cola `listen` de `4096`), el cual acepta conexiones de forma inmediata, recibe el *payload* de `48 bytes`, responde con `7 bytes` (`ACK_OK\n`) y cierra limpiamente la transacción.
3. **Misma carga sintética por ciclo en todas las arquitecturas (`AMD64`, `ARMv7`, `MIPS`) y lenguajes (`C` y `Go`):**
   * Retención inicial en el *heap* de un bloque estático de `2 MB` (`BENCHMARK_HEAP_MB=2`).
   * Nivel de concurrencia homologado en **`UCA = 64` unidades concurrentes** (`BENCHMARK_UCA=64`) y `0%` de destinos inalcanzables (`BENCHMARK_UNREACHABLE_RATIO=0`), además de evaluar en Go un segundo régimen de estrés masivo (`UCA = 4000`) representativo del patrón de propagación descontrolada.
   * En cada ciclo: reserva dinámica de un bloque temporal pseudoaleatorio de `4096 a 8191 bytes` (`malloc`/`free` en C; `make([]byte, bufferSize)` gestionado por el GC en Go), cálculo de `1` hash SHA-256 sobre dicho bloque y **`250` iteraciones de SHA-256** sobre el *payload* de firma sintética `"synthetic_payload_signature_data_block_worker_%d"` (`48 bytes`), envío TCP con *timeout* de `150 ms`, lectura de confirmación `ACK_OK\n` (`7 bytes`) y pausa de ciclo de `1 ms` (`usleep(1000)` / `time.Sleep(1ms)`).
4. **Mismo instrumento de medición de memoria RAM:** Tanto C como Go calculan `ram_used_mb` a `1.0 Hz` tomando el máximo entre el RSS del proceso (`/proc/<pid>/statm`) y el *working set* del grupo de control (`/sys/fs/cgroup/.../memory.current` descontando `inactive_file` de `memory.stat`).

**Tabla 1. Instrumentos de medición del entorno experimental**

| Qué se mide | Herramienta / Fuente | Propósito en la prueba |
| :---- | :---- | :---- |
| Memoria del contenedor (`ram_used_mb`, `ram_max_mb`) | `/sys/fs/cgroup/memory.current`, `memory.stat`, `/proc/<pid>/statm` | Medir memoria física real (RSS + kernel cgroup) a 1.0 Hz sin latencia del demonio Docker |
| Evento de falta de memoria (*OOM Kill*) | `docker inspect (.State.OOMKilled)`, código de salida `137` / `2` | Registrar la detención del contenedor por exceso de límite de RAM |
| Tráfico de red (`network_rx_mb`, `network_tx_mb`) | `/proc/net/dev` (`eth0` L2/L3 en C) y contadores atómicos L7 (`Write`/`Read` en Go) | Cuantificar el caudal acumulado recibido y transmitido hacia `servidor_mock` |
| Unidades Concurrentes (`uca_active`, `uca_max`) | Variables atómicas C11 (`atomic_long`) y Go (`sync/atomic`) | Registrar las unidades de concurrencia reales en ejecución |
| Tamaño del binario | `ls -lh` / `stat -c %s` tras la compilación | Comparar peso en disco de variantes normales y reducidas (*stripped*) |

# **3 Desarrollo**

En la implementación se analizó cómo las diferencias de consumo entre C y Go derivan directamente de la arquitectura interna de cada lenguaje, su gestión de memoria y su modelo de concurrencia.

**C — Inspiración arquitectónica en Mirai:** Mientras que el agente Mirai original emplea procesos asíncronos *single-threaded* enlazados estáticamente contra `uClibc` \[1\], nuestro simulador benigno en C11 implementa la concurrencia mediante sockets no bloqueantes con `epoll` y un conjunto acotado de hilos POSIX (`pthread`) con pilas reducidas (`64–128 KB`) y `MALLOC_ARENA_MAX=1`, enlazado dinámicamente contra `glibc` (`libc.so.6`) sobre Ubuntu 22.04:
* En `AMD64`, utiliza `64` hilos trabajadores (`1` socket no bloqueante por hilo = `64` UCA reales).
* En `ARMv7`, utiliza `16` hilos trabajadores, cada uno multiplexando `4` sockets no bloqueantes reales mediante `epoll` (`16 × 4 = 64` UCA reales).
* En `MIPS`, utiliza `4` hilos trabajadores, cada uno multiplexando `16` slots no bloqueantes reales mediante `epoll` (`4 × 16 = 64` UCA reales).
Al carecer de recolector de basura, cada bloque de `4–8 KB` reservado con `malloc()` se libera inmediatamente con `free()`, logrando una huella de memoria plana (`7.94–9.32 MB` en `AMD64` nativo) y un binario muy compacto (`18.9 KB` a `45.7 KB`) \[1, 10\] (fig.3).

**Go — Inspiración arquitectónica en BotenaGo:** Go abstrae la red mediante su librería estándar (`net`) y maneja la concurrencia con *goroutines* sobre su planificador M:N y *netpoller* integrado \[2, 8\]. Además, incorpora un recolector de basura concurrente (*Garbage Collector*, con `GOGC=100` por defecto) que administra los bloques dinámicos de `4–8 KB` reservados en cada ciclo \[8\]. Todo el *runtime* viaja enlazado estáticamente dentro del ejecutable (`CGO_ENABLED=0`), lo que produce binarios autónomos de `1.80 MB` a `2.06 MB` y una huella de memoria base superior a la de C, a cambio de compilación cruzada inmediata desde un único archivo fuente ([`BotenaGo/botena.go`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/BotenaGo/botena.go)) \[2, 3\] (fig.4).

Para la compilación de las variantes evaluadas en tamaño de binario se emplearon las siguientes instrucciones:

```bash
# Implementación C (modular multi-arquitectura y monolítica de referencia)
gcc -std=c11 -O2 -pthread src/*.c -Iinclude -o bin/cliente_c
strip --strip-all bin/cliente_c -o bin/cliente_c_stripped

# Implementación Go (binarios ELF 100% estáticos con reducción de símbolos DWARF)
CGO_ENABLED=0 GOOS=linux GOARCH=amd64  go build -ldflags="-s -w" -o bot_x86_64 botena.go
CGO_ENABLED=0 GOOS=linux GOARCH=arm GOARM=7 go build -ldflags="-s -w" -o bot_armv7 botena.go
CGO_ENABLED=0 GOOS=linux GOARCH=mipsle GOMIPS=softfloat go build -ldflags="-s -w" -o bot_mipsle botena.go
```

# **4 Comparativa y Resultados Empíricos**

Se ejecutaron sistemáticamente las 24 configuraciones del diseño factorial (`2 lenguajes × 3 arquitecturas × 4 límites de memoria: 32, 64, 128 y 256 MB`) bajo condiciones homologadas (`UCA = 64`, `UnreachableRatio = 0%`, `--cpus="1.0"`, `servidor_mock`), complementadas con las pruebas de estrés de concurrencia masiva en Go (`UCA = 4000`).

**Tabla 2. Comparativa de viabilidad y consumo de memoria en ejecución nativa (`AMD64`, `--cpus="1.0"`, sin swap)**

| Límite de memoria | Cliente C (`UCA = 64`) Media ± DE (Pico) | Cliente Go Homologado (`UCA = 64`) Media ± DE (Pico) | Cliente Go Estrés Masivo (`UCA = 4000`) Media ± DE (Pico) | Estado observado (`AMD64`) |
| :---- | :---- | :---- | :---- | :---- |
| **32 MB** | 7.94 ± 0.58 MB (9.17 MB) | 16.57 ± 0.57 MB (17.52 MB) | **OOM Stop (0.2s)** (32.00 MB) | Con `UCA=64` ambos viables (C usa 52% menos RAM); con `UCA=4000` Go colapsa |
| **64 MB** | 9.32 ± 1.41 MB (12.23 MB) | 16.11 ± 0.72 MB (17.30 MB) | **OOM Stop (0.2s)** (64.00 MB) | Con `UCA=64` ambos estables; con `UCA=4000` Go colapsa |
| **128 MB** | 9.04 ± 1.33 MB (11.69 MB) | 16.00 ± 0.55 MB (16.96 MB) | **OOM Stop (3.0s)** (>128.0 MB) | Con `UCA=64` ambos estables; con `UCA=4000` Go colapsa en pico de GC |
| **256 MB** | 8.89 ± 1.34 MB (11.15 MB) | 14.75 ± 0.92 MB (16.29 MB) | 136.90 ± 17.66 MB (145.90 MB) | Estabilidad completa en todos los regímenes |

**Tabla 3. Comparativa empírica de tamaño de binario por lenguaje y arquitectura**

| Variante / Arquitectura | Tamaño normal | Tamaño reducido (`strip` / `-s -w`) | Tipo de enlace y características de compilación |
| :---- | :---- | :---- | :---- |
| **C Monolítico (`cliente_c` AMD64)** | 22.3 KB (22,336 B) | 18.9 KB (18,856 B) | ELF 64-bit dinámico (`glibc`), `-O2 -pthread` |
| **C Modular `AMD64` (`bot_amd64`)** | 40.8 KB (40,888 B) | 35.4 KB (35,392 B) | ELF 64-bit dinámico (`glibc`), SHA-256 + telemetría CSV |
| **C Modular `ARMv7` (`bot_armv7`)** | 29.6 KB (30,312 B) | 25.3 KB (25,928 B) | ELF 32-bit `armhf` dinámico, `-march=armv7-a -mfpu=vfpv3-d16` |
| **C Modular `MIPS` (`bot_mips`)** | 45.7 KB (46,776 B) | 39.1 KB (40,056 B) | ELF 32-bit `mips` Big-Endian dinámico, `-march=mips32r2` |
| **Go `AMD64` (`bot_x86_64`)** | ~2.65 MB | 1.80 MB (1,880,216 B) | ELF 64-bit estático (`go1.22.2`), incluye *runtime*, GC y *netpoller* |
| **Go `ARMv7` (`bot_armv7`)** | ~2.68 MB | 1.81 MB (1,900,696 B) | ELF 32-bit `arm` estático (`go1.22.2`), autónomo sin `libc` |
| **Go `MIPSLE` (`bot_mipsle`)** | ~3.05 MB | 2.06 MB (2,162,871 B) | ELF 32-bit `mipsle` (`softfloat`) estático (`go1.22.2`) |

**Tabla 4. Matriz empírica multi-arquitectura bajo carga homologada (`UCA = 64`, `UnreachableRatio = 0%`, `--cpus="1.0"`, `servidor_mock`)**

| Arq. | Límite RAM | C: RAM Media ± DE (Pico) | C: Red Total L2/L3 (RX / TX) | C: UCA Media (Máx) | Go: RAM Media ± DE (Pico) | Go: Red Total L7* (RX / TX) | Go: UCA Media (Máx) |
| :---- | :---- | :---- | :---- | :---- | :---- | :---- | :---- |
| **AMD64** *(nativo)* | **32 MB** | 7.94 ± 0.58 MB (9.17 MB) | 46.07 MB (21.45 / 24.62) | 28.3 (64) | 16.57 ± 0.57 MB (17.52 MB) | 3.1213 MB (0.3973 / 2.7240) | 37.0 (60) |
| **AMD64** *(nativo)* | **64 MB** | 9.32 ± 1.41 MB (12.23 MB) | 74.20 MB (34.58 / 39.62) | 27.2 (64) | 16.11 ± 0.72 MB (17.30 MB) | 3.1627 MB (0.4025 / 2.7602) | 42.6 (64) |
| **AMD64** *(nativo)* | **128 MB** | 9.04 ± 1.33 MB (11.69 MB) | 74.39 MB (34.65 / 39.74) | 24.9 (64) | 16.00 ± 0.55 MB (16.96 MB) | 3.2967 MB (0.4196 / 2.8771) | 44.1 (64) |
| **AMD64** *(nativo)* | **256 MB** | 8.89 ± 1.34 MB (11.15 MB) | 73.80 MB (34.38 / 39.42) | 26.1 (64) | 14.75 ± 0.92 MB (16.29 MB) | 3.3418 MB (0.4253 / 2.9165) | 42.4 (64) |
| **ARMv7** *(QEMU)* | **32 MB** | 24.20 ± 0.29 MB (25.18 MB) | 51.38 MB (19.99 / 31.40) | 21.2 (64) | 32.35 ± 0.30 MB (32.98 MB) | 0.1953 MB (0.0249 / 0.1704) | 44.0 (64) |
| **ARMv7** *(QEMU)* | **64 MB** | 21.72 ± 0.01 MB (21.73 MB) | 51.23 MB (20.01 / 31.22) | 24.7 (57) | 33.29 ± 2.07 MB (35.70 MB) | 0.2445 MB (0.0311 / 0.2134) | 44.9 (61) |
| **ARMv7** *(QEMU)* | **128 MB** | 21.73 ± 0.01 MB (21.73 MB) | 51.99 MB (20.24 / 31.75) | 22.6 (64) | 34.87 ± 2.27 MB (37.46 MB) | 0.2302 MB (0.0293 / 0.2009) | 38.3 (64) |
| **ARMv7** *(QEMU)* | **256 MB** | 21.73 ± 0.01 MB (21.73 MB) | 52.17 MB (20.31 / 31.85) | 26.2 (64) | 35.05 ± 0.80 MB (35.90 MB) | 0.2433 MB (0.0310 / 0.2123) | 48.3 (61) |
| **MIPS** *(QEMU)* | **32 MB** | 12.20 ± 0.05 MB (12.28 MB) | 7.08 MB (3.17 / 3.91) | 54.8 (64) | 26.01 ± 1.44 MB (28.37 MB) | 0.2972 MB (0.0378 / 0.2594) | 43.3 (63) |
| **MIPS** *(QEMU)* | **64 MB** | 10.44 ± 0.02 MB (10.45 MB) | 7.72 MB (3.46 / 4.26) | 54.5 (64) | 27.63 ± 0.91 MB (28.12 MB) | 0.3049 MB (0.0388 / 0.2661) | 38.4 (63) |
| **MIPS** *(QEMU)* | **128 MB** | 10.54 ± 0.02 MB (10.54 MB) | 7.82 MB (3.50 / 4.32) | 57.2 (64) | 27.52 ± 1.18 MB (28.34 MB) | 0.3083 MB (0.0392 / 0.2691) | 39.1 (61) |
| **MIPS** *(QEMU)* | **256 MB** | 10.50 ± 0.02 MB (10.51 MB) | 7.61 MB (3.40 / 4.20) | 54.8 (64) | 27.01 ± 2.07 MB (28.37 MB) | 0.2838 MB (0.0361 / 0.2477) | 43.6 (61) |

*\*Nota metodológica sobre la capa de contabilización de red:* En C (`Benchmarks_c/`), `telemetry.c` registra el tráfico de interfaz en Capa 2/3 (`/proc/net/dev` sobre `eth0`, donde cada transacción TCP de `48 B` de *payload* y `7 B` de `ACK_OK\n` involucra `~665 B` considerando los paquetes `SYN`, `SYN-ACK`, `ACK`, `PSH-ACK` y `FIN-ACK` en `25 s`). En Go (`BenchmarksGo/`), los contadores atómicos registran exclusivamente los bytes útiles de Capa 7 (`48 B` enviados en `conn.Write` y `7 B` recibidos en `conn.Read` durante `15 s`); al proyectar los `2.92 MB` de *payload* L7 de Go `AMD64` (`~63,700` transacciones exitosas en `15 s`, equivalentes a `~4,246` conex/s) a tramas L2/L3 en `25 s`, representan `~70.5 MB` de tráfico de interfaz, demostrando que con `UCA = 64` ambos lenguajes alcanzan tasas de transferencia de red equivalentes (`~4,250–4,300` transacciones TCP/s en `AMD64` nativo).

### **Discusión e interpretación técnica de los hallazgos**

1. **Comparación en igualdad de concurrencia (`UCA = 64`) vs. concurrencia masiva (`UCA = 4000`):**
   Al igualar estrictamente el número de unidades concurrentes (`UCA = 64`) y eliminar los bloqueos por *timeout* (`UnreachableRatio = 0%`), se aísla con precisión el sobrecosto estructural del *runtime* de Go frente a C:
   * En ejecución nativa (`AMD64`), el cliente C utiliza **`7.94–9.32 MB`** de RAM, mientras que el cliente Go utiliza **`14.75–16.57 MB`** (aproximadamente el doble de memoria RAM para sostener exactamente la misma carga de trabajo y el mismo caudal de `~4,250` conexiones TCP/s).
   * En `ARMv7` (`32 MB`), incluso con solo `64` *goroutines*, la suma del *runtime* de Go y el emulador alcanza **`32.35 MB` de media (`32.98 MB` pico)**, operando en el límite absoluto de saturación del contenedor de `32 MB`, mientras que C con `epoll` (`16` hilos × `4` sockets reales) se mantiene estable en `24.20 MB` (`21.73 MB` en `64–256 MB`).
   * Cuando Go se ejecuta bajo su patrón habitual de concurrencia masiva no acotada (`UCA = 4000` *goroutines* reservando bloques de `4–8 KB` por ciclo con `GOGC=100`), la memoria activa escala a `~136.9 MB`, provocando la destrucción inmediata del contenedor por el *OOM Killer* en `32 MB`, `64 MB` y `128 MB`. Esto demuestra que las detenciones por OOM en agentes Go sobre IoT resultan de la **combinación del peso base del *runtime*/GC con la facilidad del lenguaje para instanciar miles de *goroutines* concurrentes sin control de *backpressure***.

2. **Sobrecarga del emulador `qemu-user-static` en arquitecturas cruzadas (`ARMv7` y `MIPS`):**
   Las mediciones en `ARMv7` y `MIPS`/`MIPSLE` reflejan la ejecución del binario invitado junto con el traductor dinámico de bloques (TCG) de `qemu-user-static` dentro del mismo `cgroup`. En C, esto incrementa la RAM medida desde `~9.0 MB` (`AMD64` nativo) hasta `~21.7 MB` en `ARMv7` y `~10.5 MB` en `MIPS`; en Go, incrementa la RAM desde `~16.0 MB` (`AMD64` nativo) hasta `~34.8 MB` en `ARMv7` y `~27.5 MB` en `MIPSLE`. Por rigor metodológico, estos valores no deben atribuirse a la arquitectura física del silicio ARM o MIPS, sino al entorno de emulación en espacio de usuario.

3. **Tamaño de binario y portabilidad de compilación cruzada:**
   Los resultados de la Tabla 3 confirman una relación de tamaño de **50× a 95×** a favor de C (`18.9–39.1 KB` reducidos con enlace dinámico a `glibc`) frente a Go (`1.80–2.06 MB` estáticos con `-ldflags="-s -w"`).En contrapartida, mientras que C requirió tres árboles de configuración y *toolchains* cruzadas específicas (`gcc-arm-linux-gnueabihf`, `gcc-mips-linux-gnu`) acopladas a la versión de `libc` del contenedor, Go generó ejecutables 100% estáticos y autónomos a partir de un único archivo fuente.

# **5 Conclusión y posibles mejoras**

El estudio empírico realizado permite responder con evidencia cuantitativa dónde se sitúa la frontera operativa entre C y Go en dispositivos IoT restringidos:

1. **En dispositivos de gama ultra-baja (`32 MB` de RAM y almacenamiento flash severamente limitado):**
   El lenguaje **C** conserva una ventaja decisiva e insustituible. Su huella de memoria (`~8–9 MB` nativo) ofrece un amplio margen de seguridad en `32 MB` y sus ejecutables de `< 40 KB` caben en particiones flash mínimas. En Go, aunque un agente cuidadosamente acotado a `64` *goroutines* logra ejecutarse en `16.6 MB` sobre `AMD64`, cualquier incremento en la concurrencia (`UCA = 4000`) o la sobrecarga en arquitecturas de 32 bits lleva el consumo por encima de los `32–128 MB`, desencadenando el *OOM Killer* del kernel Linux.
2. **En dispositivos IoT de gama media (`64 MB` a `256 MB` de RAM):**
   Cuando la concurrencia de Go se acota mediante un *pool* controlado de trabajadores (`UCA = 64`), ambos lenguajes operan con total estabilidad desde los `64 MB` y alcanzan un rendimiento de red equivalente (`~4,250` transacciones/s en `AMD64`). En cambio, si el diseño en Go emplea concurrencia masiva sin acotar (`4000` *goroutines*), se requieren al menos `256 MB` de RAM (o el ajuste explícito de `GOMEMLIMIT` introducido en Go 1.19+) para evitar el colapso por falta de memoria.
3. **Trabajos futuros:**
   * Ejecutar al menos $N = 5$ a $N = 10$ repeticiones independientes por cada celda experimental para construir intervalos de confianza inter-corrida y aplicar contrastes de hipótesis no paramétricos (Mann-Whitney U).
   * Desplegar los binarios compilados sobre placas físicas `ARMv7` (ej. Raspberry Pi / routers embebidos) y `MIPS` para aislar completamente el consumo nativo sin la capa de traducción `qemu-user-static`.
   * Evaluar el efecto de `GOMEMLIMIT` y distintos valores de `GOGC` sobre la curva de supervivencia de Go en contenedores de `32 MB` y `64 MB`.

# **6 Referencias**

\[1\] Antonakakis, M., April, T., Bailey, M., Bernhard, M., Bursztein, E., Cochran, J., Durumeric, Z., Halderman, J. A., Invernizzi, L., Kallitsis, M., Kumar, D., Lever, C., Ma, Z., Mason, J., Menscher, D., Seaman, C., Sullivan, N., Thomas, K., Zhou, Y. Understanding the Mirai Architecture. 26th USENIX Security Symposium, pp. 1093–1110. (2017). https://www.usenix.org/conference/usenixsecurity17/technical-sessions/presentation/antonakakis

\[2\] Caspi, O. AT\&T Alien Labs technical analysis on Golang network agents (BotenaGo). AT\&T Cybersecurity. (2021). https://cybersecurity.att.com/blogs/labs-research/att-alien-labs-finds-new-golang-malware-botenago

\[3\] Cashdollar, L. Analyzing a Go-Based IoT Network Architecture. Akamai Security Research. (2026). https://www.akamai.com/blog/security-research/analyzing-go-based-iot-self-propagating-ddos-botnet

\[4\] Seaman, C., Cashdollar, L., West, A. HinataBot: Análisis técnico de una arquitectura basada en Go. Akamai Security Research. (2023). https://www.akamai.com/es/blog/security-research/hinatabot-uncovering-new-golang-ddos-botnet

\[5\] Kupchik, S. Análisis de compilación y estructura en NoaBot. Akamai Security Research. (2024). https://www.akamai.com/es/blog/security-research/mirai-based-noabot-crypto-mining

\[6\] Erquiaga, M. J. Mecanismos de Control y de propagación en redes distribuidas. CACIC 2011, RedUNCI, pp. 1076–1085. (2011).

\[7\] Torassa Colombero, V., Roatta, S., López, P. Estado del arte y taxonomía en redes de nodos distribuidos. Simposio Argentino de Ciberseguridad y Ciberdefensa (SACS), 53 JAIIO, SEDICI UNLP, pp. 53–66. (2024). http://sedici.unlp.edu.ar/handle/10915/176931

\[8\] Documentación oficial de Go. Go Runtime and Garbage Collector. https://go.dev/doc/, último acceso 2026.

\[9\] Docker Documentation. Runtime options with Memory, CPUs, and GPUs. https://docs.docker.com/config/containers/resource_constraints/, último acceso 2026.

\[10\] Zhang, X., Upton, O., Beebe, N. L., Choo, K. K. R. IoT Network Forensics: A Comprehensive Digital Forensic Case Study. Forensic Science International: Digital Investigation, 32, 300926. University of Texas at San Antonio. (2020). https://cspecc.utsa.edu/publications/files/Refereed_Papers/2020-Choo-IoT-botnet-forensics-comprehensive-dig-forensic-case-study.pdf

# **Lista de Figuras**

| Figura (Columna A) | Figura (Columna B) |
| :---- | :---- |
| **(fig.1)** Esquema general de arquitectura Cliente-Control en nodos IoT. \[Pegar imagen aquí\] | **(fig.2)** Esquema de contenedores Docker con límites de RAM de 32MB, 64MB, 128MB y 256MB. \[Pegar imagen aquí\] |
| **(fig.3)** Captura del código fuente del cliente en C (`Codigo_c/Arquitectura_AMD64/src/`). \[Pegar imagen aquí\] | **(fig.4)** Captura del código fuente del cliente en Go (`BotenaGo/botena.go`). \[Pegar imagen aquí\] |
| **(fig.5)** Captura de terminal lanzando contenedores con `--memory=32m`. \[Pegar imagen aquí\] | **(fig.6)** Captura del servidor de escucha local (`servidor_mock`) recibiendo conexiones TCP. \[Pegar imagen aquí\] |
| **(fig.7)** Captura de compilación y comando `ls -lh` mostrando tamaños de binarios. \[Pegar imagen aquí\] | **(fig.8)** Captura de telemetría mostrando consumo de RAM del cliente en C. \[Pegar imagen aquí\] |
| **(fig.9)** Captura de telemetría mostrando consumo de RAM del cliente en Go. \[Pegar imagen aquí\] | **(fig.10)** Captura de evento OOM al detener Go bajo concurrencia masiva. \[Pegar imagen aquí\] |

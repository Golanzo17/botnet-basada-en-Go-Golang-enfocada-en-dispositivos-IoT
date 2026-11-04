# Implementación Benigna en C para ARMV7 (32-bit ARM Linux)
## Banco de Pruebas Empírico: Arquitectura de Referencia IoT (Inspiración Mirai)

Este submódulo contiene la implementación en lenguaje **C (estándar C11)** orientada específicamente a la arquitectura **ARMV7 (32-bit ARM / armhf / armv7l sobre Linux)**, homóloga en comportamiento, parámetros y métricas al cliente de pruebas desarrollado en **Go (`cliente.go`)** y complementaria a la versión AMD64.

El objetivo es evaluar empíricamente la viabilidad y consumo de recursos (RAM física RSS, ciclos de CPU y tamaño de binario) en procesadores ARM de 32 bits habituales en dispositivos IoT reales (enrutadores hogareños, cámaras IP, DVRs y pasarelas embebidas) bajo límites estrictos de memoria en contenedores Docker (**32 MB, 64 MB y 128 MB**).

> **Aviso Académico:** Este desarrollo es estrictamente benigno e instrumental. No implementa capacidades ofensivas ni de infección. Las conexiones de red se dirigen exclusivamente al servidor local de pruebas (`servidor_mock` en puerto 9090) y a direcciones reservadas de documentación RFC 5737 (`192.0.2.1:9999`) para emular estados TCP `SYN_SENT` de manera controlada.

---

## 1. Organización del Proyecto

El código fuente sigue una arquitectura modular en C con separación de responsabilidades y adaptada al modelo de memoria de 32 bits:

```text
Codigo_c/Arquitectura_ARMV7/
├── Dockerfile                  # Receta multi-etapa: compilación cruzada y runtime arm32v7/ubuntu
├── Makefile                    # Reglas de compilación (cruzada con arm-linux-gnueabihf-gcc o nativa)
├── README.md                   # Documentación técnica, decisiones de diseño y guía de uso
├── .gitignore                  # Exclusión de directorios obj/ y bin/
├── include/                    # Cabeceras públicas (.h)
│   ├── config.h                # Parámetros del benchmark y parseo de argumentos CLI
│   ├── memory.h                # Retención determinista de Heap en 32 bits (homólogo a Go)
│   ├── network.h               # Sockets POSIX no bloqueantes, epoll y rotación de IP
│   ├── sha256.h                # Cómputo SHA-256 nativo en registros de 32 bits
│   ├── telemetry.h             # Métricas atómicas C11 (<stdatomic.h> + libatomic) y lectura RSS
│   └── worker.h                # Workers POSIX concurrentes con stack reducido para ARM
└── src/                        # Implementación (.c)
    ├── config.c                # Parseo de opciones, argumentos posicionales y resolución DNS
    ├── main.c                  # Orquestación de fases, captura de señales y cierre limpio
    ├── memory.c                # Malloc de 32 bits con page-touching y liberación completa
    ├── network.c               # Socket POSIX SOCK_NONBLOCK + epoll_wait
    ├── sha256.c                # Algoritmo FIPS 180-4 optimizado para palabras de 32 bits
    ├── telemetry.c             # Hilo reportero (250 ms), métricas consolidadas y persistencia
    └── worker.c                # Bucle de trabajo concurrente y pool de hilos POSIX
```

---

## 2. Análisis Previo y Decisiones Específicas para ARMV7

### 2.1 Particularidades del Hardware y Arquitectura ARMv7 (32 bits)
* **Punteros y Ancho de Palabra de 32 bits:** A diferencia de AMD64 (donde `sizeof(void*) == 8`), en ARMv7 los punteros y palabras del sistema son de 4 bytes (`sizeof(void*) == 4`). Esto reduce naturalmente el tamaño en memoria de las estructuras de datos, arrays de punteros del Heap y metadatos de hilos.
* **Alineación y Registros de 32 bits:** El algoritmo SHA-256 opera nativamente con variables de 32 bits (`uint32_t`). En procesadores ARMv7 (como Cortex-A7, Cortex-A8, Cortex-A9), los registros de propósito general (R0 a R12) son de 32 bits, lo que permite ejecutar las rotaciones (`ROR32`) y operaciones lógicas directamente en registros sin penalización de emulación.
* **Pila de Hilos en Sistemas Embebidos ARM:** Mientras que en Linux de escritorio un hilo reserva por defecto hasta 8 MB de stack virtual, en la arquitectura ARMv7 para IoT la pila por hilo se restringe a **128 KB** (`pthread_attr_setstacksize`), permitiendo instanciar el pool concurrente sin saturar el espacio de direcciones de 32 bits ni la memoria física del dispositivo.
* **Instrucciones Atómicas en ARMv7:** ARMv7 utiliza pares de instrucciones exclusivas `LDREX` / `STREX` para sincronización. En C11 se utiliza `<stdatomic.h>` enlazando con `-latomic` para garantizar soporte lock-free transparente tanto para contadores de 32 bits como de 64 bits.

---

## 3. Tabla de Equivalencias entre Go y C (ARMV7)

| Componente / Responsabilidad | Implementación en Go (`cliente.go`) | Implementación en C (`Codigo_c/Arquitectura_ARMV7`) |
| :--- | :--- | :--- |
| **Entorno de Ejecución** | Runtime Go compilado para `GOARCH=arm` con GC y planificador $M:N$ | Binario nativo C11 plano sin runtime intermedio |
| **Punteros y Direccionamiento** | Punteros de 32 bits bajo runtime de Go | Punteros nativos de 32 bits (`sizeof(void*) == 4`) |
| **Concurrencia de Red** | 4000 goroutines lógicas | Pool de 64 hilos POSIX (stack 128 KB) multiplexando 4000 slots concurrentes |
| **Manejo de Sockets** | `net.Dialer` con context timeout | Socket POSIX `SOCK_NONBLOCK` + `epoll_wait` con timeout estricto de 150 ms |
| **Memoria Estática Retenida** | Slices `[][]byte` retenidos en Heap | Punteros `uint8_t**` de 4 bytes asignados con `malloc()` y paginados |
| **Memoria Dinámica Volátil** | Asignaciones de 4 a 8 KB recolectadas por el GC | `malloc()` y `free()` explícito e inmediato por ciclo (huella plana) |
| **Cómputo CPU** | `crypto/sha256.Sum256` | `sha256_compute()` pura en C sobre registros R0-R12 de 32 bits |
| **Medición de Memoria Física** | `/proc/self/statm` + `runtime.ReadMemStats` | Lectura de `/proc/self/statm` con `sysconf(_SC_PAGESIZE)` (4096 bytes) |
| **Telemetría y Métricas** | Ticker 250 ms con `sync/atomic` | Hilo dedicado (250 ms) con C11 `<stdatomic.h>` y `-latomic` |
| **Manejo de Señales** | Canal de señales (`SIGINT`, `SIGTERM`) | `sigaction()` capturando `SIGINT` y `SIGTERM`, ignorando `SIGPIPE` |
| **Persistencia de Resultados** | No disponible nativamente | Guarda automáticamente `benchmark_resultado.txt` en el volumen montado |

---

## 4. Requisitos y Dependencias

### Para Compilación Cruzada (desde PC x86_64):
```bash
sudo apt update
sudo apt install -y build-essential gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf libc6-dev-armhf-cross
```

### Para Compilación y Ejecución en Docker:
* Docker con soporte de emulación multi-arquitectura QEMU (en Ubuntu: `sudo apt install -y qemu-user-static binfmt-support`).
* Imagen base de ejecución: `arm32v7/ubuntu:22.04`.

---

## 5. Instrucciones de Compilación ([`Makefile`](file:///home/fercompu/Documentos/facultad/4to/Redes/Go_based_botnet_attacking_IoT_Devices/botnet-basada-en-Go-Golang-enfocada-en-dispositivos-IoT/Codigo_c/Arquitectura_ARMV7/Makefile))

Navega a la carpeta de la arquitectura:
```bash
cd Codigo_c/Arquitectura_ARMV7
```

### Opción A: Compilación Cruzada desde Host x86_64
```bash
make all
```
*Utiliza automáticamente `arm-linux-gnueabihf-gcc` y flags `-march=armv7-a -mfloat-abi=hard -mfpu=vfpv3-d16`.*

### Opción B: Compilación Nativa (en Raspberry Pi o contenedor ARMv7)
```bash
make CROSS_COMPILE= all
```

**Artefactos generados en `bin/`:**
* `bin/cliente_c_armv7`: Binario ELF ARM de 32 bits con símbolos de depuración.
* `bin/cliente_c_armv7_stripped`: Binario optimizado y reducido (~24 a 28 KB).

---

## 6. Instrucciones de Ejecución en Docker (ARMv7)

### Paso 1: Construir la imagen Docker para ARMv7
Desde la raíz del proyecto:
```bash
docker build -t cliente_c_armv7 Codigo_c/Arquitectura_ARMV7/
```
*(El Dockerfile utiliza compilación multi-etapa: compila con toolchain cruzado en x86_64 en pocos segundos y empaqueta en `arm32v7/ubuntu:22.04`).*

### Paso 2: Ejecución bajo Límite Forzado de 32 MB de RAM (con persistencia en host)

**Terminal 1 (Servidor Mock):**
```bash
docker run --rm -it --name servidor_mock --network red_simulacion servidor_mock_img
```

**Terminal 2 (Cliente ARMv7):**
```bash
docker run --rm -it \
    --name cliente_armv7 \
    --network red_simulacion \
    --memory=32m \
    --memory-swap=32m \
    -v "$(pwd):/app/host" \
    cliente_c_armv7 5 2 64 servidor_mock
```

---

## 7. Resultados y Persistencia Automática

Al finalizar la prueba con `Ctrl + C`:
1. El programa imprime en la consola la tabla de resultados consolidados:
   ```text
   =================================================================
          RESUMEN FINAL CONSOLIDADO DEL BENCHMARK (ARMV7 - C)       
   =================================================================
     Tiempo medido en carga:        60.0 segundos
     Slots lógicos concurrentes:    4000
     ---------------------------------------------------------------
     RAM Base en Reposo (Idle RSS): 1.48 MB
     RAM Estática en Heap:          2 MB
     RAM Pico Registrada (Max RSS): 2.92 MB
     ---------------------------------------------------------------
     Conexiones TCP Totales:        118400 (promedio: 1973.3 req/s)
     Liberaciones Manuales (free):  165200 bloques (sin pausas GC)
     Comportamiento frente a RAM:   ESTABLE (Sin OOM Stop)
   =================================================================
   [+] benchmark_resultado.txt (guardado directamente en tu carpeta de la VM)
   ```
2. Gracias al volumen `-v "$(pwd):/app/host"`, el archivo `benchmark_resultado.txt` queda guardado inmediatamente en tu carpeta anfitriona para ser consultado con `cat benchmark_resultado.txt`.

---

## 8. Limitaciones Conocidas

1. **Emulación QEMU en Host x86_64:** Al correr contenedores `arm32v7` en máquinas x86_64, el kernel utiliza emulación de instrucciones de usuario mediante QEMU (`binfmt_misc`). La sobrecarga de CPU de la emulación es mayor que en una placa física ARMv7 nativa; no obstante, las mediciones de memoria física (RSS) y estabilidad frente a límites de cgroups se mantienen estrictamente válidas.
2. **Espacio de Direcciones de 32 bits:** En ARMv7, el espacio total de direcciones virtuales utilizable por proceso está limitado a 3 o 4 GB por hardware de 32 bits. Para la prueba de 32 MB esto no supone restricción alguna.


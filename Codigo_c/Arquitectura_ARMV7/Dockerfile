# ============================================================================
# Dockerfile: Cliente Benigno de Referencia en C (ARMV7 / armhf Linux)
# Compilación cruzada optimizada y ejecución ARMv7 con QEMU user-mode
# ============================================================================

# --- Etapa 1: Compilación Cruzada (Builder) ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-arm-linux-gnueabihf \
    binutils-arm-linux-gnueabihf \
    libc6-dev-armhf-cross \
    qemu-user-static \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY Makefile .
COPY include/ ./include/
COPY src/ ./src/

RUN make clean && make CROSS_COMPILE=arm-linux-gnueabihf- all

# --- Etapa 2: Entorno de Ejecución Ligero ---
FROM ubuntu:22.04 AS runtime

WORKDIR /app

# Copiar emulador QEMU ARM y bibliotecas dinámicas ARMhf desde builder
COPY --from=builder /usr/bin/qemu-arm-static /usr/bin/qemu-arm-static
COPY --from=builder /usr/arm-linux-gnueabihf/lib/ /usr/arm-linux-gnueabihf/lib/

# Copiar ejecutables compilados para ARMv7
COPY --from=builder /build/bin/cliente_c_armv7 /app/cliente_c_armv7
COPY --from=builder /build/bin/cliente_c_armv7_stripped /app/cliente_c_armv7_stripped

# Lanzador optimizado para límites de memoria IoT (32 MB) bajo QEMU user-mode:
# - MALLOC_ARENA_MAX=1 evita multiplicación de arenas glibc por hilo
# - ulimit -s 128 acota la pila del hilo anfitrión de QEMU a 128 KB
RUN printf '#!/bin/sh\nexport MALLOC_ARENA_MAX=1\nulimit -s 128 2>/dev/null || true\nexec /usr/bin/qemu-arm-static -L /usr/arm-linux-gnueabihf /app/cliente_c_armv7_stripped "$@"\n' > /app/bot_armv7 && \
    chmod +x /app/cliente_c_armv7 /app/cliente_c_armv7_stripped /app/bot_armv7 && \
    mkdir -p /app/benchmarks

ENTRYPOINT ["/app/bot_armv7"]
CMD ["2", "2", "64", "servidor_mock"]

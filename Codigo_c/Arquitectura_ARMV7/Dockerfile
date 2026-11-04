# ============================================================================
# Dockerfile: Cliente Benigno de Referencia en C (ARMV7 / armhf Linux)
# Compilación cruzada optimizada y runtime ligero en arm32v7/ubuntu:22.04
# ============================================================================

# --- Etapa 1: Compilación Cruzada (Builder en host rápido) ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Instalar toolchain de compilación cruzada para ARMv7 hard-float (armhf)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-arm-linux-gnueabihf \
    binutils-arm-linux-gnueabihf \
    libc6-dev-armhf-cross \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY Makefile .
COPY include/ ./include/
COPY src/ ./src/

# Compilar para arquitectura ARMv7
RUN make clean && make CROSS_COMPILE=arm-linux-gnueabihf- all

# --- Etapa 2: Entorno de Ejecución ARMv7 (arm32v7) ---
FROM arm32v7/ubuntu:22.04 AS runtime

WORKDIR /app

# Copiar ejecutables compilados para ARMv7
COPY --from=builder /build/bin/cliente_c_armv7 /app/cliente_c_armv7
COPY --from=builder /build/bin/cliente_c_armv7_stripped /app/cliente_c_armv7_stripped

RUN chmod +x /app/cliente_c_armv7 /app/cliente_c_armv7_stripped

ENTRYPOINT ["/app/cliente_c_armv7_stripped"]
CMD ["5", "2", "64", "servidor_mock"]


# ===========================================================================
# Dockerfile — Cliente C para MIPS 32-bit
#
# Build multi-etapa:
#   Etapa builder: cross-compila desde ubuntu:22.04 con mips-linux-gnu-gcc.
#   Etapa runtime: corre el binario MIPS en debian:bullseye con QEMU.
#
# Para ejecutar en x86_64 se necesita QEMU binfmt:
#   docker run --privileged --rm tonistiigi/binfmt --install mips
#
# Uso:
#   docker build -t cliente_c_mips .
#   docker run --rm -it --network red_simulacion \
#       --memory=32m --memory-swap=32m \
#       -v "$(pwd):/app/host" \
#       cliente_c_mips 5 2 64 servidor_mock
#
# Autoría académica — Simulación benigna para análisis comparativo C vs Go.
# ===========================================================================

# ---------------------------------------------------------------------------
# Etapa 1: Build (cross-compile en ubuntu:22.04 x86_64)
# ---------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Instalar toolchain de cross-compilación MIPS
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-mips-linux-gnu \
    binutils-mips-linux-gnu \
    libc6-dev-mips-cross \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copiar fuentes
COPY Makefile .
COPY include/ ./include/
COPY src/     ./src/

# Cross-compilar para MIPS 32-bit big-endian
RUN make clean && make CROSS_COMPILE=mips-linux-gnu- all

# ---------------------------------------------------------------------------
# Etapa 2: Runtime (debian:bullseye con emulación QEMU MIPS)
# ---------------------------------------------------------------------------
FROM debian:bullseye AS runtime

WORKDIR /app

# Copiar binarios cross-compilados
COPY --from=builder /build/bin/cliente_c_mips         /app/cliente_c_mips
COPY --from=builder /build/bin/cliente_c_mips_stripped /app/cliente_c_mips_stripped

RUN chmod +x /app/cliente_c_mips /app/cliente_c_mips_stripped

# Crear directorios para volumen y salida
RUN mkdir -p /app/host /app/out

# Argumentos por defecto: slots workers stack_kb host
ENTRYPOINT ["/app/cliente_c_mips_stripped"]
CMD ["5", "2", "64", "servidor_mock"]

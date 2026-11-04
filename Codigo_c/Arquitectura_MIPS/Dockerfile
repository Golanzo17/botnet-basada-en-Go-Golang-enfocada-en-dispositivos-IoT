# ===========================================================================
# Dockerfile — Cliente C para MIPS 32-bit (Big-Endian)
#
# Build multi-etapa:
#   Etapa builder: cross-compila desde ubuntu:22.04 con mips-linux-gnu-gcc.
#   Etapa runtime: ejecuta el binario ELF MIPS 32-bit mediante qemu-mips-static.
# ===========================================================================

FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-mips-linux-gnu \
    binutils-mips-linux-gnu \
    libc6-dev-mips-cross \
    qemu-user-static \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY Makefile .
COPY include/ ./include/
COPY src/     ./src/

RUN make clean && make CROSS_COMPILE=mips-linux-gnu- all

FROM ubuntu:22.04 AS runtime

WORKDIR /app

# Copiar emulador QEMU MIPS y bibliotecas dinámicas MIPS O32 desde builder
COPY --from=builder /usr/bin/qemu-mips-static /usr/bin/qemu-mips-static
COPY --from=builder /usr/mips-linux-gnu/lib/  /usr/mips-linux-gnu/lib/

# Copiar binarios ELF MIPS 32-bit cross-compilados
COPY --from=builder /build/bin/cliente_c_mips          /app/cliente_c_mips
COPY --from=builder /build/bin/cliente_c_mips_stripped /app/cliente_c_mips_stripped

# Lanzador optimizado para límites de memoria IoT (32 MB) bajo QEMU user-mode:
# - MALLOC_ARENA_MAX=1 evita multiplicación de arenas glibc por hilo
# - ulimit -s 128 acota la pila del hilo anfitrión de QEMU a 128 KB
RUN printf '#!/bin/sh\nexport MALLOC_ARENA_MAX=1\nulimit -s 128 2>/dev/null || true\nexec /usr/bin/qemu-mips-static -L /usr/mips-linux-gnu /app/cliente_c_mips_stripped "$@"\n' > /app/bot_mips && \
    chmod +x /app/cliente_c_mips /app/cliente_c_mips_stripped /app/bot_mips && \
    mkdir -p /app/benchmarks /app/host /app/out

ENTRYPOINT ["/app/bot_mips"]
CMD ["2", "2", "64", "servidor_mock"]

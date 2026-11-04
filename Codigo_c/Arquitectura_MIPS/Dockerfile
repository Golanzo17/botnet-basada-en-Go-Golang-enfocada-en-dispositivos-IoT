# ===========================================================================
# Dockerfile — Cliente C para MIPS 32-bit
#
# Build multi-etapa:
#   Etapa builder: cross-compila desde ubuntu:22.04 con mips-linux-gnu-gcc.
#   Etapa runtime: corre el binario MIPS en debian:bullseye con QEMU.
# ===========================================================================

FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-mips-linux-gnu \
    binutils-mips-linux-gnu \
    libc6-dev-mips-cross \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY Makefile .
COPY include/ ./include/
COPY src/     ./src/

RUN make clean && make CROSS_COMPILE=mips-linux-gnu- all

FROM debian:bullseye AS runtime

WORKDIR /app

COPY --from=builder /build/bin/cliente_c_mips          /app/cliente_c_mips
COPY --from=builder /build/bin/cliente_c_mips_stripped /app/cliente_c_mips_stripped

RUN chmod +x /app/cliente_c_mips /app/cliente_c_mips_stripped && \
    ln -sf /app/cliente_c_mips_stripped /app/bot_mips && \
    mkdir -p /app/benchmarks /app/host /app/out

ENTRYPOINT ["/app/cliente_c_mips_stripped"]
CMD ["2", "2", "64", "servidor_mock"]

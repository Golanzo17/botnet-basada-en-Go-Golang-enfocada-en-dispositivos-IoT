# ============================================================================
# Dockerfile: Cliente Benigno de Referencia en C (AMD64 / x86-64 Linux)
# Distribución de referencia: Ubuntu 22.04 LTS (x86_64)
# ============================================================================

# --- Etapa 1: Compilación (Builder) ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    make \
    binutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY Makefile .
COPY include/ ./include/
COPY src/ ./src/

RUN make clean && make all

# --- Etapa 2: Entorno de Ejecución (Runtime Ligero) ---
FROM ubuntu:22.04 AS runtime

WORKDIR /app

COPY --from=builder /build/bin/cliente_c /app/cliente_c
COPY --from=builder /build/bin/cliente_c_stripped /app/cliente_c_stripped

RUN chmod +x /app/cliente_c /app/cliente_c_stripped && \
    ln -sf /app/cliente_c_stripped /app/bot_amd64 && \
    mkdir -p /app/benchmarks

ENTRYPOINT ["/app/cliente_c_stripped"]
CMD ["2", "2", "64", "servidor_mock"]

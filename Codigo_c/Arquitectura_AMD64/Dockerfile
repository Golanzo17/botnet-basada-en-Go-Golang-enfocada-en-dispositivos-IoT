# ============================================================================
# Dockerfile: Cliente Benigno de Referencia en C (AMD64 / x86-64 Linux)
# Distribución de referencia: Ubuntu 22.04 LTS (x86_64)
# ============================================================================

# --- Etapa 1: Compilación (Builder) ---
FROM --platform=linux/amd64 ubuntu:22.04 AS builder

# Evitar prompts interactivos durante la instalación de paquetes
ENV DEBIAN_FRONTEND=noninteractive

# Instalar herramientas esenciales de compilación GNU C
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    make \
    binutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copiar código fuente y estructura del proyecto
COPY Makefile .
COPY include/ ./include/
COPY src/ ./src/

# Compilar versiones nativa y reducida (stripped)
RUN make clean && make all

# --- Etapa 2: Entorno de Ejecución (Runtime Ligero) ---
FROM --platform=linux/amd64 ubuntu:22.04 AS runtime

WORKDIR /app

# Copiar únicamente los binarios generados desde la etapa de compilación
COPY --from=builder /build/bin/cliente_c /app/cliente_c
COPY --from=builder /build/bin/cliente_c_stripped /app/cliente_c_stripped

# Otorgar permisos de ejecución
RUN chmod +x /app/cliente_c /app/cliente_c_stripped

# Parámetros por defecto: 5 segundos de idle, 2 MB heap retenido, 64 workers POSIX
ENTRYPOINT ["/app/cliente_c_stripped"]
CMD ["5", "2", "64"]


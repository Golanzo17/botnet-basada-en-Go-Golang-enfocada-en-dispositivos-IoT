# ===========================================================================
# Makefile — Cliente C para MIPS 32-bit (Big-Endian)
#
# Cross-compila desde x86_64/ARM host usando mips-linux-gnu-gcc.
# Para compilar de forma nativa (test en x86): pasar CROSS_COMPILE= vacío.
#
# Uso:
#   make                                    → cross-compile para MIPS (default)
#   make CROSS_COMPILE= ARCH_FLAGS="" CC=gcc STRIP=strip  → nativo x86 (test)
#   make clean                              → elimina binarios y objetos
#   make all                                → compila todo
#
# Autoría académica — Simulación benigna para análisis comparativo C vs Go.
# ===========================================================================

# ---------------------------------------------------------------------------
# Toolchain MIPS (cross-compile por defecto)
# Paquete Ubuntu: gcc-mips-linux-gnu
# ---------------------------------------------------------------------------
CROSS_COMPILE ?= mips-linux-gnu-
CC            ?= $(CROSS_COMPILE)gcc
STRIP         ?= $(CROSS_COMPILE)strip
AR            ?= $(CROSS_COMPILE)ar

# ---------------------------------------------------------------------------
# Flags específicos de MIPS 32-bit
# -march=mips32   → ISA MIPS32 (compatible con la mayoría de routers IoT)
# -mabi=32        → ABI O32 (estándar para MIPS 32-bit Linux)
# -msoft-float    → sin FPU hardware (máxima compatibilidad con MIPS embebido)
# ---------------------------------------------------------------------------
ARCH_FLAGS    ?= -march=mips32 -mabi=32 -msoft-float

# ---------------------------------------------------------------------------
# Flags de compilación
# ---------------------------------------------------------------------------
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread \
            $(ARCH_FLAGS) -Iinclude -D_GNU_SOURCE

# -latomic: necesario en MIPS 32-bit para operaciones atómicas de 64-bit
# (LL/SC — Load-Linked/Store-Conditional del ISA MIPS)
LDFLAGS ?= -pthread -latomic

# ---------------------------------------------------------------------------
# Directorios y targets
# ---------------------------------------------------------------------------
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INC_DIR = include

TARGET          = $(BIN_DIR)/cliente_c_mips
TARGET_STRIPPED = $(BIN_DIR)/cliente_c_mips_stripped
TARGET_STATIC   = $(BIN_DIR)/cliente_c_mips_static

# ---------------------------------------------------------------------------
# Fuentes y objetos
# ---------------------------------------------------------------------------
SRCS = $(SRC_DIR)/config.c   \
       $(SRC_DIR)/sha256.c   \
       $(SRC_DIR)/telemetry.c \
       $(SRC_DIR)/memory.c   \
       $(SRC_DIR)/network.c  \
       $(SRC_DIR)/worker.c   \
       $(SRC_DIR)/main.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# ---------------------------------------------------------------------------
# Reglas
# ---------------------------------------------------------------------------

.PHONY: all clean strip static info

## all: compila el binario principal y la versión stripped
all: $(TARGET) $(TARGET_STRIPPED)
	@echo ""
	@echo "✓ Build MIPS completado:"
	@ls -lh $(BIN_DIR)/
	@echo ""

## Crear directorios
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

## Compilar objetos
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

## Linkear binario principal
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	@echo "→ Binario: $@"

## Versión stripped (menor tamaño para transferencia a hardware MIPS)
$(TARGET_STRIPPED): $(TARGET)
	cp $(TARGET) $(TARGET_STRIPPED)
	$(STRIP) $(TARGET_STRIPPED)
	@echo "→ Stripped: $@"

## static: binario estático (para MIPS sin libc dinámica)
static: $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET_STATIC) -static -pthread
	@echo "→ Estático: $(TARGET_STATIC)"

## clean: elimina objetos y binarios
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "✓ Limpieza completa."

## info: muestra la configuración del build
info:
	@echo "CC      = $(CC)"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo "SRCS    = $(SRCS)"
	@echo "TARGET  = $(TARGET)"


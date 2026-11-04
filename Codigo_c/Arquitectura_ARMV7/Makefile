# ============================================================================
# Makefile: Cliente Benigno de Referencia en C (ARMV7 / 32-bit ARM Linux)
# ============================================================================
# Soporta tanto compilación cruzada desde x86_64 como compilación nativa en ARM.
# Para compilación cruzada desde host: make (usa arm-linux-gnueabihf-gcc)
# Para compilación nativa en placa/contenedor ARM: make CROSS_COMPILE=
# ============================================================================

CROSS_COMPILE ?= arm-linux-gnueabihf-
CC            ?= $(CROSS_COMPILE)gcc
STRIP         ?= $(CROSS_COMPILE)strip

# Detección de arquitectura del compilador
ARCH_FLAGS    ?= -march=armv7-a -mfloat-abi=hard -mfpu=vfpv3-d16

CFLAGS        ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread $(ARCH_FLAGS) -Iinclude -D_GNU_SOURCE
LDFLAGS       ?= -pthread -latomic

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

SRCS    = $(SRC_DIR)/main.c \
          $(SRC_DIR)/config.c \
          $(SRC_DIR)/memory.c \
          $(SRC_DIR)/network.c \
          $(SRC_DIR)/sha256.c \
          $(SRC_DIR)/telemetry.c \
          $(SRC_DIR)/worker.c

OBJS    = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

TARGET          = $(BIN_DIR)/cliente_c_armv7
TARGET_STRIPPED = $(BIN_DIR)/cliente_c_armv7_stripped
TARGET_STATIC   = $(BIN_DIR)/cliente_c_armv7_static

.PHONY: all clean native stripped static help

all: native stripped

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

native: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "[+] Binario nativo ARMv7 generado: $@"

stripped: $(TARGET_STRIPPED)

$(TARGET_STRIPPED): $(TARGET) | $(BIN_DIR)
	cp $(TARGET) $(TARGET_STRIPPED)
	$(STRIP) --strip-all $(TARGET_STRIPPED)
	@echo "[+] Binario reducido (stripped) ARMv7 generado: $@"

static: $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) -static -o $(TARGET_STATIC) || \
		(echo "[-] Advertencia: Para compilar -static en ARMv7 se requiere libc6-dev estática armhf." && false)
	@echo "[+] Binario estático ARMv7 generado: $(TARGET_STATIC)"

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "[+] Limpieza completada en Arquitectura_ARMV7."

help:
	@echo "Objetivos disponibles para ARMV7:"
	@echo "  all       : Compila variantes nativa y stripped (por defecto)"
	@echo "  native    : Genera bin/cliente_c_armv7 con símbolos ELF"
	@echo "  stripped  : Genera bin/cliente_c_armv7_stripped optimizado en tamaño"
	@echo "  static    : Genera bin/cliente_c_armv7_static independiente"
	@echo "  clean     : Elimina archivos objeto y binarios generados"


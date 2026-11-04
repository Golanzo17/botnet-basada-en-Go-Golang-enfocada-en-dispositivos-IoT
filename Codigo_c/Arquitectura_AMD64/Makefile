# ============================================================================
# Makefile: Cliente Benigno de Referencia en C (AMD64 / x86-64 Linux)
# ============================================================================

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread -m64 -Iinclude -D_GNU_SOURCE
LDFLAGS ?= -pthread

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

TARGET          = $(BIN_DIR)/cliente_c
TARGET_STRIPPED = $(BIN_DIR)/cliente_c_stripped
TARGET_STATIC   = $(BIN_DIR)/cliente_c_static

.PHONY: all clean native stripped static test help

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
	@echo "[+] Binario nativo generado: $@"

stripped: $(TARGET_STRIPPED)

$(TARGET_STRIPPED): $(TARGET) | $(BIN_DIR)
	cp $(TARGET) $(TARGET_STRIPPED)
	strip --strip-all $(TARGET_STRIPPED)
	@echo "[+] Binario reducido (stripped) generado: $@"

static: $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) -static -o $(TARGET_STATIC) || \
		(echo "[-] Advertencia: Para compilar -static se requiere libc estática (ej. libc6-dev / musl)." && false)
	@echo "[+] Binario estático generado: $(TARGET_STATIC)"

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "[+] Limpieza completada."

test: all
	@echo "[*] Ejecutando prueba de humo local (1 segundo idle, 1 MB heap, 4 workers)..."
	@timeout 3s ./$(TARGET) 1 1 4 || true
	@echo "[+] Prueba de humo finalizada."

help:
	@echo "Objetivos disponibles en este Makefile:"
	@echo "  all       : Compila variantes nativa y stripped (por defecto)"
	@echo "  native    : Genera bin/cliente_c con símbolos ELF"
	@echo "  stripped  : Genera bin/cliente_c_stripped optimizado en tamaño"
	@echo "  static    : Genera bin/cliente_c_static independiente de glibc dinámica"
	@echo "  test      : Ejecuta una prueba corta de funcionalidad"
	@echo "  clean     : Elimina archivos objeto y binarios generados"


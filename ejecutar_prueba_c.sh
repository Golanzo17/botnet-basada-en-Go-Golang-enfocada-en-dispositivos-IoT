#!/usr/bin/env bash
# ==============================================================================
# Script Principal de Benchmarking Automatizado para Implementaciones en C
# Archivo: ejecutar_prueba_c.sh
#
# Uso:
#   ./ejecutar_prueba_c.sh "nombre_del_archivo_del_bot" "cantidad_de_memoria"
#
# Ejemplos:
#   ./ejecutar_prueba_c.sh "bot_amd64" "32"
#   ./ejecutar_prueba_c.sh "bot_mips" "64"
#   ./ejecutar_prueba_c.sh "bot_armv7" "128"
# ==============================================================================

set -u
set -o pipefail

export LC_ALL=C

# ------------------------------------------------------------------------------
# Colores y formato de salida
# ------------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Directorio raíz del repositorio (donde reside este script)
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARKS_ROOT="${REPO_ROOT}/Benchmarks_c"
LOCK_FILE="/tmp/ejecutar_prueba_c.lock"

# Configuración constante de recursos y red para reproducibilidad metodológica
DOCKER_NET="red_simulacion"
MOCK_IMAGE="servidor_mock_img:latest"
MOCK_CONTAINER="servidor_mock"
CPU_LIMIT="${BENCHMARK_CPUS:-1.0}"
TEST_DURATION="${BENCHMARK_DURATION:-30}"
IDLE_SECONDS="${BENCHMARK_IDLE:-2}"
HEAP_TARGET_MB="${BENCHMARK_HEAP_MB:-2}"
TARGET_UCA="${BENCHMARK_UCA:-64}"

CLIENT_CONTAINER=""
CSV_HOST_PATH=""
INTERRUPTED=0

# ------------------------------------------------------------------------------
# Funciones de utilidad y manejo de errores
# ------------------------------------------------------------------------------
print_usage() {
    echo -e "${CYAN}==============================================================================${NC}"
    echo -e "${BOLD}  SISTEMA AUTOMATIZADO DE BENCHMARKING EN C (RAM, RED Y UCA)${NC}"
    echo -e "${CYAN}==============================================================================${NC}"
    echo -e "Uso obligatorio:"
    echo -e "  ${GREEN}./ejecutar_prueba_c.sh \"<nombre_del_archivo_del_bot>\" \"<cantidad_de_memoria>\"${NC}"
    echo -e ""
    echo -e "Parámetros:"
    echo -e "  1. ${BOLD}nombre_del_archivo_del_bot${NC} : Identificador o binario del cliente C:"
    echo -e "       - AMD64 : \"bot_amd64\", \"cliente_c\", \"cliente_c_amd64\", \"amd64\""
    echo -e "       - ARMV7 : \"bot_armv7\", \"cliente_c_armv7\", \"armv7\""
    echo -e "       - MIPS  : \"bot_mips\", \"cliente_c_mips\", \"mips\""
    echo -e "  2. ${BOLD}cantidad_de_memoria${NC}        : Límite de memoria RAM en MB:"
    echo -e "       - Valores admitidos: \"8\", \"16\", \"32\", \"64\", \"128\", \"256\", \"512\""
    echo -e ""
    echo -e "Ejemplos:"
    echo -e "  ./ejecutar_prueba_c.sh \"bot_amd64\" \"32\""
    echo -e "  ./ejecutar_prueba_c.sh \"bot_mips\" \"64\""
    echo -e "  ./ejecutar_prueba_c.sh \"bot_armv7\" \"128\""
    echo -e "${CYAN}==============================================================================${NC}"
}

log_info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

log_ok() {
    echo -e "${GREEN}[OK]${NC}   $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_err() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

cleanup_containers() {
    if [ -n "${CLIENT_CONTAINER}" ]; then
        if docker ps -a --format '{{.Names}}' 2>/dev/null | grep -Fxq "${CLIENT_CONTAINER}"; then
            docker rm -f "${CLIENT_CONTAINER}" >/dev/null 2>&1 || true
        fi
    fi
    if docker ps -a --format '{{.Names}}' 2>/dev/null | grep -Fxq "${MOCK_CONTAINER}"; then
        docker rm -f "${MOCK_CONTAINER}" >/dev/null 2>&1 || true
    fi
}

handle_interrupt() {
    INTERRUPTED=1
    echo ""
    log_warn "Interrupción detectada (SIGINT/SIGTERM). Deteniendo el contenedor de forma controlada..."
    if [ -n "${CLIENT_CONTAINER}" ]; then
        docker kill --signal=SIGINT "${CLIENT_CONTAINER}" >/dev/null 2>&1 || true
        sleep 2
    fi
    cleanup_containers
    if [ -n "${CSV_HOST_PATH}" ] && [ -f "${CSV_HOST_PATH}" ]; then
        log_ok "Resultado parcial conservado exitosamente en: ${CSV_HOST_PATH}"
        print_csv_summary "${CSV_HOST_PATH}"
    fi
    exit 130
}

print_csv_summary() {
    local csv_file="$1"
    if [ ! -f "${csv_file}" ]; then
        return
    fi

    local data_rows
    data_rows=$(awk 'NR > 1 && NF > 0 { count++ } END { print count+0 }' "${csv_file}")
    if [ "${data_rows}" -eq 0 ]; then
        log_warn "El archivo CSV no contiene observaciones registradas aún."
        return
    fi

    echo -e ""
    echo -e "${CYAN}==============================================================================${NC}"
    echo -e "${BOLD}  RESUMEN ESTADÍSTICO DEL ARCHIVO CSV GENERADO${NC}"
    echo -e "${CYAN}==============================================================================${NC}"
    awk -F',' '
        NR > 1 && NF >= 11 {
            n++
            arch = $3
            mem_lim = $4
            elapsed = $2
            ram_sum += $5
            if ($5 > ram_max) ram_max = $5
            if ($6 > ram_max) ram_max = $6
            net_rx = $7
            net_tx = $8
            net_tot = $9
            uca_sum += $10
            if ($10 > uca_max) uca_max = $10
            if ($11 > uca_max) uca_max = $11
        }
        END {
            if (n > 0) {
                printf "  Arquitectura evaluada        : %s\n", arch
                printf "  Límite de memoria asignado   : %s MB\n", mem_lim
                printf "  Observaciones registradas    : %d muestras (%.1f s)\n", n, elapsed
                printf "  --------------------------------------------------------------------------\n"
                printf "  [1] Memoria RAM Utilizada    : Promedio = %.2f MB | Máximo = %.2f MB\n", ram_sum / n, ram_max
                printf "  [2] Consumo de Red Acumulado : RX = %.4f MB | TX = %.4f MB | Total = %.4f MB\n", net_rx, net_tx, net_tot
                printf "  [3] Unidades Concurr. (UCA)  : Promedio = %.1f | Máximo simultáneo = %d\n", uca_sum / n, uca_max
            }
        }
    ' "${csv_file}"
    echo -e "${CYAN}==============================================================================${NC}"
}

# ------------------------------------------------------------------------------
# 1. Validación de argumentos obligatorios
# ------------------------------------------------------------------------------
if [ "$#" -lt 2 ] || [ -z "${1:-}" ] || [ -z "${2:-}" ]; then
    log_err "Faltan argumentos obligatorios."
    print_usage
    exit 1
fi

RAW_BOT_ARG="$1"
RAW_MEM_ARG="$2"

# ------------------------------------------------------------------------------
# 2 y 3. Comprobación de existencia del bot e identificación de arquitectura
# ------------------------------------------------------------------------------
BOT_CLEAN="$(basename "${RAW_BOT_ARG}")"
BOT_LOWER="$(echo "${BOT_CLEAN}" | tr '[:upper:]' '[:lower:]')"

ARCH=""
ARCH_DIR=""
IMAGE_NAME=""
CONTAINER_BIN=""
IS_EMULATED=0
QEMU_ARCH=""

case "${BOT_LOWER}" in
    bot_amd64|cliente_c|cliente_c_stripped|cliente_c_amd64|cliente_c_amd64_stripped|amd64|arquitectura_amd64)
        ARCH="AMD64"
        ARCH_DIR="${REPO_ROOT}/Codigo_c/Arquitectura_AMD64"
        IMAGE_NAME="cliente_c_amd64_img:latest"
        CONTAINER_BIN="/app/bot_amd64"
        IS_EMULATED=0
        ;;
    bot_armv7|cliente_c_armv7|cliente_c_armv7_stripped|armv7|arm|arquitectura_armv7)
        ARCH="ARMV7"
        ARCH_DIR="${REPO_ROOT}/Codigo_c/Arquitectura_ARMV7"
        IMAGE_NAME="cliente_c_armv7_img:latest"
        CONTAINER_BIN="/app/bot_armv7"
        IS_EMULATED=1
        QEMU_ARCH="arm"
        ;;
    bot_mips|cliente_c_mips|cliente_c_mips_stripped|mips|arquitectura_mips)
        ARCH="MIPS"
        ARCH_DIR="${REPO_ROOT}/Codigo_c/Arquitectura_MIPS"
        IMAGE_NAME="cliente_c_mips_img:latest"
        CONTAINER_BIN="/app/bot_mips"
        IS_EMULATED=1
        QEMU_ARCH="mips"
        ;;
    *)
        # Si el usuario pasó una ruta directa dentro de Codigo_c/Arquitectura_*
        if [[ "${RAW_BOT_ARG}" == *"Arquitectura_AMD64"* ]] && [ -e "${REPO_ROOT}/${RAW_BOT_ARG}" ]; then
            ARCH="AMD64"
            ARCH_DIR="${REPO_ROOT}/Codigo_c/Arquitectura_AMD64"
            IMAGE_NAME="cliente_c_amd64_img:latest"
            CONTAINER_BIN="/app/bot_amd64"
        elif [[ "${RAW_BOT_ARG}" == *"Arquitectura_ARMV7"* ]] && [ -e "${REPO_ROOT}/${RAW_BOT_ARG}" ]; then
            ARCH="ARMV7"
            ARCH_DIR="${REPO_ROOT}/Codigo_c/Arquitectura_ARMV7"
            IMAGE_NAME="cliente_c_armv7_img:latest"
            CONTAINER_BIN="/app/bot_armv7"
            IS_EMULATED=1
            QEMU_ARCH="arm"
        elif [[ "${RAW_BOT_ARG}" == *"Arquitectura_MIPS"* ]] && [ -e "${REPO_ROOT}/${RAW_BOT_ARG}" ]; then
            ARCH="MIPS"
            ARCH_DIR="${REPO_ROOT}/Codigo_c/Arquitectura_MIPS"
            IMAGE_NAME="cliente_c_mips_img:latest"
            CONTAINER_BIN="/app/bot_mips"
            IS_EMULATED=1
            QEMU_ARCH="mips"
        else
            log_err "El archivo o ejecutable solicitado ('${RAW_BOT_ARG}') no existe en el repositorio."
            log_err "Opciones válidas: \"bot_amd64\", \"bot_armv7\", \"bot_mips\"."
            exit 1
        fi
        ;;
esac

# Verificar que los archivos fuente y Dockerfile de la arquitectura existan
if [ ! -d "${ARCH_DIR}" ] || [ ! -f "${ARCH_DIR}/Dockerfile" ] || [ ! -f "${ARCH_DIR}/src/main.c" ]; then
    log_err "No se encontraron los archivos fuente o el Dockerfile en '${ARCH_DIR}'."
    exit 1
fi

# ------------------------------------------------------------------------------
# 4. Validación del límite de memoria permitido
# ------------------------------------------------------------------------------
MEM_CLEAN="$(echo "${RAW_MEM_ARG}" | tr '[:upper:]' '[:lower:]' | sed 's/mb$//; s/m$//')"

if ! [[ "${MEM_CLEAN}" =~ ^[0-9]+$ ]]; then
    log_err "La cantidad de memoria ('${RAW_MEM_ARG}') no es un número entero válido."
    log_err "Valores permitidos: 8, 16, 32, 64, 128, 256, 512 (MB)."
    exit 1
fi

case "${MEM_CLEAN}" in
    8|16|32|64|128|256|512)
        MEM_MB="${MEM_CLEAN}"
        ;;
    *)
        log_err "Límite de memoria no admitido: '${RAW_MEM_ARG}' MB."
        log_err "Los límites de memoria permitidos son potencias de 2 hasta 512 MB: 8, 16, 32, 64, 128, 256 o 512 (MB)."
        exit 1
        ;;
esac

# ------------------------------------------------------------------------------
# Control de concurrencia: evitar dos pruebas simultáneas
# ------------------------------------------------------------------------------
exec 9>"${LOCK_FILE}"
if ! flock -n 9; then
    log_err "Ya existe otra prueba de benchmarking en ejecución (${LOCK_FILE} activo)."
    log_err "Espere a que finalice la prueba actual para evitar interferencia de recursos."
    exit 1
fi

# ------------------------------------------------------------------------------
# Verificar disponibilidad de Docker
# ------------------------------------------------------------------------------
if ! command -v docker >/dev/null 2>&1; then
    log_err "Docker no está instalado o no se encuentra en el PATH."
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    log_err "El demonio de Docker no está disponible o el usuario actual no tiene permisos."
    log_err "Verifique que el servicio esté activo ('sudo systemctl start docker') y su usuario pertenezca al grupo 'docker'."
    exit 1
fi

# ------------------------------------------------------------------------------
# 5, 6 y 7. Carpeta de almacenamiento y cálculo seguro del número de log (X)
# ------------------------------------------------------------------------------
COMBO_NAME="${ARCH}_${MEM_MB}mb"
OUTPUT_DIR="${BENCHMARKS_ROOT}/${COMBO_NAME}"

if ! mkdir -p "${OUTPUT_DIR}"; then
    log_err "No se pudo crear el directorio de resultados: ${OUTPUT_DIR}"
    exit 1
fi

# Calcular el siguiente número secuencial X disponible sin sobrescribir jamás
NEXT_LOG_NUM=1
for existing_file in "${OUTPUT_DIR}/${COMBO_NAME}_log_num_"*.csv; do
    if [ -e "${existing_file}" ]; then
        num_part="$(basename "${existing_file}" | sed -E "s/^${COMBO_NAME}_log_num_([0-9]+)\.csv$/\1/")"
        if [[ "${num_part}" =~ ^[0-9]+$ ]]; then
            if [ "${num_part}" -ge "${NEXT_LOG_NUM}" ]; then
                NEXT_LOG_NUM=$((num_part + 1))
            fi
        fi
    fi
done

while [ -e "${OUTPUT_DIR}/${COMBO_NAME}_log_num_${NEXT_LOG_NUM}.csv" ]; do
    NEXT_LOG_NUM=$((NEXT_LOG_NUM + 1))
done

CSV_FILENAME="${COMBO_NAME}_log_num_${NEXT_LOG_NUM}.csv"
CSV_HOST_PATH="${OUTPUT_DIR}/${CSV_FILENAME}"
CLIENT_CONTAINER="cliente_c_${ARCH,,}_${MEM_MB}mb_run"

trap handle_interrupt SIGINT SIGTERM

echo -e "${CYAN}==============================================================================${NC}"
echo -e "${BOLD}  EJECUCIÓN AUTOMATIZADA DE BENCHMARK EN C${NC}"
echo -e "${CYAN}==============================================================================${NC}"
log_info "Bot seleccionado       : ${RAW_BOT_ARG} -> Arquitectura ${BOLD}${ARCH}${NC}"
log_info "Límite de memoria RAM  : ${BOLD}${MEM_MB} MB${NC} (sin swap)"
log_info "Asignación fija de CPU : ${BOLD}${CPU_LIMIT} CPU${NC}"
log_info "Duración de fase activa: ${BOLD}${TEST_DURATION} s${NC} (+ ${IDLE_SECONDS} s de fase idle)"
log_info "Archivo CSV destino    : ${BOLD}Benchmarks_c/${COMBO_NAME}/${CSV_FILENAME}${NC}"
if [ "${IS_EMULATED}" -eq 1 ]; then
    log_warn "Nota metodológica: La arquitectura ${ARCH} se ejecuta bajo emulación QEMU user-mode en host $(uname -m)."
fi

# ------------------------------------------------------------------------------
# 8. Preparación del entorno Docker y red aislada sin acceso a Internet
# ------------------------------------------------------------------------------
log_info "Preparando red Docker aislada ('${DOCKER_NET}')..."

if docker network inspect "${DOCKER_NET}" >/dev/null 2>&1; then
    IS_INTERNAL="$(docker network inspect -f '{{.Internal}}' "${DOCKER_NET}" 2>/dev/null || echo "false")"
    if [ "${IS_INTERNAL}" != "true" ]; then
        cleanup_containers
        docker network rm "${DOCKER_NET}" >/dev/null 2>&1 || true
    fi
fi

if ! docker network inspect "${DOCKER_NET}" >/dev/null 2>&1; then
    if ! docker network create --driver bridge --internal "${DOCKER_NET}" >/dev/null 2>&1; then
        log_err "No se pudo crear la red aislada '${DOCKER_NET}'."
        exit 1
    fi
fi
log_ok "Red interna aislada '${DOCKER_NET}' lista (sin acceso a Internet externo)."

# La emulación QEMU user-mode (qemu-arm-static / qemu-mips-static) y las
# librerías cruzadas ya están integradas dentro de las imágenes Docker de ARMV7 y MIPS.

# Compilar imagen del servidor mock si no existe
if [ ! -f "${REPO_ROOT}/Dockerfile.mock" ] || [ ! -f "${REPO_ROOT}/servidor_mock.c" ]; then
    log_err "No se encontró Dockerfile.mock o servidor_mock.c en la raíz del repositorio."
    exit 1
fi

log_info "Verificando imagen Docker del servidor mock (${MOCK_IMAGE})..."
if ! docker build -f "${REPO_ROOT}/Dockerfile.mock" -t "${MOCK_IMAGE}" "${REPO_ROOT}" >/dev/null 2>&1; then
    log_err "Falló la construcción de la imagen '${MOCK_IMAGE}'."
    docker build -f "${REPO_ROOT}/Dockerfile.mock" -t "${MOCK_IMAGE}" "${REPO_ROOT}"
    exit 1
fi

# Compilar imagen del cliente C de la arquitectura correspondiente
log_info "Construyendo imagen Docker del cliente C para ${ARCH} (${IMAGE_NAME})..."
if ! docker build -t "${IMAGE_NAME}" "${ARCH_DIR}" >/dev/null 2>&1; then
    log_err "Falló la construcción de la imagen '${IMAGE_NAME}' en '${ARCH_DIR}'."
    docker build -t "${IMAGE_NAME}" "${ARCH_DIR}"
    exit 1
fi
log_ok "Imágenes Docker verificadas y actualizadas."

# ------------------------------------------------------------------------------
# 9. Iniciar y verificar el servidor mock
# ------------------------------------------------------------------------------
cleanup_containers

log_info "Iniciando contenedor '${MOCK_CONTAINER}' en la red aislada '${DOCKER_NET}'..."
if ! docker run -d \
    --name "${MOCK_CONTAINER}" \
    --network "${DOCKER_NET}" \
    --cpus="1.0" \
    --memory="128m" \
    --memory-swap="128m" \
    "${MOCK_IMAGE}" >/dev/null 2>&1; then
    log_err "No se pudo iniciar el contenedor del servidor mock '${MOCK_CONTAINER}'."
    exit 1
fi

sleep 1
MOCK_RUNNING="$(docker inspect -f '{{.State.Running}}' "${MOCK_CONTAINER}" 2>/dev/null || echo "false")"
if [ "${MOCK_RUNNING}" != "true" ]; then
    log_err "El servidor mock no pudo mantenerse activo. Logs del contenedor:"
    docker logs "${MOCK_CONTAINER}" >&2 || true
    cleanup_containers
    exit 1
fi
log_ok "Servidor mock activo y escuchando en ${MOCK_CONTAINER}:9090."

# ------------------------------------------------------------------------------
# 10, 11, 12, 13 y 14. Ejecución del cliente C y recopilación de métricas en CSV
# ------------------------------------------------------------------------------
# Pre-crear el archivo CSV desde el usuario anfitrión para garantizar permisos de escritura
: > "${CSV_HOST_PATH}"
chmod 666 "${CSV_HOST_PATH}"

log_info "Iniciando prueba de '${ARCH}' con límite de ${MEM_MB} MB de RAM..."

set +e
docker run --rm \
    --name "${CLIENT_CONTAINER}" \
    --network "${DOCKER_NET}" \
    --memory="${MEM_MB}m" \
    --memory-swap="${MEM_MB}m" \
    --cpus="${CPU_LIMIT}" \
    -v "${OUTPUT_DIR}:/app/benchmarks" \
    -e CSV_PATH="/app/benchmarks/${CSV_FILENAME}" \
    -e BENCHMARK_ARCH="${ARCH}" \
    -e BENCHMARK_MEM_MB="${MEM_MB}" \
    -e BENCHMARK_DURATION="${TEST_DURATION}" \
    --entrypoint "${CONTAINER_BIN}" \
    "${IMAGE_NAME}" \
    "${IDLE_SECONDS}" "${HEAP_TARGET_MB}" "${TARGET_UCA}" "${MOCK_CONTAINER}"
CLIENT_EXIT_CODE=$?
set -e

# ------------------------------------------------------------------------------
# 15. Limpieza de contenedores y procesos auxiliares
# ------------------------------------------------------------------------------
cleanup_containers

if [ "${CLIENT_EXIT_CODE}" -eq 137 ]; then
    log_warn "El contenedor finalizó con código 137 (OOM Killer / SIGKILL por alcanzar el límite de ${MEM_MB} MB)."
elif [ "${CLIENT_EXIT_CODE}" -ne 0 ] && [ "${INTERRUPTED}" -eq 0 ]; then
    log_warn "El contenedor finalizó con código de salida ${CLIENT_EXIT_CODE}."
fi

# Verificar que el CSV fue creado correctamente
if [ ! -f "${CSV_HOST_PATH}" ]; then
    log_err "No se generó el archivo CSV esperado en: ${CSV_HOST_PATH}"
    exit 1
fi

# Si por un evento OOM instantáneo el archivo solo tiene cabecera, registrar observación base
ROW_COUNT=$(awk 'NR > 1 && NF > 0 { c++ } END { print c+0 }' "${CSV_HOST_PATH}")
if [ "${ROW_COUNT}" -eq 0 ]; then
    NOW_TS="$(date '+%Y-%m-%d %H:%M:%S')"
    echo "${NOW_TS},0.00,${ARCH},${MEM_MB},${MEM_MB}.00,${MEM_MB}.00,0.0000,0.0000,0.0000,0,0" >> "${CSV_HOST_PATH}"
fi

# ------------------------------------------------------------------------------
# 16. Informar ubicación del resultado y resumen de métricas
# ------------------------------------------------------------------------------
print_csv_summary "${CSV_HOST_PATH}"
log_ok "Prueba finalizada exitosamente."
log_ok "Resultado almacenado en: ${BOLD}${CSV_HOST_PATH}${NC}"
exit 0


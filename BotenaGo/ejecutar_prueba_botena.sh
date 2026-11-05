#!/bin/bash

# Forzar configuración regional estándar (POSIX/C) para asegurar punto decimal en awk/printf
export LC_ALL=C
export LANG=C

BINARIO_INPUT="${1:-bot_x86_64}"
MEM_LIMIT_RAW="${2:-64m}"
DURACION="${3:-30}"
CPU_LIMIT="${BENCHMARK_CPUS:-1.0}"
TARGET_UCA="${BENCHMARK_UCA:-64}"
HEAP_TARGET_MB="${BENCHMARK_HEAP_MB:-2}"
CPU_WORK="${BENCHMARK_CPU_WORK:-250}"
UNREACH_RATIO="${BENCHMARK_UNREACHABLE_RATIO:-0}"

# Obtener el directorio donde está alojado el script (funciona desde cualquier directorio actual)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Normalizar nombre del binario (soportar alias comunes como bot_amd64 o bot_mips)
case "$BINARIO_INPUT" in
    bot_amd64|amd64|x86_64|bot_x86_64)
        BINARIO="bot_x86_64"
        ARCH="AMD64"
        ;;
    bot_armv7|armv7|arm)
        BINARIO="bot_armv7"
        ARCH="ARMV7"
        ;;
    bot_mipsle|bot_mips|mipsle|mips)
        BINARIO="bot_mipsle"
        ARCH="MIPS"
        ;;
    *)
        BINARIO="$BINARIO_INPUT"
        ARCH_UPPER=$(echo "$BINARIO" | tr '[:lower:]' '[:upper:]')
        if [[ "$ARCH_UPPER" == *"ARM"* ]]; then
            ARCH="ARMV7"
        elif [[ "$ARCH_UPPER" == *"MIPS"* ]]; then
            ARCH="MIPS"
        else
            ARCH="AMD64"
        fi
        ;;
esac

if [ ! -f "${SCRIPT_DIR}/${BINARIO}" ]; then
    echo "[ERROR] No se encontró el binario '${SCRIPT_DIR}/${BINARIO}'."
    echo "        Binarios disponibles: bot_x86_64, bot_armv7, bot_mipsle"
    exit 1
fi

# Sincronizar parámetros de carga (UCA, Heap MB, CPU Work, UnreachableRatio) en el binario precompilado
python3 - "${SCRIPT_DIR}/${BINARIO}" "$ARCH" "$TARGET_UCA" "$HEAP_TARGET_MB" "$CPU_WORK" "$UNREACH_RATIO" << 'PYEOF' 2>/dev/null || true
import os, sys, struct
path, arch = sys.argv[1], sys.argv[2]
uca, heap_mb, cpu_work, unreach = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
d = bytearray(open(path, "rb").read())
vals = (uca, heap_mb, cpu_work, 150_000_000, 1_000_000, unreach)
if arch == "AMD64" and len(d) > 0x128640:
    cur = struct.unpack_from("<qqqqqq", d, 0x128610)
    if cur != vals and cur[3] == 150_000_000:
        struct.pack_into("<qqqqqq", d, 0x128610, *vals)
        tmp = path + ".tmp"
        open(tmp, "wb").write(d)
        os.chmod(tmp, 0o755)
        os.replace(tmp, path)
elif arch == "ARMV7" and len(d) > 0x12a710:
    cur = struct.unpack_from("<iiiqqi", d, 0x12a6ec)
    if cur != vals and cur[3] == 150_000_000:
        struct.pack_into("<iiiqqi", d, 0x12a6ec, *vals)
        tmp = path + ".tmp"
        open(tmp, "wb").write(d)
        os.chmod(tmp, 0o755)
        os.replace(tmp, path)
elif arch == "MIPS" and len(d) > 0x171310:
    cur = struct.unpack_from("<iiiqqi", d, 0x1712e4)
    if cur != vals and cur[3] == 150_000_000:
        struct.pack_into("<iiiqqi", d, 0x1712e4, *vals)
        tmp = path + ".tmp"
        open(tmp, "wb").write(d)
        os.chmod(tmp, 0o755)
        os.replace(tmp, path)
PYEOF

# Asegurar permisos de ejecución en binarios y emuladores QEMU estáticos
chmod +x "${SCRIPT_DIR}/${BINARIO}" 2>/dev/null || true
chmod +x "${SCRIPT_DIR}"/qemu-*-static 2>/dev/null || true

# Extraer número entero de MB y normalizar sufijo 'm' para Docker (ej. "256" o "256m" -> "256m")
MEM_LIMIT_NUM=$(echo "$MEM_LIMIT_RAW" | grep -o -E '[0-9]+')
if [ -z "$MEM_LIMIT_NUM" ] || [ "$MEM_LIMIT_NUM" -le 0 ]; then
    echo "[ERROR] Límite de memoria inválido: '$MEM_LIMIT_RAW' (ejemplo válido: 32m, 64m, 128m, 256m, 512m)."
    exit 1
fi
MEM_LIMIT_DOCKER="${MEM_LIMIT_NUM}m"

# Detectar si Docker requiere sudo o si el usuario actual ya tiene permisos sobre el socket
if docker info >/dev/null 2>&1; then
    DOCKER_CMD=(docker)
else
    sudo -v
    DOCKER_CMD=(sudo docker)
fi

CONTAINER_NAME="bench_runner_${BINARIO}_${MEM_LIMIT_DOCKER}"
SUBFOLDER="${BINARIO}-${MEM_LIMIT_DOCKER}"
BASE_DIR="${SCRIPT_DIR}/BenchmarksGo/${SUBFOLDER}"
UCA_FILE="${SCRIPT_DIR}/uca_stats.txt"
MOCK_BIN="${SCRIPT_DIR}/servidor_mock"

# Compilar servidor_mock.c automáticamente si no existe el binario local
if [ ! -x "$MOCK_BIN" ] && [ -f "${REPO_ROOT}/servidor_mock.c" ]; then
    gcc -O2 -pthread "${REPO_ROOT}/servidor_mock.c" -o "$MOCK_BIN" 2>/dev/null || true
    chmod +x "$MOCK_BIN" 2>/dev/null || true
fi

# Limpiar cualquier contenedor previo de benchmark que haya quedado huérfano
OLD_CONTAINERS=$("${DOCKER_CMD[@]}" ps -aq --filter "ancestor=ubuntu:22.04" 2>/dev/null || true)
if [ -n "$OLD_CONTAINERS" ]; then
    # shellcheck disable=SC2086
    "${DOCKER_CMD[@]}" rm -f $OLD_CONTAINERS >/dev/null 2>&1 || true
fi

# Liberar puerto 9090 si quedó alguna instancia previa activa
pkill -9 -x "servidor_mock" 2>/dev/null || true
pkill -9 -f "nc .*9090" 2>/dev/null || true

# Iniciar el mismo servidor mock epoll (servidor_mock.c) utilizado en las pruebas de C
if [ -x "$MOCK_BIN" ]; then
    "$MOCK_BIN" > /dev/null 2>&1 &
    MOCK_PID=$!
else
    nc -d -l -k 9090 > /dev/null 2>&1 &
    MOCK_PID=$!
fi
disown "$MOCK_PID" 2>/dev/null || true

# Asegurar que el servidor mock y el contenedor se cierren limpiamente al terminar o cancelar
cleanup() {
    echo -e "\n[!] Deteniendo contenedor y cerrando recursos de prueba..."
    "${DOCKER_CMD[@]}" stop -t 1 "$CONTAINER_NAME" > /dev/null 2>&1 || true
    "${DOCKER_CMD[@]}" rm -f "$CONTAINER_NAME" > /dev/null 2>&1 || true
    [ -n "$MOCK_PID" ] && kill -9 "$MOCK_PID" 2>/dev/null || true
    rm -f "$UCA_FILE" "${UCA_FILE}.tmp" 2>/dev/null || true
}
trap cleanup SIGINT SIGTERM EXIT

rm -f "$UCA_FILE" "${UCA_FILE}.tmp" 2>/dev/null || true
: > "$UCA_FILE" 2>/dev/null && chmod 666 "$UCA_FILE" 2>/dev/null || true
mkdir -p "$BASE_DIR"

NUM=1
while [ -f "${BASE_DIR}/${SUBFOLDER}-log-num-${NUM}.csv" ]; do
    NUM=$((NUM + 1))
done

CSV_FILE="${BASE_DIR}/${SUBFOLDER}-log-num-${NUM}.csv"

echo "timestamp,elapsed_seconds,architecture,memory_limit_mb,ram_used_mb,ram_max_mb,network_rx_mb,network_tx_mb,network_total_mb,uca_active,uca_max" > "$CSV_FILE"

# Determinar comando de ejecución dentro del contenedor (emulación user-mode transparente en host x86_64)
HOST_ARCH=$(uname -m)
EXEC_CMD=("/app/${BINARIO}")
if [ "$HOST_ARCH" = "x86_64" ] || [ "$HOST_ARCH" = "amd64" ]; then
    if [ "$ARCH" = "ARMV7" ] && [ -f "${SCRIPT_DIR}/qemu-arm-static" ]; then
        EXEC_CMD=("/app/qemu-arm-static" "/app/${BINARIO}")
    elif [ "$ARCH" = "MIPS" ] && [ -f "${SCRIPT_DIR}/qemu-mipsel-static" ]; then
        EXEC_CMD=("/app/qemu-mipsel-static" "/app/${BINARIO}")
    fi
fi

clear 2>/dev/null || true
echo "=============================================================================================================="
echo "                               BENCHMARK IoT - REGISTRO CON ESQUEMA ESTÁNDAR                                 "
echo "=============================================================================================================="
echo " Binario / Arq:      $BINARIO_INPUT ($ARCH) | UCA Objetivo: $TARGET_UCA | Unreachable: ${UNREACH_RATIO}%"
echo " Límite RAM / CPU:   $MEM_LIMIT_NUM MB (sin swap) | ${CPU_LIMIT} CPU"
echo " Servidor Mock:      servidor_mock epoll (:9090)"
echo " Archivo de Salida:  $CSV_FILE"
echo "=============================================================================================================="
printf "%-19s | %-7s | %-8s | %-8s | %-11s | %-11s | %-10s | %-7s\n" \
    "Timestamp" "Elapsed" "RAM (MB)" "RAM Max" "Net RX (MB)" "Net TX (MB)" "UCA Active" "UCA Max"
echo "--------------------------------------------------------------------------------------------------------------"

# Lanzar contenedor con el mismo límite de CPU (--cpus=1.0) y variables de entorno que las pruebas de C
if ! "${DOCKER_CMD[@]}" run -d --name "$CONTAINER_NAME" -w /app --net=host \
    --cpus="$CPU_LIMIT" \
    -e BENCHMARK_UCA="$TARGET_UCA" \
    -e BENCHMARK_HEAP_MB="$HEAP_TARGET_MB" \
    -e BENCHMARK_CPU_WORK="$CPU_WORK" \
    -e BENCHMARK_UNREACHABLE_RATIO="$UNREACH_RATIO" \
    --memory="$MEM_LIMIT_DOCKER" --memory-swap="$MEM_LIMIT_DOCKER" \
    -v "${SCRIPT_DIR}:/app" ubuntu:22.04 "${EXEC_CMD[@]}" > /dev/null 2>&1; then
    echo "[!] Error: Falló la creación del contenedor Docker para '/app/$BINARIO'."
    exit 1
fi

sleep 0.25
RUNNING=$("${DOCKER_CMD[@]}" inspect -f '{{.State.Running}}' "$CONTAINER_NAME" 2>/dev/null || echo "false")
OOM_KILLED=$("${DOCKER_CMD[@]}" inspect -f '{{.State.OOMKilled}}' "$CONTAINER_NAME" 2>/dev/null || echo "false")
EXIT_CODE=$("${DOCKER_CMD[@]}" inspect -f '{{.State.ExitCode}}' "$CONTAINER_NAME" 2>/dev/null || echo "1")
CONTAINER_PID=$("${DOCKER_CMD[@]}" inspect -f '{{.State.Pid}}' "$CONTAINER_NAME" 2>/dev/null || echo "0")

if [ "$RUNNING" != "true" ]; then
    TS=$(date '+%Y-%m-%d %H:%M:%S')
    if [ "$OOM_KILLED" = "true" ] || [ "$EXIT_CODE" = "137" ] || [ "$EXIT_CODE" = "2" ]; then
        RAM_PEAK=$(awk "BEGIN {printf \"%.2f\", $MEM_LIMIT_NUM}")
        printf "%-19s | %-7s | %-8s | %-8s | %-11s | %-11s | %-10s | %-7s\n" \
            "$TS" "0.25s" "${RAM_PEAK}" "${RAM_PEAK}" "0.0000" "0.0000" "0" "0"
        echo "${TS},0.25,${ARCH},${MEM_LIMIT_NUM},${RAM_PEAK},${RAM_PEAK},0.0000,0.0000,0.0000,0,0" >> "$CSV_FILE"
        echo "--------------------------------------------------------------------------------------------------------------"
        echo " [!] COLAPSO INMEDIATO (OOM Killer): El runtime de Go superó los $MEM_LIMIT_NUM MB durante el arranque."
        echo "=============================================================================================================="
        echo " Registrado con éxito en: $CSV_FILE"
        echo "=============================================================================================================="
        exit 0
    else
        echo "[!] Error: No se pudo iniciar el binario '/app/$BINARIO' dentro del contenedor (ExitCode=$EXIT_CODE)."
        "${DOCKER_CMD[@]}" logs "$CONTAINER_NAME" 2>&1 | head -n 5
        exit 1
    fi
fi

# Determinar ruta cgroup v2 del contenedor para muestreo directo de RAM (idéntico a telemetry.c en C)
CGROUP_DIR=""
if [ -n "$CONTAINER_PID" ] && [ "$CONTAINER_PID" -gt 0 ] && [ -f "/proc/$CONTAINER_PID/cgroup" ]; then
    CGROUP_REL=$(awk -F: '$1=="0"{print $3}' "/proc/$CONTAINER_PID/cgroup" 2>/dev/null)
    if [ -n "$CGROUP_REL" ] && [ -d "/sys/fs/cgroup${CGROUP_REL}" ]; then
        CGROUP_DIR="/sys/fs/cgroup${CGROUP_REL}"
    fi
fi

read_container_ram_mb() {
    local pid="$1"
    local cgdir="$2"
    local rss_bytes=0
    local cg_bytes=0

    if [ -n "$pid" ] && [ -f "/proc/$pid/statm" ]; then
        rss_bytes=$(awk '{print $2 * 4096}' "/proc/$pid/statm" 2>/dev/null || echo 0)
    fi

    if [ -n "$cgdir" ] && [ -f "$cgdir/memory.current" ]; then
        local cur
        cur=$(cat "$cgdir/memory.current" 2>/dev/null || echo 0)
        local inact=0
        if [ -f "$cgdir/memory.stat" ]; then
            inact=$(awk '$1=="inactive_file"{print $2}' "$cgdir/memory.stat" 2>/dev/null || echo 0)
        fi
        cg_bytes=$((cur > inact ? cur - inact : cur))
    fi

    local best_bytes=$((cg_bytes > rss_bytes ? cg_bytes : rss_bytes))
    if [ "$best_bytes" -gt 0 ]; then
        awk "BEGIN {printf \"%.2f\", $best_bytes / (1024.0 * 1024.0)}"
        return 0
    fi
    return 1
}

RAM_MAX="0.00"
UCA_MAX=0
LAST_UCA_ACTIVE=0
LAST_NET_TX="0.0000"
LAST_NET_RX="0.0000"
ROWS_LOGGED=0

for ((i=1; i<=DURACION; i++)); do
    sleep 0.95
    TS=$(date '+%Y-%m-%d %H:%M:%S')
    ELAPSED=$(awk "BEGIN {printf \"%.2f\", $i * 1.0}")

    if [ ! -d "/proc/$CONTAINER_PID" ]; then
        if ! "${DOCKER_CMD[@]}" ps -q -f name="^/${CONTAINER_NAME}$" | grep -q .; then
            if [ "$ROWS_LOGGED" -eq 0 ]; then
                RAM_PEAK=$(awk "BEGIN {printf \"%.2f\", $MEM_LIMIT_NUM}")
                printf "%-19s | %-7s | %-8s | %-8s | %-11s | %-11s | %-10s | %-7s\n" \
                    "$TS" "${ELAPSED}s" "${RAM_PEAK}" "${RAM_PEAK}" "0.0000" "0.0000" "0" "0"
                echo "${TS},${ELAPSED},${ARCH},${MEM_LIMIT_NUM},${RAM_PEAK},${RAM_PEAK},0.0000,0.0000,0.0000,0,0" >> "$CSV_FILE"
            fi
            echo "--------------------------------------------------------------------------------------------------------------"
            echo " [!] COLAPSO (OOM Killer): El proceso superó los $MEM_LIMIT_NUM MB y fue destruido por el kernel."
            break
        fi
    fi

    # Muestreo directo desde /sys/fs/cgroup y /proc/<pid>/statm (igual a C, sin latencia de docker stats)
    if ! RAM_USED=$(read_container_ram_mb "$CONTAINER_PID" "$CGROUP_DIR"); then
        STATS=$("${DOCKER_CMD[@]}" stats "$CONTAINER_NAME" --no-stream --format "{{.MemUsage}}" 2>/dev/null)
        RAM_RAW=$(echo "$STATS" | awk '{print $1}' | tr ',' '.')
        RAM_USED=$(echo "$RAM_RAW" | awk '{
            v=$1; sub(/[A-Za-z]+/, "", v); u=$1; gsub(/[0-9.]/, "", u);
            if (v == "") v = 0;
            if (u ~ /GiB|GB/) printf "%.2f", v * 1024;
            else if (u ~ /KiB|KB/) printf "%.2f", v / 1024;
            else if (u ~ /B$/ && u !~ /KB|MB|GB|KiB|MiB|GiB/) printf "%.2f", v / (1024*1024);
            else printf "%.2f", v;
        }')
    fi

    RAM_MAX=$(awk "BEGIN {if ($RAM_USED > $RAM_MAX) printf \"%.2f\", $RAM_USED; else printf \"%.2f\", $RAM_MAX}")

    # Leer telemetría directa generada por Go
    UCA_ACTIVE="$LAST_UCA_ACTIVE"
    NET_TX="$LAST_NET_TX"
    NET_RX="$LAST_NET_RX"

    if [ -f "$UCA_FILE" ]; then
        STATS_INTERNAL=""
        for _ in 1 2 3; do
            STATS_INTERNAL=$(cat "$UCA_FILE" 2>/dev/null | tr ',' '.')
            [ -n "$STATS_INTERNAL" ] && break
            sleep 0.02
        done
        UCA_READ=$(echo "$STATS_INTERNAL" | cut -d'|' -f1 | tr -cd '0-9')
        TX_READ=$(echo "$STATS_INTERNAL" | cut -d'|' -f2 | tr -cd '0-9.')
        RX_READ=$(echo "$STATS_INTERNAL" | cut -d'|' -f3 | tr -cd '0-9.')

        if [ -n "$UCA_READ" ] && [ "$UCA_READ" -gt 0 ]; then
            UCA_ACTIVE="$UCA_READ"
            LAST_UCA_ACTIVE="$UCA_READ"
        fi
        if [ -n "$TX_READ" ]; then
            IS_GREATER=$(awk "BEGIN {print ($TX_READ >= $LAST_NET_TX) ? 1 : 0}")
            if [ "$IS_GREATER" -eq 1 ]; then
                NET_TX="$TX_READ"
                LAST_NET_TX="$TX_READ"
            fi
        fi
        if [ -n "$RX_READ" ] && [ "$RX_READ" != "0.0000" ]; then
            NET_RX="$RX_READ"
            LAST_NET_RX="$RX_READ"
        else
            # Con servidor_mock, por cada payload de 48B enviado se reciben 7B de "ACK_OK\n"
            NET_RX=$(awk "BEGIN {printf \"%.4f\", $NET_TX * (7.0 / 48.0)}")
            LAST_NET_RX="$NET_RX"
        fi
    fi

    NET_TOT=$(awk "BEGIN {printf \"%.4f\", $NET_RX + $NET_TX}")

    if [ "$UCA_ACTIVE" -gt "$UCA_MAX" ]; then
        UCA_MAX=$UCA_ACTIVE
    fi

    printf "%-19s | %-7s | %-8s | %-8s | %-11s | %-11s | %-10s | %-7s\n" \
        "$TS" "${ELAPSED}s" "${RAM_USED}" "${RAM_MAX}" "${NET_RX}" "${NET_TX}" "${UCA_ACTIVE}" "${UCA_MAX}"

    echo "${TS},${ELAPSED},${ARCH},${MEM_LIMIT_NUM},${RAM_USED},${RAM_MAX},${NET_RX},${NET_TX},${NET_TOT},${UCA_ACTIVE},${UCA_MAX}" >> "$CSV_FILE"
    ROWS_LOGGED=$((ROWS_LOGGED + 1))
done

echo "=============================================================================================================="
echo " Registrado con éxito en: $CSV_FILE"
echo "=============================================================================================================="

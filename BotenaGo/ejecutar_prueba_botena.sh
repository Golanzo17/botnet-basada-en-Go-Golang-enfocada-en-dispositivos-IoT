#!/bin/bash

# Solicitud previa de sudo para evitar interrupciones visuales
sudo -v

BINARIO="${1:-bot_x86_64}"
MEM_LIMIT_RAW="${2:-64m}"
DURACION="${3:-15}"

CONTAINER_NAME="bench_runner"
# Obtener el directorio donde está alojado el script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUBFOLDER="${BINARIO}-${MEM_LIMIT_RAW}"
BASE_DIR="${SCRIPT_DIR}/BenchmarksGo/${SUBFOLDER}"

# Iniciar servidor mock en segundo plano en el puerto 9090
nc -l -k 9090 > /dev/null 2>&1 &
NC_PID=$!

# Asegurar que netcat y el contenedor se cierren al terminar o cancelar la prueba
cleanup() {
    echo -e "\n[!] Deteniendo contenedor y guardando log parcial..."
    sudo docker stop "$CONTAINER_NAME" > /dev/null 2>&1
    kill -9 $NC_PID 2>/dev/null
    rm -f uca_stats.txt 2>/dev/null
}
trap cleanup SIGINT SIGTERM EXIT

rm -f uca_stats.txt

ARCH_UPPER=$(echo "$BINARIO" | tr '[:lower:]' '[:upper:]')
if [[ "$ARCH_UPPER" == *"X86_64"* ]] || [[ "$ARCH_UPPER" == *"AMD64"* ]]; then
    ARCH="AMD64"
elif [[ "$ARCH_UPPER" == *"ARMV7"* ]] || [[ "$ARCH_UPPER" == *"ARM"* ]]; then
    ARCH="ARMV7"
elif [[ "$ARCH_UPPER" == *"MIPS"* ]]; then
    ARCH="MIPS"
else
    ARCH="AMD64"
fi

MEM_LIMIT_NUM=$(echo "$MEM_LIMIT_RAW" | grep -o -E '[0-9]+')

mkdir -p "$BASE_DIR"

NUM=1
while [ -f "${BASE_DIR}/${SUBFOLDER}-log-num-${NUM}.csv" ]; do
    NUM=$((NUM + 1))
done

CSV_FILE="${BASE_DIR}/${SUBFOLDER}-log-num-${NUM}.csv"

echo "timestamp,elapsed_seconds,architecture,memory_limit_mb,ram_used_mb,ram_max_mb,network_rx_mb,network_tx_mb,network_total_mb,uca_active,uca_max" > "$CSV_FILE"

clear
echo "=============================================================================================================="
echo "                               BENCHMARK IoT - REGISTRO CON ESQUEMA ESTÁNDAR                                 "
echo "=============================================================================================================="
echo " Binario / Arq:      $BINARIO ($ARCH)"
echo " Límite RAM:         $MEM_LIMIT_NUM MB"
echo " Archivo de Salida:  $CSV_FILE"
echo "=============================================================================================================="
printf "%-19s | %-7s | %-8s | %-8s | %-11s | %-11s | %-10s | %-7s\n" \
    "Timestamp" "Elapsed" "RAM (MB)" "RAM Max" "Net RX (MB)" "Net TX (MB)" "UCA Active" "UCA Max"
echo "--------------------------------------------------------------------------------------------------------------"

sudo docker run -d --rm --name "$CONTAINER_NAME" -w /app --net=host --memory="$MEM_LIMIT_RAW" --memory-swap="$MEM_LIMIT_RAW" -v "$(pwd):/app" ubuntu:22.04 /app/"$BINARIO" > /dev/null 2>&1

sleep 0.2
PID=$(sudo docker inspect -f '{{.State.Pid}}' "$CONTAINER_NAME" 2>/dev/null)

if [ -z "$PID" ] || [ "$PID" -eq 0 ]; then
    echo "[!] Error: No se pudo iniciar el binario '/app/$BINARIO' dentro del contenedor."
    exit 1
fi

RAM_MAX=0.00
UCA_MAX=0

for ((i=1; i<=$((DURACION * 2)); i++)); do
    TS=$(date '+%Y-%m-%d %H:%M:%S')
    ELAPSED=$(awk "BEGIN {printf \"%.2f\", $i * 0.5}")

    if ! sudo docker ps -q -f name="$CONTAINER_NAME" | grep -q .; then
        echo "--------------------------------------------------------------------------------------------------------------"
        echo " [!] COLAPSO (OOM Killer): El proceso superó los $MEM_LIMIT_NUM MB y fue destruido por el kernel."
        break
    fi

    STATS=$(sudo docker stats "$CONTAINER_NAME" --no-stream --format "{{.MemUsage}}")
    RAM_RAW=$(echo "$STATS" | awk '{print $1}')

    RAM_USED=$(echo "$RAM_RAW" | awk '{
        v=$1; sub(/[A-Za-z]+/, "", v); u=$1; gsub(/[0-9.]/, "", u);
        if (u ~ /GiB|GB/) print v * 1024;
        else if (u ~ /KiB|KB/) print v / 1024;
        else if (u ~ /B$/ && u !~ /KB|MB|GB|KiB|MiB|GiB/) print v / (1024*1024);
        else print v;
    }' | awk '{printf "%.2f", $1}')

    RAM_MAX=$(awk "BEGIN {if ($RAM_USED > $RAM_MAX) print $RAM_USED; else print $RAM_MAX}")

    # Leer telemetría directa generada por Go
    UCA_ACTIVE=0
    NET_TX="0.0000"
    NET_RX="0.0000"

    if [ -f "uca_stats.txt" ]; then
        STATS_INTERNAL=$(cat uca_stats.txt 2>/dev/null)
        UCA_READ=$(echo "$STATS_INTERNAL" | cut -d'|' -f1)
        TX_READ=$(echo "$STATS_INTERNAL" | cut -d'|' -f2)
        RX_READ=$(echo "$STATS_INTERNAL" | cut -d'|' -f3)

        [ -n "$UCA_READ" ] && UCA_ACTIVE="$UCA_READ"
        [ -n "$TX_READ" ] && NET_TX="$TX_READ"
        [ -n "$RX_READ" ] && NET_RX="$RX_READ"
    fi

    NET_TOT=$(awk "BEGIN {printf \"%.4f\", $NET_RX + $NET_TX}")

    if [ "$UCA_ACTIVE" -gt "$UCA_MAX" ]; then
        UCA_MAX=$UCA_ACTIVE
    fi

    printf "%-19s | %-7s | %-8s | %-8s | %-11s | %-11s | %-10s | %-7s\n" \
        "$TS" "${ELAPSED}s" "${RAM_USED}" "${RAM_MAX}" "${NET_RX}" "${NET_TX}" "${UCA_ACTIVE}" "${UCA_MAX}"

    echo "${TS},${ELAPSED},${ARCH},${MEM_LIMIT_NUM},${RAM_USED},${RAM_MAX},${NET_RX},${NET_TX},${NET_TOT},${UCA_ACTIVE},${UCA_MAX}" >> "$CSV_FILE"

    sleep 0.5
done

echo "=============================================================================================================="
echo " Registrado con éxito en: $CSV_FILE"
echo "=============================================================================================================="
#!/usr/bin/env bash
# ============================================================================
# SCRIPT DE INSTRUMENTACIÓN Y DEMO EN VIVO (C vs Go en Entornos IoT)
# Asignatura: Redes de Datos - 4to Año LSI - FaCENA UNNE
# ============================================================================

set -e

GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}=================================================================${NC}"
echo -e "${CYAN}  BANCO DE PRUEBAS EMPÍRICO: C (Mirai Ref) vs Go (BotenaGo Ref)  ${NC}"
echo -e "${CYAN}=================================================================${NC}"

# 1. Compilación de todas las variantes (Tabla 3 / Fig. 7)
compile_all() {
    echo -e "\n${YELLOW}[1/3] Compilando binarios de prueba y servidor mock...${NC}"

    # Servidor Mock TCP
    gcc -O2 -pthread servidor_mock.c -o servidor_mock

    # Cliente C (Nativo y Reducido)
    gcc -O2 -pthread cliente.c -o cliente_c
    cp cliente_c cliente_c_stripped
    strip cliente_c_stripped

    # También generamos versión estática pura por si se corre en contenedor mínimo
    gcc -static -O2 -pthread cliente.c -o cliente_c_static 2>/dev/null || true

    # Cliente Go (Nativo y Reducido con -ldflags="-s -w")
    CGO_ENABLED=0 go build -o cliente_go cliente.go
    CGO_ENABLED=0 go build -ldflags="-s -w" -o cliente_go_stripped cliente.go

    echo -e "${GREEN}[+] Compilación exitosa.${NC}\n"
    echo -e "${CYAN}--- TABLA 3: Comparativa de Tamaño de Binarios (ls -lh) ---${NC}"
    ls -lh cliente_c cliente_c_stripped cliente_go cliente_go_stripped
    echo -e "${CYAN}-----------------------------------------------------------${NC}"
}

# 2. Configurar regla iptables para simular SYN_SENT reales hacia 192.0.2.1:9999
setup_network_simulation() {
    echo -e "\n${YELLOW}[2/3] Configurando entorno de red local...${NC}"
    # Aumentar rango de puertos efímeros y descriptores para alta concurrencia
    ulimit -n 65535 2>/dev/null || true

    if command -v iptables >/dev/null 2>&1 && [ "$EUID" -eq 0 ]; then
        if ! iptables -C OUTPUT -d 192.0.2.1 -p tcp --dport 9999 -j DROP 2>/dev/null; then
            iptables -A OUTPUT -d 192.0.2.1 -p tcp --dport 9999 -j DROP
            echo -e "${GREEN}[+] Regla iptables aplicada: paquetes a 192.0.2.1:9999 en DROP (fuerza estado SYN_SENT).${NC}"
        fi
    else
        echo -e "${YELLOW}[i] Nota: Ejecuta con sudo si deseas aplicar DROP en iptables para 192.0.2.1:9999.${NC}"
    fi
}

# 3.Ejecución con límite de memoria en Docker (Fig. 5, 8, 9, 10)
run_docker_test() {
    local lang=$1
    local mem=$2
    local bin_name="cliente_c_stripped"
    if [ "$lang" == "go" ]; then
        bin_name="cliente_go_stripped"
    fi

    echo -e "\n${CYAN}[DOCKER TEST] Ejecutando ${bin_name} con límite estricto de RAM = ${mem} (sin swap)${NC}"
    echo -e "${YELLOW}Comando: docker run --rm -it --network=host --memory=${mem} --memory-swap=${mem} -v \"\$(pwd):/app\" ubuntu:22.04 /app/${bin_name}${NC}\n"

    set +e
    docker run --rm -it \
        --name "iot_test_${lang}_${mem}" \
        --network=host \
        --memory="${mem}" \
        --memory-swap="${mem}" \
        -v "$(pwd):/app" \
        ubuntu:22.04 "/app/${bin_name}"
    local exit_code=$?
    set -e

    if [ $exit_code -eq 137 ]; then
        echo -e "\n${RED}[!] EVENTO OOM DETECTADO (Exit Code 137 - SIGKILL por el Kernel Linux)${NC}"
        echo -e "${RED}    El dispositivo IoT (${mem} RAM) agotó su memoria física ejecutando ${bin_name}.${NC}"
        echo -e "${YELLOW}    Puedes verificar el registro del kernel ejecutando: sudo dmesg -T | tail -n 15${NC}"
    else
        echo -e "\n${GREEN}[+] Ejecución finalizada con código ${exit_code}.${NC}"
    fi
}

# Menú interactivo para la presentación en vivo
if [ "$1" == "compile" ]; then
    compile_all
    exit 0
fi

compile_all
setup_network_simulation

echo -e "\n${CYAN}¿Cómo deseas ejecutar la demostración?${NC}"
echo "  1) Iniciar Servidor Mock TCP en puerto 9090 (ejecutar en otra terminal antes de las pruebas)"
echo "  2) Correr Cliente C directo en esta VM (ideal si la VM ya tiene la RAM limitada)"
echo "  3) Correr Cliente Go directo en esta VM (ideal si la VM ya tiene la RAM limitada)"
echo "  4) Emular Cámara IP Gama Baja (32 MB RAM) con Docker -> Cliente C"
echo "  5) Emular Cámara IP Gama Baja (32 MB RAM) con Docker -> Cliente Go (Provoca OOM)"
echo "  6) Emular Dispositivo Gama Media (64 MB RAM) con Docker -> Cliente Go (Estable)"
echo "  7) Emular DVR / Router (128 MB RAM) con Docker -> Cliente Go (Estable)"
echo "  0) Salir"
read -rp "Selecciona una opción [0-7]: " opt

case $opt in
    1) ./servidor_mock ;;
    2) ./cliente_c_stripped ;;
    3) ./cliente_go_stripped ;;
    4) run_docker_test "c" "32m" ;;
    5) run_docker_test "go" "32m" ;;
    6) run_docker_test "go" "64m" ;;
    7) run_docker_test "go" "128m" ;;
    *) echo "Saliendo..." ;;
esac


/**
 * ============================================================================
 * BENCHMARK EMPÍRICO DE C (CPU + HEAP MANUAL + SOCKETS POSIX NON-BLOCKING)
 * Caso de referencia arquitectónico: Agente IoT en C (estilo Mirai)
 * ============================================================================
 * Propósito académico:
 *   Simular de forma benigna y controlada la huella de recursos (RAM RSS,
 *   ciclos de CPU, alloc/free manual y descriptores de sockets TCP en estado
 *   ESTABLISHED y SYN_SENT) de un agente escrito en C puro sobre hardware IoT
 *   restringido (32 MB, 64 MB, 128 MB de RAM).
 *
 * Compilación:
 *   Nativo:    gcc -O2 -pthread cliente.c -o cliente_c
 *   Estático:  gcc -static -O2 -pthread cliente.c -o cliente_c_static
 *   Reducido:  strip cliente_c -o cliente_c_stripped
 * ============================================================================
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// ============================================================================
// CONFIGURACIÓN DEL BENCHMARK (Homóloga a cliente.go)
// ============================================================================
typedef struct {
    int base_port;          // Puerto del servidor mock local (ej. 9090)
    int num_workers;        // Hilos de trabajo POSIX (ajustado a núcleos / concurrencia)
    int simulated_slots;    // Cantidad de tareas/conexiones concurrentes (ej. 4000)
    int heap_target_mb;     // RAM estática retenida en el Heap (MB)
    int cpu_loop_work;      // Trabajo de CPU por ciclo (iteraciones SHA-256)
    int dial_timeout_ms;    // Timeout de sockets TCP (ms)
    int worker_delay_us;    // Pausa entre ciclos de cada worker (microsegundos)
    int unreachable_ratio;  // % de conexiones a IP inalcanzable para SYN_SENT (0-100)
    int idle_seconds;       // Segundos iniciales en reposo antes de iniciar carga
    char target_host[64];   // IP del servidor local (por defecto 127.0.0.1)
} Config;

// ============================================================================
// TELEMETRÍA GLOBAL ATÓMICA
// ============================================================================
typedef struct {
    volatile long active_slots;
    volatile long active_sockets;
    volatile long successful_dials;
    volatile long failed_dials;
    volatile long syn_sent_dials;
    volatile long bytes_transferred;
    volatile long manual_frees;
} Telemetry;

static Telemetry metrics = {0};
static volatile sig_atomic_t keep_running = 1;
static uint8_t **heap_memory_hold = NULL;
static Config cfg;

// ============================================================================
// IMPLEMENTACIÓN COMPACTA DE SHA-256 (Cero dependencias externas)
// Permite compilar sin requerir libssl-dev y realizar el mismo trabajo de CPU
// ============================================================================
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x) (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x) (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static void sha256_compute(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint8_t block[64];
    size_t i = 0;

    while (i + 64 <= len) {
        uint32_t w[64];
        for (int t = 0; t < 16; t++) {
            w[t] = ((uint32_t)data[i + t * 4] << 24) |
                   ((uint32_t)data[i + t * 4 + 1] << 16) |
                   ((uint32_t)data[i + t * 4 + 2] << 8) |
                   ((uint32_t)data[i + t * 4 + 3]);
        }
        for (int t = 16; t < 64; t++) {
            w[t] = SIG1(w[t - 2]) + w[t - 7] + SIG0(w[t - 15]) + w[t - 16];
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int t = 0; t < 64; t++) {
            uint32_t t1 = h + EP1(e) + CH(e, f, g) + K256[t] + w[t];
            uint32_t t2 = EP0(a) + MAJ(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
        i += 64;
    }

    // Bloque final simplificado para carga sintética de CPU
    memset(block, 0, sizeof(block));
    size_t rem = len - i;
    if (rem > 0) memcpy(block, data + i, rem);
    block[rem] = 0x80;
    uint32_t w[64];
    for (int t = 0; t < 16; t++) {
        w[t] = ((uint32_t)block[t * 4] << 24) |
               ((uint32_t)block[t * 4 + 1] << 16) |
               ((uint32_t)block[t * 4 + 2] << 8) |
               ((uint32_t)block[t * 4 + 3]);
    }
    for (int t = 16; t < 64; t++) {
        w[t] = SIG1(w[t - 2]) + w[t - 7] + SIG0(w[t - 15]) + w[t - 16];
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int t = 0; t < 64; t++) {
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + K256[t] + w[t];
        uint32_t t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;

    for (int t = 0; t < 8; t++) {
        out[t * 4]     = (state[t] >> 24) & 0xff;
        out[t * 4 + 1] = (state[t] >> 16) & 0xff;
        out[t * 4 + 2] = (state[t] >> 8) & 0xff;
        out[t * 4 + 3] = state[t] & 0xff;
    }
}

// ============================================================================
// LECTURA DE MEMORIA FÍSICA REAL (RSS) DESDE EL KERNEL LINUX (/proc/self/statm)
// ============================================================================
static double get_process_rss_mb(void) {
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0.0;
    long total_pages = 0, rss_pages = 0;
    if (fscanf(fp, "%ld %ld", &total_pages, &rss_pages) != 2) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);
    long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
    return (double)(rss_pages * page_size_kb) / 1024.0;
}

// ============================================================================
// MANEJO DE SEÑALES
// ============================================================================
static void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
}

// ============================================================================
// RETENCIÓN DE MEMORIA ESTÁTICA EN HEAP (Homóloga a allocateStaticHeap en Go)
// ============================================================================
static void allocate_static_heap(int target_mb) {
    if (target_mb <= 0) return;
    printf("[+] Reservando %d MB de RAM fija en el Heap (malloc)...\n", target_mb);
    heap_memory_hold = (uint8_t **)malloc(sizeof(uint8_t *) * target_mb);
    if (!heap_memory_hold) {
        perror("malloc heap_memory_hold");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < target_mb; i++) {
        size_t chunk_size = 1024 * 1024;
        heap_memory_hold[i] = (uint8_t *)malloc(chunk_size);
        if (!heap_memory_hold[i]) {
            perror("malloc chunk");
            exit(EXIT_FAILURE);
        }
        // Tocar cada página para forzar asignación física en RAM (RSS real)
        for (size_t j = 0; j < chunk_size; j++) {
            heap_memory_hold[i][j] = (uint8_t)((i + j) % 256);
        }
    }
}

static void free_static_heap(int target_mb) {
    if (!heap_memory_hold || target_mb <= 0) return;
    for (int i = 0; i < target_mb; i++) {
        free(heap_memory_hold[i]);
    }
    free(heap_memory_hold);
}

// ============================================================================
// CONEXIÓN TCP NO BLOQUEANTE CON TIMEOUT (Estilo POSIX / Mirai)
// ============================================================================
static int dial_tcp_with_timeout(const char *ip, int port, int timeout_ms, const uint8_t *payload, size_t payload_len) {
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int res = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) {
        close(sockfd);
        return -1;
    }

    if (res != 0) {
        int epfd = epoll_create1(0);
        if (epfd < 0) {
            close(sockfd);
            return -1;
        }
        struct epoll_event ev, events[1];
        ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
        ev.data.fd = sockfd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

        int nfds = epoll_wait(epfd, events, 1, timeout_ms);
        close(epfd);

        if (nfds <= 0) {
            // Timeout alcanzado (en IP 192.0.2.1 el socket permaneció en SYN_SENT)
            close(sockfd);
            return -1;
        }

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            close(sockfd);
            return -1;
        }
    }

    // Conexión exitosa: contabilizar socket activo y transmitir payload
    __sync_add_and_fetch(&metrics.active_sockets, 1);

    ssize_t sent = send(sockfd, payload, payload_len, MSG_NOSIGNAL);
    if (sent > 0) {
        __sync_add_and_fetch(&metrics.bytes_transferred, sent);
        uint8_t buf[16];
        // Lectura rápida no bloqueante de respuesta si está disponible
        ssize_t r = recv(sockfd, buf, sizeof(buf), MSG_DONTWAIT);
        (void)r;
    }

    close(sockfd);
    __sync_sub_and_fetch(&metrics.active_sockets, 1);
    return 0;
}

// ============================================================================
// WORKER POSIX EN C (CPU + MALLOC/FREE INMEDIATO + ROTACIÓN TCP/SYN_SENT)
// ============================================================================
typedef struct {
    int worker_id;
    int slots_assigned;
} WorkerArgs;

static void *worker_node_c(void *arg) {
    WorkerArgs *wargs = (WorkerArgs *)arg;
    int id = wargs->worker_id;
    int slots = wargs->slots_assigned;
    free(wargs);

    __sync_add_and_fetch(&metrics.active_slots, slots);

    unsigned int seed = (unsigned int)(time(NULL) ^ (id * 7919) ^ pthread_self());
    char payload_str[128];
    int payload_len = snprintf(payload_str, sizeof(payload_str),
                               "synthetic_payload_signature_data_block_worker_%d", id);
    uint8_t digest[32];
    int iter = 0;

    while (keep_running) {
        // A. ASIGNACIÓN Y LIBERACIÓN DINÁMICA MANUAL (Contraste con el GC de Go)
        // En C, malloc() y free() devuelven el bloque inmediatamente al allocator
        // para ser reutilizado en el próximo ciclo sin acumular basura en el Heap.
        size_t buffer_size = 4096 + (rand_r(&seed) % 4096);
        uint8_t *dynamic_buffer = (uint8_t *)malloc(buffer_size);
        if (dynamic_buffer) {
            for (size_t b = 0; b < buffer_size; b++) {
                dynamic_buffer[b] = (uint8_t)((iter + b) % 256);
            }
            sha256_compute(dynamic_buffer, buffer_size, digest);
            free(dynamic_buffer);
            __sync_add_and_fetch(&metrics.manual_frees, 1);
        }

        // B. CÓMPUTO INTENSIVO DE CPU (SHA-256 idéntico al cliente Go)
        for (int k = 0; k < cfg.cpu_loop_work; k++) {
            sha256_compute((const uint8_t *)payload_str, (size_t)payload_len, digest);
        }
        iter++;

        // C. SELECCIÓN DE OBJETIVO CON ROTACIÓN DE IP (Loopback vs TEST-NET SYN_SENT)
        int is_unreachable = (rand_r(&seed) % 100) < cfg.unreachable_ratio;
        const char *target_ip = cfg.target_host;
        int target_port = cfg.base_port;

        if (is_unreachable) {
            target_ip = "192.0.2.1";
            target_port = 9999;
            __sync_add_and_fetch(&metrics.syn_sent_dials, 1);
        }

        // D. INTENTO DE CONEXIÓN TCP NO BLOQUEANTE
        if (dial_tcp_with_timeout(target_ip, target_port, cfg.dial_timeout_ms,
                                  (const uint8_t *)payload_str, (size_t)payload_len) == 0) {
            __sync_add_and_fetch(&metrics.successful_dials, 1);
        } else {
            __sync_add_and_fetch(&metrics.failed_dials, 1);
        }

        if (cfg.worker_delay_us > 0) {
            usleep((useconds_t)cfg.worker_delay_us);
        }
    }

    __sync_sub_and_fetch(&metrics.active_slots, slots);
    return NULL;
}

// ============================================================================
// MAIN & DASHBOARD DE MONITOREO
// ============================================================================
int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // Configuración por defecto homóloga a cliente.go
    cfg.base_port = 9090;
    cfg.num_workers = 64;          // Hilos POSIX ligeros multiplexando los 4000 slots
    cfg.simulated_slots = 4000;    // Nivel de concurrencia lógica equivalente
    cfg.heap_target_mb = 2;        // 2 MB de RAM base retenida en Heap
    cfg.cpu_loop_work = 250;       // 250 iteraciones SHA-256 por ciclo
    cfg.dial_timeout_ms = 150;     // 150 ms de timeout por conexión
    cfg.worker_delay_us = 1000;    // 1 ms de espera entre ciclos
    cfg.unreachable_ratio = 30;    // 30% de conexiones a TEST-NET (SYN_SENT)
    cfg.idle_seconds = 5;          // 5 segundos iniciales en reposo para medir Tabla 2
    strncpy(cfg.target_host, "127.0.0.1", sizeof(cfg.target_host) - 1);

    // Argumentos opcionales por línea de comandos: ./cliente_c [idle_sec] [heap_mb] [workers]
    if (argc > 1) cfg.idle_seconds = atoi(argv[1]);
    if (argc > 2) cfg.heap_target_mb = atoi(argv[2]);
    if (argc > 3) cfg.num_workers = atoi(argv[3]);

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);

    printf("=================================================================\n");
    printf(" BENCHMARK EMPIRICO DE C (CPU + HEAP MANUAL + POSIX SOCKETS)     \n");
    printf(" Cores CPU: %ld | Slots/Workers: %d/%d | RAM Base: %d MB | SYN_SENT: %d%%\n",
           num_cores, cfg.simulated_slots, cfg.num_workers, cfg.heap_target_mb, cfg.unreachable_ratio);
    printf("=================================================================\n");

    // FASE 1: Medición en Reposo (Idle) para Tabla 2 del informe
    if (cfg.idle_seconds > 0) {
        printf("[*] FASE 1 (REPOSO / IDLE): Midiendo huella base del binario en C durante %d s...\n",
               cfg.idle_seconds);
        for (int s = cfg.idle_seconds; s > 0 && keep_running; s--) {
            printf("\r[C IDLE]    Tiempo restante: %d s | RAM Física (RSS): %.2f MB   ",
                   s, get_process_rss_mb());
            fflush(stdout);
            sleep(1);
        }
        printf("\n");
    }

    if (!keep_running) return 0;

    // FASE 2: Reserva de Heap estático y despliegue de carga concurrente
    allocate_static_heap(cfg.heap_target_mb);

    printf("[+] Desplegando %d slots sobre %d workers POSIX con stack reducido...\n",
           cfg.simulated_slots, cfg.num_workers);

    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * cfg.num_workers);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Ajustar el stack por hilo a 128 KB (en lugar de 8 MB por defecto en Linux)
    // típico de programación de sistemas embebidos en C.
    pthread_attr_setstacksize(&attr, 128 * 1024);

    int slots_per_worker = cfg.simulated_slots / cfg.num_workers;
    for (int i = 0; i < cfg.num_workers; i++) {
        WorkerArgs *wargs = (WorkerArgs *)malloc(sizeof(WorkerArgs));
        wargs->worker_id = i;
        wargs->slots_assigned = (i == cfg.num_workers - 1)
                                    ? (cfg.simulated_slots - slots_per_worker * i)
                                    : slots_per_worker;
        if (pthread_create(&threads[i], &attr, worker_node_c, wargs) != 0) {
            perror("pthread_create");
            free(wargs);
        }
    }
    pthread_attr_destroy(&attr);

    // Bucle de telemetría en vivo (cada 250 ms, idéntico al de Go)
    while (keep_running) {
        usleep(250 * 1000);

        long dials_ok  = __sync_lock_test_and_set(&metrics.successful_dials, 0);
        long dials_err = __sync_lock_test_and_set(&metrics.failed_dials, 0);
        long dials_syn = __sync_lock_test_and_set(&metrics.syn_sent_dials, 0);
        long bytes_tx  = __sync_lock_test_and_set(&metrics.bytes_transferred, 0);
        long frees_cnt = __sync_lock_test_and_set(&metrics.manual_frees, 0);

        double rss_mb = get_process_rss_mb();

        printf("\r[C METRICS]  Slots: %ld | RAM (RSS): %.2f MB | Frees/s: %ld | Dials/s: %ld | Timeouts/s: %ld | Traffic: %.1f KB/s   ",
               metrics.active_slots,
               rss_mb,
               frees_cnt * 4,
               dials_ok * 4,
               (dials_err + dials_syn) * 4,
               ((double)bytes_tx / 1024.0) * 4.0);
        fflush(stdout);
    }

    printf("\n[!] Finalizando test de C y liberando memoria manualmente...\n");
    for (int i = 0; i < cfg.num_workers; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);
    free_static_heap(cfg.heap_target_mb);
    printf("[+] Prueba de C finalizada limpiamente.\n");
    return 0;
}


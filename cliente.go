package main

import (
	"context"
	"crypto/sha256"
	"fmt"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

// ============================================================================
// CONFIGURACIÓN DEL BENCHMARK (Homóloga a cliente.c)
// ============================================================================
type Config struct {
	BasePort         string        // Puerto del listener local
	NumGoroutines    int           // Cantidad de Goroutines concurrentes
	HeapTargetMB     int           // RAM estática retenida en el Heap (MB)
	CPULoopWork      int           // Trabajo de CPU por ciclo (SHA-256)
	DialTimeout      time.Duration // Timeout de sockets TCP
	WorkerDelay      time.Duration // Pausa entre ciclos de cada worker
	UnreachableRatio int           // Porcentaje de conexiones dirigidas a IP con SYN_SENT (0-100)
	IdleSeconds      int           // Segundos iniciales en reposo antes de lanzar la carga
}

// ============================================================================
// TELEMETRÍA GLOBAL
// ============================================================================
type Telemetry struct {
	ActiveGoroutines int64
	ActiveSockets    int64
	SuccessfulDials  int64
	FailedDials      int64
	SynSentDials     int64
	BytesTransferred int64
	GCRuns           uint32
}

var (
	metrics        Telemetry
	heapMemoryHold [][]byte
)

// ============================================================================
// LECTURA DE MEMORIA FÍSICA REAL (RSS) DEL KERNEL LINUX (/proc/self/statm)
// ============================================================================
func getProcessRSSMB() float64 {
	data, err := os.ReadFile("/proc/self/statm")
	if err != nil {
		return 0.0
	}
	fields := strings.Fields(string(data))
	if len(fields) < 2 {
		return 0.0
	}
	rssPages, err := strconv.ParseInt(fields[1], 10, 64)
	if err != nil {
		return 0.0
	}
	pageSize := int64(os.Getpagesize())
	return float64(rssPages*pageSize) / (1024.0 * 1024.0)
}

// ============================================================================
// 1. WORKER COMPLETO (CPU + RAM ESTÁTICA/DINÁMICA + ROTACIÓN TCP/SYN_SENT)
// ============================================================================
func workerNode(ctx context.Context, wg *sync.WaitGroup, id int, cfg Config) {
	defer wg.Done()

	atomic.AddInt64(&metrics.ActiveGoroutines, 1)
	defer atomic.AddInt64(&metrics.ActiveGoroutines, -1)

	dialer := &net.Dialer{
		Timeout: cfg.DialTimeout,
	}

	r := rand.New(rand.NewSource(time.Now().UnixNano() + int64(id)))
	payloadMock := []byte(fmt.Sprintf("synthetic_payload_signature_data_block_worker_%d", id))
	iter := 0

	for {
		select {
		case <-ctx.Done():
			return
		default:
			// A. ESTRÉS DINÁMICO AL GARBAGE COLLECTOR (Short-lived Allocations)
			// Tamaño dinámico para evitar Escape Analysis y forzar asignación en Heap
			bufferSize := 4096 + r.Intn(4096)
			dynamicBuffer := make([]byte, bufferSize)
			for b := range dynamicBuffer {
				dynamicBuffer[b] = byte((iter + b) % 256)
			}
			// Forzar paso por SHA-256 para asegurar que escape a Heap
			_ = sha256.Sum256(dynamicBuffer)

			// B. CÓMPUTO INTENSIVO DE CPU
			for k := 0; k < cfg.CPULoopWork; k++ {
				_ = sha256.Sum256(payloadMock)
			}
			iter++

			// C. SELECCIÓN DE OBJETIVO CON ROTACIÓN DE IP
			var targetIP string
			isUnreachable := r.Intn(100) < cfg.UnreachableRatio

			if isUnreachable {
				// Apuntar a IP de documentación/TEST-NET (192.0.2.1) para forzar SYN_SENT real
				targetIP = "192.0.2.1:9999"
				atomic.AddInt64(&metrics.SynSentDials, 1)
			} else {
				// Rotación limpia dentro de la interfaz loopback (127.0.0.1)
				targetIP = "127.0.0.1:" + cfg.BasePort
			}

			// D. INTENTO DE CONEXIÓN TCP Y MANEJO DE SOCKETS
			conn, err := dialer.DialContext(ctx, "tcp", targetIP)
			if err != nil {
				atomic.AddInt64(&metrics.FailedDials, 1)
			} else {
				atomic.AddInt64(&metrics.SuccessfulDials, 1)
				atomic.AddInt64(&metrics.ActiveSockets, 1)

				_ = conn.SetDeadline(time.Now().Add(200 * time.Millisecond))
				n, err := conn.Write(payloadMock)
				if err == nil {
					atomic.AddInt64(&metrics.BytesTransferred, int64(n))
					buf := make([]byte, 16)
					_, _ = conn.Read(buf)
				}

				conn.Close()
				atomic.AddInt64(&metrics.ActiveSockets, -1)
			}

			if cfg.WorkerDelay > 0 {
				time.Sleep(cfg.WorkerDelay)
			}
		}
	}
}

// ============================================================================
// 2. RETENCIÓN DE MEMORIA ESTÁTICA EN HEAP
// ============================================================================
func allocateStaticHeap(targetMB int) {
	if targetMB <= 0 {
		return
	}
	fmt.Printf("[+] Reservando %d MB de RAM fija en el Heap de Go...\n", targetMB)
	for i := 0; i < targetMB; i++ {
		chunk := make([]byte, 1024*1024)
		for j := range chunk {
			chunk[j] = byte((i + j) % 256)
		}
		heapMemoryHold = append(heapMemoryHold, chunk)
	}
}

// ============================================================================
// MAIN & MONITORING DASHBOARD
// ============================================================================
func main() {
	cfg := Config{
		BasePort:         "9090",
		NumGoroutines:    4000,                  // Nivel de Goroutines concurrentes
		HeapTargetMB:     2,                     // 2 MB base retenidos en Heap
		CPULoopWork:      250,                   // Carga de hashing CPU por worker
		DialTimeout:      150 * time.Millisecond,
		WorkerDelay:      1 * time.Millisecond,
		UnreachableRatio: 30,                    // 30% de las conexiones forzadas a SYN_SENT/Timeout
		IdleSeconds:      5,                     // 5 segundos iniciales en reposo para medir Tabla 2
	}

	// Argumentos opcionales por CLI: ./cliente_go [idle_sec] [heap_mb] [goroutines]
	if len(os.Args) > 1 {
		if v, err := strconv.Atoi(os.Args[1]); err == nil {
			cfg.IdleSeconds = v
		}
	}
	if len(os.Args) > 2 {
		if v, err := strconv.Atoi(os.Args[2]); err == nil {
			cfg.HeapTargetMB = v
		}
	}
	if len(os.Args) > 3 {
		if v, err := strconv.Atoi(os.Args[3]); err == nil {
			cfg.NumGoroutines = v
		}
	}

	fmt.Println("=================================================================")
	fmt.Println(" BENCHMARK EMPÍRICO DE GO (CPU + HEAP + GC + TCP SYN_SENT)       ")
	fmt.Printf(" Cores CPU: %d | Goroutines: %d | RAM Base: %d MB | SYN_SENT: %d%%\n",
		runtime.NumCPU(), cfg.NumGoroutines, cfg.HeapTargetMB, cfg.UnreachableRatio)
	fmt.Println("=================================================================")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		fmt.Println("\n[!] Finalizando test de Go...")
		cancel()
	}()

	// FASE 1: Medición en Reposo (Idle) para Tabla 2 del informe
	if cfg.IdleSeconds > 0 {
		fmt.Printf("[*] FASE 1 (REPOSO / IDLE): Midiendo huella base del Runtime de Go durante %d s...\n", cfg.IdleSeconds)
		for s := cfg.IdleSeconds; s > 0; s-- {
			select {
			case <-ctx.Done():
				return
			default:
				var m runtime.MemStats
				runtime.ReadMemStats(&m)
				fmt.Printf("\r[GO IDLE]    Tiempo restante: %d s | Heap Alloc: %.2f MB | Runtime Sys: %.2f MB | RAM Física (RSS): %.2f MB   ",
					s, float64(m.Alloc)/1024/1024, float64(m.Sys)/1024/1024, getProcessRSSMB())
				time.Sleep(1 * time.Second)
			}
		}
		fmt.Println()
	}

	// FASE 2: Reserva de Heap estático y despliegue de Goroutines
	allocateStaticHeap(cfg.HeapTargetMB)

	var wg sync.WaitGroup
	fmt.Printf("[+] Desplegando %d Goroutines sobre el scheduler m:n...\n", cfg.NumGoroutines)
	for i := 0; i < cfg.NumGoroutines; i++ {
		wg.Add(1)
		go workerNode(ctx, &wg, i, cfg)
	}

	// Telemetría en vivo cada 250 ms
	go func() {
		ticker := time.NewTicker(250 * time.Millisecond)
		defer ticker.Stop()

		var lastNumGC uint32

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				var m runtime.MemStats
				runtime.ReadMemStats(&m)

				dialsOK := atomic.SwapInt64(&metrics.SuccessfulDials, 0)
				dialsErr := atomic.SwapInt64(&metrics.FailedDials, 0)
				dialsSyn := atomic.SwapInt64(&metrics.SynSentDials, 0)
				bytesSent := atomic.SwapInt64(&metrics.BytesTransferred, 0)

				gcRuns := m.NumGC - lastNumGC
				lastNumGC = m.NumGC

				rssMB := getProcessRSSMB()

				fmt.Printf("\r[GO METRICS] Rutinas: %d | Heap: %.1f MB (RSS: %.1f MB) | GC/s: %d | Dials/s: %d | Timeouts/s: %d | Traffic: %.1f KB/s   ",
					atomic.LoadInt64(&metrics.ActiveGoroutines),
					float64(m.Alloc)/1024/1024,
					rssMB,
					gcRuns*4,
					dialsOK*4,
					(dialsErr+dialsSyn)*4,
					(float64(bytesSent)/1024)*4,
				)
			}
		}
	}()

	wg.Wait()
	fmt.Println("\n[+] Prueba de Go finalizada.")
}


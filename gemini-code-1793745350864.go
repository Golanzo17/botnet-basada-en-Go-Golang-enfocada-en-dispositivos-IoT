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
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

// ============================================================================
// CONFIGURACIÓN DEL BENCHMARK
// ============================================================================
type Config struct {
	BasePort         string        // Puerto del listener local
	NumGoroutines    int           // Cantidad de Goroutines concurrentes
	HeapTargetMB     int           // RAM estática retenida en el Heap (MB)
	CPULoopWork      int           // Trabajo de CPU por ciclo (SHA-256)
	DialTimeout      time.Duration // Timeout de sockets TCP
	WorkerDelay      time.Duration // Pausa entre ciclos de cada worker
	UnreachableRatio int           // Porcentaje de conexiones dirigidas a IPs sin puerto abierto (0-100)
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
			// CORRECCIÓN 4: Tamaño dinámico para evitar Escape Analysis y forzar asignación en Heap
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
				// CORRECCIÓN 2: Apuntar a IP de documentación/TEST-NET (192.0.2.1) con DROP en iptables para forzar SYN_SENT real
				targetIP = "192.0.2.1:9999"
				atomic.AddInt64(&metrics.SynSentDials, 1)
			} else {
				// CORRECCIÓN 2: Rotación limpia dentro de la interfaz loopback (127.0.0.1)
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
	fmt.Printf("[+] Reservando %d MB de RAM fija en el Heap...\n", targetMB)
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
		NumGoroutines:    4000,                // Nivel de Goroutines concurrentes
		HeapTargetMB:     2,                   // CORRECCIÓN 3: Valor realista (2 MB) para no falsear contenedores de 32MB/64MB
		CPULoopWork:      250,                 // Carga de hashing CPU por worker
		DialTimeout:      150 * time.Millisecond,
		WorkerDelay:      1 * time.Millisecond,
		UnreachableRatio: 30,                  // 30% de las conexiones forzadas a SYN_SENT/Timeout
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

	// CORRECCIÓN 1: Se eliminó el startMockTCPServer incrustado.
	// El servidor Mock debe correr en un binario o contenedor separado para evitar sesgo de medición.

	allocateStaticHeap(cfg.HeapTargetMB)

	var wg sync.WaitGroup
	fmt.Printf("[+] Desplegando %d Goroutines...\n", cfg.NumGoroutines)
	for i := 0; i < cfg.NumGoroutines; i++ {
		wg.Add(1)
		go workerNode(ctx, &wg, i, cfg)
	}

	// Telemetría en vivo
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

				fmt.Printf("\r[GO METRICS] Rutinas: %d | RAM: %.1f MB | GC Runs/s: %d | Dials/s: %d | Timeouts/s: %d | Traffic: %.1f KB/s",
					atomic.LoadInt64(&metrics.ActiveGoroutines),
					float64(m.Alloc)/1024/1024,
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
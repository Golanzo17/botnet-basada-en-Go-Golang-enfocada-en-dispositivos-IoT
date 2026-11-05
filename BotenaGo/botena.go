package main

import (
	"context"
	"crypto/sha256"
	"fmt"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"strconv"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

type Config struct {
	TargetHost       string
	BasePort         string
	NumGoroutines    int
	HeapTargetMB     int
	CPULoopWork      int
	DialTimeout      time.Duration
	WorkerDelay      time.Duration
	UnreachableRatio int
}

type Telemetry struct {
	ActiveGoroutines int64
	ActiveWorkers    int64
	ActiveSockets    int64
	SuccessfulDials  int64
	FailedDials      int64
	SynSentDials     int64
	BytesTransferred int64
	BytesReceived    int64
	GCRuns           uint32
}

var (
	metrics        Telemetry
	heapMemoryHold [][]byte
)

func envIntOrDefault(key string, def int) int {
	if val := os.Getenv(key); val != "" {
		if n, err := strconv.Atoi(val); err == nil && n >= 0 {
			return n
		}
	}
	return def
}

func envStrOrDefault(key, def string) string {
	if val := os.Getenv(key); val != "" {
		return val
	}
	return def
}

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
			atomic.AddInt64(&metrics.ActiveWorkers, 1)

			bufferSize := 4096 + r.Intn(4096)
			dynamicBuffer := make([]byte, bufferSize)
			for b := range dynamicBuffer {
				dynamicBuffer[b] = byte((iter + b) % 256)
			}
			_ = sha256.Sum256(dynamicBuffer)

			for k := 0; k < cfg.CPULoopWork; k++ {
				_ = sha256.Sum256(payloadMock)
			}
			iter++

			var targetIP string
			if r.Intn(100) < cfg.UnreachableRatio {
				targetIP = "192.0.2.1:9999"
				atomic.AddInt64(&metrics.SynSentDials, 1)
			} else {
				targetIP = cfg.TargetHost + ":" + cfg.BasePort
			}

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
					rBytes, rErr := conn.Read(buf)
					if rBytes > 0 && rErr == nil {
						atomic.AddInt64(&metrics.BytesReceived, int64(rBytes))
					}
				}

				conn.Close()
				atomic.AddInt64(&metrics.ActiveSockets, -1)
			}

			atomic.AddInt64(&metrics.ActiveWorkers, -1)

			if cfg.WorkerDelay > 0 {
				time.Sleep(cfg.WorkerDelay)
			}
		}
	}
}

func allocateStaticHeap(targetMB int) {
	if targetMB <= 0 {
		return
	}
	for i := 0; i < targetMB; i++ {
		chunk := make([]byte, 1024*1024)
		for j := range chunk {
			chunk[j] = byte((i + j) % 256)
		}
		heapMemoryHold = append(heapMemoryHold, chunk)
	}
}

func main() {
	cfg := Config{
		TargetHost:       envStrOrDefault("BENCHMARK_HOST", "127.0.0.1"),
		BasePort:         envStrOrDefault("BENCHMARK_PORT", "9090"),
		NumGoroutines:    envIntOrDefault("BENCHMARK_UCA", 64),
		HeapTargetMB:     envIntOrDefault("BENCHMARK_HEAP_MB", 2),
		CPULoopWork:      envIntOrDefault("BENCHMARK_CPU_WORK", 250),
		DialTimeout:      150 * time.Millisecond,
		WorkerDelay:      1 * time.Millisecond,
		UnreachableRatio: envIntOrDefault("BENCHMARK_UNREACHABLE_RATIO", 0),
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigChan
		_ = os.Remove("uca_stats.txt")
		cancel()
	}()

	allocateStaticHeap(cfg.HeapTargetMB)

	var wg sync.WaitGroup
	for i := 0; i < cfg.NumGoroutines; i++ {
		wg.Add(1)
		go workerNode(ctx, &wg, i, cfg)
	}

	// Telemetría exportada atómicamente cada 100ms
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				activeWorkers := atomic.LoadInt64(&metrics.ActiveWorkers)
				totalBytesTx := atomic.LoadInt64(&metrics.BytesTransferred)
				totalBytesRx := atomic.LoadInt64(&metrics.BytesReceived)
				netTxMB := float64(totalBytesTx) / (1024.0 * 1024.0)
				netRxMB := float64(totalBytesRx) / (1024.0 * 1024.0)

				telemetryStr := fmt.Sprintf("%d|%.4f|%.4f", activeWorkers, netTxMB, netRxMB)
				if err := os.WriteFile("uca_stats.txt.tmp", []byte(telemetryStr), 0644); err == nil {
					_ = os.Rename("uca_stats.txt.tmp", "uca_stats.txt")
				}
			}
		}
	}()

	wg.Wait()
	_ = os.Remove("uca_stats.txt")
}

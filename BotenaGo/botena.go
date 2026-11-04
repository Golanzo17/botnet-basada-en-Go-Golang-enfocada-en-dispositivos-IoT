package main

import (
	"context"
	"crypto/sha256"
	"fmt"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

type Config struct {
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
	GCRuns           uint32
}

var (
	metrics        Telemetry
	heapMemoryHold [][]byte
)

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
				targetIP = "127.0.0.1:" + cfg.BasePort
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
					_, _ = conn.Read(buf)
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
		BasePort:         "9090",
		NumGoroutines:    4000,
		HeapTargetMB:     2,
		CPULoopWork:      250,
		DialTimeout:      150 * time.Millisecond,
		WorkerDelay:      1 * time.Millisecond,
		UnreachableRatio: 30,
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

	// Telemetría exportada cada 100ms
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				activeWorkers := atomic.LoadInt64(&metrics.ActiveWorkers)
				totalBytes := atomic.LoadInt64(&metrics.BytesTransferred)
				netTxMB := float64(totalBytes) / (1024.0 * 1024.0)

				// Escribir UCA y Net TX directamente en el archivo compartido
				telemetryStr := fmt.Sprintf("%d|%.4f|0.0000", activeWorkers, netTxMB)
				_ = os.WriteFile("uca_stats.txt", []byte(telemetryStr), 0644)
			}
		}
	}()

	wg.Wait()
	_ = os.Remove("uca_stats.txt")
}

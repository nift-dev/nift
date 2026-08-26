package nift

import (
	"sync"
	"testing"
)

// CP17: the Go binding reclaims callback-output buffers once no render is in
// flight (render-active lifetime), bounding callback memory to peak concurrent
// render activity instead of the engine's whole lifetime. These tests assert
// the bound directly through the internal callbackSet.

func newLoaderEngine(t *testing.T) (*Engine, *callbackSet) {
	t.Helper()
	e := NewEngine()
	e.SetRoot("/")
	e.SetLoader(func(path string) HostResult {
		return HostResult{Status: HostFound, Value: "<p>PART</p>"}
	})
	v, _ := callbackRegistry.Load(e.id)
	return e, v.(*callbackSet)
}

func TestCallbackBufferReclaimedAfterEachRender(t *testing.T) {
	e, set := newLoaderEngine(t)
	defer e.Close()
	for i := 0; i < 200; i++ {
		res, err := e.Render(`@input("p.html")`, "<main>@content</main>", nil)
		if err != nil || !res.OK {
			t.Fatalf("render %d failed: err=%v ok=%v errmsg=%v", i, err, res.OK, res.Error)
		}
		set.mu.Lock()
		n := len(set.bufs)
		set.mu.Unlock()
		if n != 0 {
			t.Fatalf("iteration %d: %d callback buffers retained after the render completed (must be 0)", i, n)
		}
	}
}

func TestCallbackBufferReclaimedAfterConcurrentRenders(t *testing.T) {
	e, set := newLoaderEngine(t)
	defer e.Close()
	var wg sync.WaitGroup
	for i := 0; i < 16; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				res, err := e.Render(`@input("p.html")`, "<main>@content</main>", nil)
				if err != nil || !res.OK {
					t.Errorf("render failed: err=%v ok=%v errmsg=%v", err, res.OK, res.Error)
					return
				}
			}
		}()
	}
	wg.Wait()
	set.mu.Lock()
	defer set.mu.Unlock()
	if n := len(set.bufs); n != 0 {
		t.Fatalf("after all concurrent renders quiesced: %d callback buffers retained (must be 0)", n)
	}
}

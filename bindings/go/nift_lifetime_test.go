package nift

import (
	"sync"
	"testing"
	"time"
)

// CP17 lifetime tests: deterministic close-during-render (callback rendezvous)
// and the quiescent-reclamation admission race.

func TestCloseEngineDuringRenderIsDeferred(t *testing.T) {
	e := NewEngine()
	e.SetRoot("/")
	entered := make(chan struct{})
	release := make(chan struct{})
	var once sync.Once
	e.SetLoader(func(path string) HostResult {
		once.Do(func() { close(entered) })
		<-release
		return HostResult{Status: HostFound, Value: "<p>PART</p>"}
	})

	errCh := make(chan error, 1)
	done := make(chan Result, 1)
	go func() {
		r, err := e.Render(`@input("p.html")`, "<main>@content</main>", nil)
		errCh <- err
		done <- r
	}()

	<-entered // render provably in native execution
	e.Close() // must defer native destruction
	if _, err := e.Render("x", "y", nil); err == nil {
		t.Fatal("expected a closed engine to reject a new render")
	}
	close(release)
	if err := <-errCh; err != nil {
		t.Fatalf("render error: %v", err)
	}
	r := <-done
	if !r.OK {
		t.Fatalf("render failed: %v", r.Error)
	}
	// Close is idempotent.
	e.Close()
}

func TestCloseContextDuringRenderIsDeferred(t *testing.T) {
	e := NewEngine()
	e.SetRoot("/")
	c := NewContext()
	c.SetString("s", "ctx")
	entered := make(chan struct{})
	release := make(chan struct{})
	var once sync.Once
	e.SetLoader(func(path string) HostResult {
		once.Do(func() { close(entered) })
		<-release
		return HostResult{Status: HostFound, Value: "<b>$[s]</b>"}
	})

	errCh := make(chan error, 1)
	done := make(chan Result, 1)
	go func() {
		r, err := e.Render(`@input("p.html")`, "<main>@content</main>", c)
		errCh <- err
		done <- r
	}()

	<-entered // render provably in native execution using the context
	c.Close() // must defer native context destruction
	close(release)
	if err := <-errCh; err != nil {
		t.Fatalf("render error: %v", err)
	}
	r := <-done
	if !r.OK || r.Output != "<main><b>ctx</b></main>" {
		t.Fatalf("bad result: ok=%v output=%q err=%v", r.OK, r.Output, r.Error)
	}
	e.Close()
}

func TestBufferReclaimDoesNotRaceNewRenderAdmission(t *testing.T) {
	e, set := newLoaderEngine(t)
	defer e.Close()

	enteredReclaim := make(chan struct{})
	releaseReclaim := make(chan struct{})
	var hookOnce sync.Once
	reclaimTestHook = func() {
		// One-shot: only the FIRST reclamation (render A) blocks inside the
		// critical section; later reclamations (render B after release) are
		// no-ops so the test still completes.
		hookOnce.Do(func() {
			close(enteredReclaim)
			<-releaseReclaim
		})
	}
	defer func() { reclaimTestHook = nil }()

	// Render A finishes; its reclamation enters the hook while holding the
	// Engine lifecycle mutex (renderCount already zero).
	aErr := make(chan error, 1)
	go func() {
		_, err := e.Render(`@input("p.html")`, "<main>@content</main>", nil)
		aErr <- err
	}()
	<-enteredReclaim

	// Render B attempts admission: it must block until A's reclamation
	// completes, so no buffer free can interleave with B's callbacks.
	bErr := make(chan error, 1)
	bOK := make(chan bool, 1)
	go func() {
		r, err := e.Render(`@input("p.html")`, "<main>@content</main>", nil)
		bErr <- err
		bOK <- r.OK
	}()
	select {
	case err := <-bErr:
		t.Fatalf("render B was admitted while reclamation held the lifecycle mutex (err=%v)", err)
	case <-time.After(150 * time.Millisecond):
	}

	close(releaseReclaim)
	if err := <-aErr; err != nil {
		t.Fatalf("render A error: %v", err)
	}
	if err := <-bErr; err != nil {
		t.Fatalf("render B error: %v", err)
	}
	if ok := <-bOK; !ok {
		t.Fatal("render B failed")
	}

	set.mu.Lock()
	n := len(set.bufs)
	set.mu.Unlock()
	if n != 0 {
		t.Fatalf("%d callback buffers retained after quiescence (must be 0)", n)
	}
}

func TestNonRenderOperationRacesEngineClose(t *testing.T) {
	e := NewEngine()
	entered := make(chan struct{})
	release := make(chan struct{})
	nativeOpTestHook = func() {
		close(entered)
		<-release
	}
	defer func() { nativeOpTestHook = nil }()

	opDone := make(chan error, 1)
	go func() { opDone <- e.SetString("s", "x") }()
	<-entered // setter admitted, holding the lifecycle mutex before the native call

	closeDone := make(chan struct{})
	go func() { e.Close(); close(closeDone) }()
	select {
	case <-closeDone:
		t.Fatal("Close completed while an admitted non-render operation held the lifecycle mutex")
	case <-time.After(150 * time.Millisecond):
	}

	close(release)
	if err := <-opDone; err != nil {
		t.Fatalf("admitted setter failed: %v", err)
	}
	<-closeDone // Close proceeds only after the operation returns
	if err := e.SetString("s", "y"); err == nil {
		t.Fatal("expected a closed engine to reject a new setter")
	}
	if e.IsOpen() {
		t.Fatal("expected IsOpen to be false after Close")
	}
	e.Close() // idempotent
}

func TestNonRenderOperationRacesContextClose(t *testing.T) {
	c := NewContext()
	entered := make(chan struct{})
	release := make(chan struct{})
	nativeOpTestHook = func() {
		close(entered)
		<-release
	}
	defer func() { nativeOpTestHook = nil }()

	opDone := make(chan error, 1)
	go func() { opDone <- c.SetString("s", "x") }()
	<-entered // setter admitted, holding the context lifecycle mutex

	closeDone := make(chan struct{})
	go func() { c.Close(); close(closeDone) }()
	select {
	case <-closeDone:
		t.Fatal("Close completed while an admitted context operation held the lifecycle mutex")
	case <-time.After(150 * time.Millisecond):
	}

	close(release)
	if err := <-opDone; err != nil {
		t.Fatalf("admitted context setter failed: %v", err)
	}
	<-closeDone
	if err := c.SetString("s", "y"); err == nil {
		t.Fatal("expected a closed context to reject a new setter")
	}
	c.Close() // idempotent
}

func TestQueryOperationRacesEngineClose(t *testing.T) {
	e := NewEngine()
	entered := make(chan struct{})
	release := make(chan struct{})
	nativeOpTestHook = func() {
		close(entered)
		<-release
	}
	defer func() { nativeOpTestHook = nil }()

	opDone := make(chan bool, 1)
	go func() { opDone <- e.IsOpen() }()
	<-entered // query admitted, holding the lifecycle mutex

	closeDone := make(chan struct{})
	go func() { e.Close(); close(closeDone) }()
	select {
	case <-closeDone:
		t.Fatal("Close completed while an admitted query held the lifecycle mutex")
	case <-time.After(150 * time.Millisecond):
	}

	close(release)
	if got := <-opDone; got {
		t.Fatal("admitted IsOpen unexpectedly true")
	}
	<-closeDone
}

func TestSetLoaderAfterCloseDoesNotPanic(t *testing.T) {
	e := NewEngine()
	e.Close()
	// Must be rejected cleanly (no registry lookup on e.id==0 after Close).
	e.SetLoader(func(path string) HostResult { return HostResult{} })
	e.SetEnvironmentProvider(func(name string) HostResult { return HostResult{} })
}

func TestProviderInstallRacesEngineClose(t *testing.T) {
	e := NewEngine()
	entered := make(chan struct{})
	release := make(chan struct{})
	nativeOpTestHook = func() {
		close(entered)
		<-release
	}
	defer func() { nativeOpTestHook = nil }()

	opDone := make(chan struct{})
	go func() {
		e.SetLoader(func(path string) HostResult {
			return HostResult{Status: HostFound, Value: "<p>P</p>"}
		})
		close(opDone)
	}()

	<-entered // SetLoader admitted, holding the lifecycle mutex before registry/native access
	closeDone := make(chan struct{})
	go func() { e.Close(); close(closeDone) }()
	select {
	case <-closeDone:
		t.Fatal("Close completed while an admitted provider install held the lifecycle mutex")
	case <-time.After(150 * time.Millisecond):
	}

	close(release)
	<-opDone
	<-closeDone
	e.Close() // idempotent
}

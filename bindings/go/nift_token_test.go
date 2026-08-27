package nift

import "testing"

// Token ownership: the C-owned user_data token must survive create/install/
// render/close cycles and provider replacement, and be freed at Close (no
// leak of the C token under -race).
func TestTokenLifetimeCreateInstallClose(t *testing.T) {
	for i := 0; i < 100; i++ {
		e := NewEngine()
		e.SetEnvironmentProvider(func(name string) HostResult {
			return HostResult{Status: HostFound, Value: "v"}
		})
		ctx := NewContext()
		r, err := e.RenderSources(RenderSource{Text: "@getenv(X)"}, RenderSource{Text: "<main>@content</main>"}, ctx)
		if err != nil || !r.OK || r.Output != "<main>v</main>" {
			t.Fatalf("iter %d: err=%v ok=%v out=%q", i, err, r.OK, r.Output)
		}
		ctx.Close()
		e.Close()
	}
}

// Provider replacement swaps the Go closure behind the same C-owned token; the
// new provider must be used and the old closure no longer reachable.
func TestProviderReplacement(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	e.SetEnvironmentProvider(func(string) HostResult {
		return HostResult{Status: HostFound, Value: "one"}
	})
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.RenderSources(RenderSource{Text: "@getenv(X)"}, RenderSource{Text: "<main>@content</main>"}, ctx)
	if err != nil || !r.OK || r.Output != "<main>one</main>" {
		t.Fatalf("before: err=%v ok=%v out=%q", err, r.OK, r.Output)
	}
	e.SetEnvironmentProvider(func(string) HostResult {
		return HostResult{Status: HostFound, Value: "two"}
	})
	r2, err := e.RenderSources(RenderSource{Text: "@getenv(X)"}, RenderSource{Text: "<main>@content</main>"}, ctx)
	if err != nil || !r2.OK || r2.Output != "<main>two</main>" {
		t.Fatalf("after: err=%v ok=%v out=%q", err, r2.OK, r2.Output)
	}
}

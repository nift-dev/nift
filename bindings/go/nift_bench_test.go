package nift

import "testing"

// Go-binding render overhead probe: measures ns/render through the Go binding
// (cgo -> C ABI -> C++ Embed). The direct C ABI figure is ~4.1 us/render on the
// same workload (c_abi_bench); cgo per-call overhead should be small relative
// to a render.
func BenchmarkRender(b *testing.B) {
	e := NewEngine()
	defer e.Close()
	ctx := NewContext()
	defer ctx.Close()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		r, err := e.Render("site=$[site] $[user.name]", "<main>@content</main>", ctx)
		if err != nil || !r.OK {
			b.Fatalf("err=%v ok=%v", err, r.OK)
		}
	}
}

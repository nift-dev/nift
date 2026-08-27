package nift

// Go 1.26 cgo pointer-pinning coverage: every goString() call site must keep
// its Go-backed pointer valid for the full C call. These tests deliberately
// build names, values, paths, titles and JSON at runtime (heap-backed, never
// compile-time literals) so a missing pin panics with "unpinned Go pointer".
// Run with `go test` and `go test -race`.

import (
	"fmt"
	"path/filepath"
	"strings"
	"testing"
)

func dynamicName(i int) string { return fmt.Sprintf("key%d", i) }
func dynamicValue(i int) string { return strings.Repeat(fmt.Sprintf("v%d|", i), i%4+2) }

func TestPointerPinningEngineSetters(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	// Heap-built names and values through every Engine setter kind.
	for i := 0; i < 8; i++ {
		name := dynamicName(i)
		value := dynamicValue(i)
		if err := e.SetString(name, value); err != nil {
			t.Fatalf("SetString(%q): %v", name, err)
		}
		if err := e.SetInt("i"+name, int32(i)); err != nil {
			t.Fatalf("SetInt: %v", err)
		}
		if err := e.SetNumber("n"+name, float64(i)+0.5); err != nil {
			t.Fatalf("SetNumber: %v", err)
		}
		if err := e.SetBool("b"+name, i%2 == 0); err != nil {
			t.Fatalf("SetBool: %v", err)
		}
		jsonText := fmt.Sprintf(`{"n":%d,"s":%q}`, i, dynamicValue(i+1))
		if err := e.SetJSON("j"+name, jsonText); err != nil {
			t.Fatalf("SetJSON: %v", err)
		}
	}
	// Render must see the heap-built bindings.
	r, err := e.RenderText("$[key0]|$[ikey0]|$[jkey0.n]|$[bkey0]")
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "v0|v0||0|0|true" {
		t.Fatalf("dynamic engine bindings: ok=%v out=%q err=%+v", r.OK, r.Output, r.Error)
	}
	// Invalid dynamic names must flow through the C validation and error.
	if err := e.SetString(dynamicName(99)+" with space!", "x"); err == nil {
		t.Fatal("invalid dynamic binding name must be rejected")
	}
}

func TestPointerPinningContextSetters(t *testing.T) {
	e, root := writeProject(t)
	defer e.Close()
	ctx := NewContext()
	defer ctx.Close()
	page := filepath.Join(root, "content", "about.html")
	// Heap-built page identity, current output and title.
	ctx.SetPageName(dynamicName(1))
	ctx.SetCurrentOutput(filepath.Join(root, "public", dynamicName(2)+".html"))
	ctx.SetTitle(strings.Repeat("T", 12))
	for i := 0; i < 6; i++ {
		name := dynamicName(i + 10)
		if err := ctx.SetString(name, dynamicValue(i)); err != nil {
			t.Fatalf("Context.SetString: %v", err)
		}
		if err := ctx.SetInt("i"+name, int32(i)); err != nil {
			t.Fatalf("Context.SetInt: %v", err)
		}
		if err := ctx.SetNumber("n"+name, float64(i)/2); err != nil {
			t.Fatalf("Context.SetNumber: %v", err)
		}
		if err := ctx.SetBool("b"+name, i%2 == 1); err != nil {
			t.Fatalf("Context.SetBool: %v", err)
		}
		jsonText := fmt.Sprintf(`{"j":%d}`, i)
		if err := ctx.SetJSON("j"+name, jsonText); err != nil {
			t.Fatalf("Context.SetJSON: %v", err)
		}
	}
	// Rendering with the dynamic request bindings + path source.
	name := dynamicName(10)
	r, err := e.RenderPathWithContext(page, ctx)
	if err != nil {
		t.Fatal(err)
	}
	_ = name
	_ = r
	// Exercise dynamic page name through a tracked render (unknown name -> error).
	unknown := dynamicName(1000)
	ru, err := e.RenderWithContext(unknown, ctx)
	if err != nil {
		t.Fatal(err)
	}
	if ru.OK {
		t.Fatalf("dynamic unknown page %q must fail", unknown)
	}
}

func TestPointerPinningRenderDynamicSources(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	if err := e.SetString("who", dynamicValue(3)); err != nil {
		t.Fatal(err)
	}
	// Heap-built render sources (previously the exact trigger for the panic).
	text := fmt.Sprintf("<p>%s</p>", dynamicValue(4))
	r, err := e.RenderText(text)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != text {
		t.Fatalf("dynamic renderText: out=%q", r.Output)
	}
	// Typed composition with dynamic text sources.
	page := fmt.Sprintf("$[who]|%s", dynamicValue(5))
	tpl := fmt.Sprintf("<main>@content</main>%s", strings.Repeat("x", 3))
	rc, err := e.RenderSources(RenderSource{Text: page}, RenderSource{Text: tpl}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !rc.OK || rc.Output != "<main>"+dynamicValue(3)+"|"+dynamicValue(5)+"</main>xxx" {
		t.Fatalf("dynamic composition: out=%q err=%+v", rc.Output, rc.Error)
	}
}
package nift

import (
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

func writeProjectFile(t *testing.T, root, rel, contents string) {
	t.Helper()
	full := filepath.Join(root, rel)
	if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(full, []byte(contents), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestABICompat(t *testing.T) {
	if err := ABICompat(); err != nil {
		t.Fatal(err)
	}
}

func TestBasicRender(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	if err := e.SetString("site", "hello"); err != nil {
		t.Fatal(err)
	}
	if err := e.SetJSON("user", `{"name":"Acme"}`); err != nil {
		t.Fatal(err)
	}
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.Render("site=$[site] $[user.name]", "<main>@content</main>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<main>site=hello Acme</main>" {
		t.Fatalf("got ok=%v output=%q", r.OK, r.Output)
	}
}

func TestRenderErrors(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	if err := e.SetString("9bad", "x"); err == nil {
		t.Fatal("expected invalid binding name error")
	}
	if err := e.SetJSON("site", "{not json"); err == nil {
		t.Fatal("expected malformed JSON error")
	}
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.Render("@content", "<main>@content</main>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if r.OK || r.Error == nil || r.Error.Message == "" {
		t.Fatal("expected a controlled render failure with a diagnostic")
	}
}

func TestIntegerWidth(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	ctx := NewContext()
	defer ctx.Close()
	if err := e.SetInt("imin", -2147483648); err != nil {
		t.Fatal(err)
	}
	if err := e.SetInt("imax", 2147483647); err != nil {
		t.Fatal(err)
	}
	if err := e.SetNumber("dbl", 3.5); err != nil {
		t.Fatal(err)
	}
	r, err := e.Render("$[imin]|$[imax]|$[dbl]", "<main>@content</main>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<main>-2147483648|2147483647|3.5</main>" {
		t.Fatalf("got %q", r.Output)
	}
}

func TestEmptyVsMissingEnv(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	e.SetEnvironmentProvider(func(name string) HostResult {
		if name == "EMPTY" {
			return HostResult{Status: HostFound, Value: ""}
		}
		return HostResult{Status: HostNotFound}
	})
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.Render("@getenv(EMPTY)|@getenv(MISSING)", "<main>@content</main>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<main>|</main>" {
		t.Fatalf("got ok=%v output=%q", r.OK, r.Output)
	}
}

func TestHostErrorFailsRender(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	e.SetEnvironmentProvider(func(name string) HostResult {
		return HostResult{Status: HostError, Error: "host exploded"}
	})
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.Render("@getenv(X)", "<main>@content</main>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if r.OK {
		t.Fatal("expected host failure to fail the render")
	}
	if r.Error == nil || !strings.Contains(r.Error.Message, "host exploded") {
		t.Fatalf("expected host failure diagnostic, got %+v", r.Error)
	}
}

func TestLoaderProvider(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	e.SetLoader(func(path string) HostResult {
		if strings.HasSuffix(path, "templates/template.html") {
			return HostResult{Status: HostFound, Value: "<main>@content</main>\n"}
		}
		if strings.HasSuffix(path, "content/blog.html") {
			return HostResult{Status: HostFound, Value: "<p>LOADER-CONTENT</p>\n"}
		}
		return HostResult{Status: HostNotFound}
	})
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.RenderPath("content/blog.html", "templates/template.html", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<main><p>LOADER-CONTENT</p></main>\n" {
		t.Fatalf("got ok=%v output=%q", r.OK, r.Output)
	}
	if len(r.Dependencies) != 2 {
		t.Fatalf("deps %v", r.Dependencies)
	}
}

func TestPanicContainedInCallback(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	e.SetEnvironmentProvider(func(name string) HostResult {
		panic("boom")
	})
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.Render("@getenv(X)", "<main>@content</main>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if r.OK {
		t.Fatal("expected the panic to become a host failure")
	}
	if r.Error == nil || !strings.Contains(r.Error.Message, "host callback failed") {
		t.Fatalf("expected host failure diagnostic, got %+v", r.Error)
	}
}

func TestResultOwnershipAcrossLifecycle(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	ctx := NewContext()
	defer ctx.Close()
	r, err := e.Render("hello", "<main>@content</main>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<main>hello</main>" {
		t.Fatal("unexpected result")
	}
	if _, err := e.Render("world", "<main>@content</main>", ctx); err != nil {
		t.Fatal(err)
	}
	if r.Output != "<main>hello</main>" {
		t.Fatal("earlier Go result was invalidated by a later render")
	}
	engine2 := NewEngine()
	if err := engine2.SetString("x", "1"); err != nil {
		t.Fatal(err)
	}
	ctx2 := NewContext()
	rr, err := engine2.Render("$[x]", "<main>@content</main>", ctx2)
	ctx2.Close()
	engine2.Close()
	if err != nil || !rr.OK || rr.Output != "<main>1</main>" {
		t.Fatal("second engine render failed")
	}
	if r.Output != "<main>hello</main>" {
		t.Fatal("earlier Go result was invalidated by another engine")
	}
}

func TestRepeatedCreateDestroy(t *testing.T) {
	for i := 0; i < 200; i++ {
		e := NewEngine()
		ctx := NewContext()
		r, err := e.Render("x", "<main>@content</main>", ctx)
		if err != nil || !r.OK {
			t.Fatalf("iteration %d: %v ok=%v", i, err, r.OK)
		}
		ctx.Close()
		e.Close()
	}
}

func TestConcurrentRenders(t *testing.T) {
	e := NewEngine()
	defer e.Close()
	var wg sync.WaitGroup
	for g := 0; g < 8; g++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			ctx := NewContext()
			defer ctx.Close()
			for i := 0; i < 200; i++ {
				r, err := e.Render("<h2>P</h2>", "<main>@content</main>", ctx)
				if err != nil || !r.OK || r.Output != "<main><h2>P</h2></main>" {
					t.Errorf("concurrent render: err=%v ok=%v out=%q", err, r.OK, r.Output)
					return
				}
			}
		}()
	}
	wg.Wait()
}

func TestProjectPagination(t *testing.T) {
	root := t.TempDir()
	writeProjectFile(t, root, ".nift/config.json", `{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","incremental-mode":"modified"}}`)
	writeProjectFile(t, root, ".nift/tracked.json", `{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]}`)
	writeProjectFile(t, root, "templates/template.html", "<main>$[title]</main>\n@content")
	writeProjectFile(t, root, "content/blog.html", "@item{one}@item{two}@item{three}@paginate")
	writeProjectFile(t, root, "content/blog.paginate.html", "<section>page $[paginate.current]/$[paginate.total]:[$[paginate.items]]</section>")

	e := OpenEngine(root)
	defer e.Close()
	if !e.IsOpen() {
		t.Fatalf("not open: %s", e.OpenError())
	}
	r, err := e.RenderPage("blog", nil)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK {
		t.Fatalf("render failed: %+v", r.Error)
	}
	if r.Output != "<main>Blog</main>\n<section>page 1/3:[one]</section>" {
		t.Fatalf("output %q", r.Output)
	}
	if len(r.Pages) != 2 || r.Pages[0].Page != 2 || r.Pages[1].Page != 3 {
		t.Fatalf("pages %+v", r.Pages)
	}
	if !strings.Contains(r.Pages[0].Output, "[two]") || !strings.Contains(r.Pages[1].Output, "[three]") {
		t.Fatalf("page outputs %q %q", r.Pages[0].Output, r.Pages[1].Output)
	}
	wantDeps := []string{"content/blog.html", "content/blog.paginate.html", "templates/template.html"}
	if len(r.Dependencies) != len(wantDeps) {
		t.Fatalf("deps %v", r.Dependencies)
	}
	for i := range wantDeps {
		if r.Dependencies[i] != wantDeps[i] {
			t.Fatalf("deps %v", r.Dependencies)
		}
	}
}

func TestReload(t *testing.T) {
	root := t.TempDir()
	writeProjectFile(t, root, ".nift/config.json", `{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","incremental-mode":"modified"}}`)
	writeProjectFile(t, root, ".nift/tracked.json", `{"tracked":[{"name":"blog","title":"GEN-A","template":"templates/template.html"}]}`)
	writeProjectFile(t, root, "templates/template.html", "<main>$[title]</main>\n@content")
	writeProjectFile(t, root, "content/blog.html", "<p>x</p>\n")
	e := OpenEngine(root)
	defer e.Close()
	r, err := e.RenderPage("blog", nil)
	if err != nil || !r.OK || !strings.Contains(r.Output, "GEN-A") {
		t.Fatalf("err=%v ok=%v out=%q", err, r.OK, r.Output)
	}
	writeProjectFile(t, root, ".nift/tracked.json", `{"tracked":[{"name":"blog","title":"GEN-B","template":"templates/template.html"}]}`)
	if err := e.Reload(); err != nil {
		t.Fatal(err)
	}
	r2, err := e.RenderPage("blog", nil)
	if err != nil || !r2.OK || !strings.Contains(r2.Output, "GEN-B") {
		t.Fatalf("err=%v ok=%v out=%q", err, r2.OK, r2.Output)
	}
}

package nift

// CP19 render API: Render(name) is ALWAYS a tracked page name; RenderPath is
// ALWAYS a filesystem path; RenderText is ALWAYS in-memory source. No
// existence-based dispatch.

import (
	"path/filepath"
	"strings"
	"testing"
)

func writeProject(t *testing.T) (*Engine, string) {
	t.Helper()
	root := t.TempDir()
	writeProjectFile(t, root, ".nift/config.json",
		`{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}}`)
	writeProjectFile(t, root, ".nift/tracked.json",
		`{"tracked":[{"name":"/","title":"Home","template":"templates/template.html"},{"name":"about","title":"About","template":"templates/template.html"},{"name":"products/headphones","title":"Headphones","template":"templates/template.html"}]}`)
	writeProjectFile(t, root, "templates/template.html", "<main>@content</main>")
	writeProjectFile(t, root, "content/index.html", "<p>home</p>")
	writeProjectFile(t, root, "content/about.html", "<p>about</p>")
	writeProjectFile(t, root, "content/products/headphones.html", "<h1>$[product.name]</h1>")
	e := OpenEngine(root)
	if !e.IsOpen() {
		t.Fatalf("not open: %s", e.OpenError())
	}
	return e, root
}

func TestRenderTrackedPage(t *testing.T) {
	e, _ := writeProject(t)
	defer e.Close()
	r, err := e.Render("about")
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<main><p>about</p></main>" {
		t.Fatalf("render(name): ok=%v out=%q err=%+v", r.OK, r.Output, r.Error)
	}
	ctx := NewContext()
	defer ctx.Close()
	if err := ctx.SetJSON("product", `{"name":"headphones"}`); err != nil {
		t.Fatal(err)
	}
	r2, err := e.RenderWithContext("products/headphones", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !r2.OK || r2.Output != "<main><h1>headphones</h1></main>" {
		t.Fatalf("render(name, ctx): out=%q", r2.Output)
	}
}

func TestRenderUnknownTrackedPage(t *testing.T) {
	e, _ := writeProject(t)
	defer e.Close()
	r, err := e.Render("no-such-page")
	if err != nil {
		t.Fatal(err)
	}
	if r.OK || r.Error == nil || !strings.Contains(r.Error.Message, "unknown") {
		t.Fatalf("unknown tracked page must be a controlled unknown-page error: ok=%v err=%+v", r.OK, r.Error)
	}
}

func TestRenderPath(t *testing.T) {
	e, root := writeProject(t)
	defer e.Close()
	path := filepath.Join(root, "content", "about.html")
	r, err := e.RenderPath(path)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<p>about</p>" {
		t.Fatalf("render_path(existing): out=%q", r.Output)
	}
	missing := filepath.Join(root, "does-not-exist.html")
	r2, err := e.RenderPath(missing)
	if err != nil {
		t.Fatal(err)
	}
	if r2.OK || r2.Error == nil {
		t.Fatalf("render_path(missing) must be a controlled missing-path error: ok=%v", r2.OK)
	}
	if r2.Output == "<p>about</p>" {
		t.Fatalf("missing path must never be reinterpreted as the literal file content")
	}
}

func TestRenderText(t *testing.T) {
	e, root := writeProject(t)
	defer e.Close()
	r, err := e.RenderText("<p>literal</p>")
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<p>literal</p>" {
		t.Fatalf("render_text: out=%q", r.Output)
	}
	// A text argument that happens to name an existing file must still render
	// as literal text - render_text never checks the filesystem.
	namesAFile := filepath.Join(root, "content", "about.html")
	r2, err := e.RenderText(namesAFile)
	if err != nil {
		t.Fatal(err)
	}
	if !r2.OK || r2.Output != namesAFile {
		t.Fatalf("render_text must not resolve its argument as a file path: out=%q", r2.Output)
	}
	ctx := NewContext()
	defer ctx.Close()
	if err := ctx.SetString("who", "world"); err != nil {
		t.Fatal(err)
	}
	r3, err := e.RenderTextWithContext("<p>$[who]</p>", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !r3.OK || r3.Output != "<p>world</p>" {
		t.Fatalf("render_text(text, ctx): out=%q", r3.Output)
	}
}

func TestRenderNoContextIsFresh(t *testing.T) {
	e, _ := writeProject(t)
	defer e.Close()
	a, err := e.RenderText("$[x]")
	if err != nil {
		t.Fatal(err)
	}
	ctx := NewContext()
	defer ctx.Close()
	if err := ctx.SetString("x", "value"); err != nil {
		t.Fatal(err)
	}
	b, err := e.RenderTextWithContext("$[x]", ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !b.OK || b.Output != "value" {
		t.Fatalf("context provided: out=%q", b.Output)
	}
	// unbound $[x] renders literally => no request state leaked from b into a.
	if !a.OK || a.Output != "$[x]" {
		t.Fatalf("no-context render must not reuse request state: out=%q", a.Output)
	}
}

func TestRenderCompositionTypedSources(t *testing.T) {
	e, root := writeProject(t)
	defer e.Close()
	pagePath := filepath.Join(root, "content", "about.html")
	tplPath := filepath.Join(root, "templates", "template.html")
	// path/path
	r, err := e.RenderSources(RenderSource{IsPath: true, Path: pagePath}, RenderSource{IsPath: true, Path: tplPath}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !r.OK || r.Output != "<main><p>about</p></main>" {
		t.Fatalf("path/path composition: out=%q", r.Output)
	}
	// text/text
	r2, err := e.RenderSources(RenderSource{Text: "<p>hi</p>"}, RenderSource{Text: "<main>@content</main>"}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !r2.OK || r2.Output != "<main><p>hi</p></main>" {
		t.Fatalf("text/text composition: out=%q", r2.Output)
	}
	// mixed text/path
	r3, err := e.RenderSources(RenderSource{Text: "<p>mixed</p>"}, RenderSource{IsPath: true, Path: tplPath}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !r3.OK || r3.Output != "<main><p>mixed</p></main>" {
		t.Fatalf("text/path composition: out=%q", r3.Output)
	}
}
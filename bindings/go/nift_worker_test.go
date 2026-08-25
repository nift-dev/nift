package nift

import (
	"strings"
	"sync"
	"testing"
)

// The critical binding regression: a C++ pagination worker thread invokes the C
// callback, which crosses into the Go host provider, which returns a result
// back to the render. Found/NotFound/Error must all behave correctly, and the
// Go provider must be safe under -race on C++-owned worker threads.
func TestPaginationWorkerEnvCallback(t *testing.T) {
	root := t.TempDir()
	writeProjectFile(t, root, ".nift/config.json", `{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}`)
	writeProjectFile(t, root, ".nift/tracked.json", `{"tracked":[
 {"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}},
 {"name":"other","title":"Other","template":"templates/template.html","paginate":{"items-per-page":1}}
]}`)
	writeProjectFile(t, root, "templates/template.html", "<main>$[title]</main>\n@content")
	writeProjectFile(t, root, "content/blog.html", "@item{one}@item{two}@item{three}@paginate")
	writeProjectFile(t, root, "content/other.html", "@item{a}@item{b}@paginate")
	// @getenv in the paginate templates forces the C++ pagination page loop to
	// invoke the Go environment provider on its worker threads.
	writeProjectFile(t, root, "content/blog.paginate.html", "<section>@getenv(FAIL_BARRIER) page $[paginate.current]</section>")
	writeProjectFile(t, root, "content/other.paginate.html", "<section>@getenv(OK_BARRIER) page $[paginate.current]</section>")

	e := OpenEngine(root)
	defer e.Close()
	if !e.IsOpen() {
		t.Fatalf("not open: %s", e.OpenError())
	}
	e.SetEnvironmentProvider(func(name string) HostResult {
		switch name {
		case "FAIL_BARRIER":
			return HostResult{Status: HostError, Error: "host exploded"}
		case "OK_BARRIER":
			return HostResult{Status: HostFound, Value: "ok"}
		default:
			return HostResult{Status: HostNotFound}
		}
	})

	// blog hits the failing env on a C++ worker thread -> render fails with the
	// host failure diagnostic.
	r, err := e.RenderPage("blog", nil)
	if err != nil {
		t.Fatal(err)
	}
	if r.OK {
		t.Fatal("expected the pagination-worker host failure to fail the render")
	}
	if r.Error == nil || !strings.Contains(r.Error.Message, "host callback failed") {
		t.Fatalf("expected host failure diagnostic, got %+v", r.Error)
	}

	// other hits the succeeding env on the worker thread -> renders.
	o, err := e.RenderPage("other", nil)
	if err != nil {
		t.Fatal(err)
	}
	if !o.OK || !strings.Contains(o.Output, "ok") || len(o.Pages) != 1 {
		t.Fatalf("ok=%v out=%q pages=%d", o.OK, o.Output, len(o.Pages))
	}
}

// Concurrent paginated renders from Go goroutines while C++ worker threads
// invoke the Go env provider; the race detector must be clean.
func TestPaginationWorkerConcurrent(t *testing.T) {
	root := t.TempDir()
	writeProjectFile(t, root, ".nift/config.json", `{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}`)
	writeProjectFile(t, root, ".nift/tracked.json", `{"tracked":[
 {"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}},
 {"name":"other","title":"Other","template":"templates/template.html","paginate":{"items-per-page":1}}
]}`)
	writeProjectFile(t, root, "templates/template.html", "<main>$[title]</main>\n@content")
	writeProjectFile(t, root, "content/blog.html", "@item{one}@item{two}@item{three}@paginate")
	writeProjectFile(t, root, "content/other.html", "@item{a}@item{b}@paginate")
	writeProjectFile(t, root, "content/blog.paginate.html", "<section>@getenv(FAIL_BARRIER) page $[paginate.current]</section>")
	writeProjectFile(t, root, "content/other.paginate.html", "<section>@getenv(OK_BARRIER) page $[paginate.current]</section>")

	e := OpenEngine(root)
	defer e.Close()
	e.SetEnvironmentProvider(func(name string) HostResult {
		if name == "FAIL_BARRIER" {
			return HostResult{Status: HostError, Error: "host exploded"}
		}
		return HostResult{Status: HostFound, Value: "ok"}
	})

	var wg sync.WaitGroup
	for g := 0; g < 8; g++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 0; i < 50; i++ {
				fail, err := e.RenderPage("blog", nil)
				if err != nil || fail.OK {
					t.Errorf("blog must fail: err=%v ok=%v", err, fail.OK)
					return
				}
				ok, err := e.RenderPage("other", nil)
				if err != nil || !ok.OK || !strings.Contains(ok.Output, "ok") {
					t.Errorf("other must succeed: err=%v ok=%v", err, ok.OK)
					return
				}
			}
		}()
	}
	wg.Wait()
}

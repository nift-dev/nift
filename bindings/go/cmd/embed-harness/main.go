// Command embed-harness drives the Go binding through the shared Embed
// contract's neutral protocol (JSON request on stdin -> JSON result on
// stdout), mirroring the C++/Rust engine harnesses. Used by
// nift-embed-regression-suite/embed/adapters/go-embed.
//
// Invocation (same shape as the other harnesses):
//
//	embed-harness <root> <page|-> <template|-> <page_name|->
//	            <current_output|-> <page_path|-> <template_path|-> <mode> [seam|-]
//
// Bindings arrive as `name=value` lines on stdin; a `json:` value prefix binds
// JSON.
package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"sort"
	"strings"

	"nift.dev/embed"
)

func main() {
	if len(os.Args) < 9 {
		fmt.Fprintln(os.Stderr, "usage: embed-harness root page template page_name current_output page_path template_path mode [seam]")
		os.Exit(2)
	}
	root := os.Args[1]
	pageText := dashOrEmpty(os.Args[2])
	templateText := dashOrEmpty(os.Args[3])
	pageName := dashOrEmpty(os.Args[4])
	currentOutput := dashOrEmpty(os.Args[5])
	pagePath := dashOrEmpty(os.Args[6])
	templatePath := dashOrEmpty(os.Args[7])
	mode := os.Args[8]
	seam := "-"
	if len(os.Args) > 9 {
		seam = os.Args[9]
	}

	var engine *nift.Engine
	if mode == "page" {
		engine = nift.OpenEngine(root)
	} else {
		engine = nift.NewEngine()
		engine.SetRoot(root)
	}
	defer engine.Close()

	// Bindings on stdin: name=value lines; json: prefix binds JSON.
	var loaderKeys []string
	context := nift.NewContext()
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}
		// A "@ctx " prefix binds the pair on the render Context instead of the
		// Engine, exercising the context-over-engine precedence contract.
		onContext := strings.HasPrefix(line, "@ctx ")
		if onContext {
			line = line[len("@ctx "):]
		}
		eq := strings.IndexByte(line, '=')
		if eq < 0 {
			continue
		}
		name, value := line[:eq], line[eq+1:]
		var err error
		if strings.HasPrefix(value, "json:") {
			if onContext {
				err = context.SetJSON(name, value[len("json:"):])
			} else {
				err = engine.SetJSON(name, value[len("json:"):])
			}
		} else {
			if onContext {
				err = context.SetString(name, value)
			} else {
				err = engine.SetString(name, value)
			}
		}
		if err != nil {
			fmt.Printf(`{"ok":false,"error":"invalid binding name: %s"}`+"\n", name)
			os.Exit(0)
		}
	}

	switch seam {
	case "loader":
		engine.SetLoader(func(path string) nift.HostResult {
			loaderKeys = append(loaderKeys, path)
			switch {
			case strings.HasSuffix(path, "/templates/template.html"):
				return nift.HostResult{Status: nift.HostFound, Value: "<main>@content</main>\n"}
			case strings.HasSuffix(path, "/content/blog.html"):
				return nift.HostResult{Status: nift.HostFound, Value: "<p>LOADER-CONTENT</p>\n"}
			case strings.HasSuffix(path, "/content/post.html"):
				return nift.HostResult{Status: nift.HostFound, Value: "@input(\"part.html\")\n"}
			case strings.HasSuffix(path, "/content/part.html"):
				return nift.HostResult{Status: nift.HostFound, Value: "<p>LOADER-PART</p>\n"}
			default:
				return nift.HostResult{Status: nift.HostNotFound}
			}
		})
	case "loader-error":
		engine.SetLoader(func(string) nift.HostResult {
			return nift.HostResult{Status: nift.HostError, Error: "host exploded"}
		})
	case "env":
		engine.SetEnvironmentProvider(func(name string) nift.HostResult {
			switch name {
			case "NIFT_ENV_A":
				return nift.HostResult{Status: nift.HostFound, Value: "alpha"}
			case "NIFT_ENV_B":
				return nift.HostResult{Status: nift.HostFound, Value: "beta"}
			default:
				return nift.HostResult{Status: nift.HostNotFound}
			}
		})
	case "env-error":
		engine.SetEnvironmentProvider(func(string) nift.HostResult {
			return nift.HostResult{Status: nift.HostError, Error: "host exploded"}
		})
	}

	defer context.Close()
	if pageName != "" {
		context.SetPageName(pageName)
	}
	if currentOutput != "" {
		context.SetCurrentOutput(currentOutput)
	}

	var result nift.Result
	var err error
	switch mode {
	case "page":
		result, err = engine.RenderPage(pageName, context)
	case "partial":
		result, err = engine.RenderPartial(pageText, context)
	default: // composed
		page := nift.RenderSource{}
		if pagePath != "" {
			page = nift.RenderSource{IsPath: true, Path: pagePath}
		} else {
			page = nift.RenderSource{Text: pageText}
		}
		tpl := nift.RenderSource{}
		if templatePath != "" {
			tpl = nift.RenderSource{IsPath: true, Path: templatePath}
		} else {
			tpl = nift.RenderSource{Text: templateText}
		}
		result, err = engine.RenderSources(page, tpl, context)
	}
	if err != nil {
		emit(map[string]any{"ok": false, "error": err.Error()})
		return
	}

	if !result.OK {
		// Errors carry only ok/error (no loaderKeys), matching the other
		// adapters: loaderKeys are part of the successful result.
		doc := map[string]any{"ok": false}
		if result.Error != nil {
			doc["error"] = result.Error.Message
		}
		emit(doc)
		return
	}
	deps := result.Dependencies
	if deps == nil {
		deps = []string{}
	}
	reqs := result.Requirements
	if reqs == nil {
		reqs = []string{}
	}
	pages := result.Pages
	if pages == nil {
		pages = []nift.Page{}
	}
	doc := map[string]any{
		"ok":           true,
		"output":       result.Output,
		"dependencies": deps,
		"requirements": reqs,
		"pagination":   pages,
	}
	if seam == "loader" {
		doc["loaderKeys"] = relativeKeys(root, loaderKeys)
	}
	emit(doc)
	return
}

func relativeKeys(root string, keys []string) []string {
	// Separator normalization: the engine reports loader keys with forward
	// slashes (generic_string) on every platform, so normalize both the root
	// prefix and each key before stripping.
	norm := func(s string) string { return strings.ReplaceAll(s, "\\", "/") }
	prefix := strings.TrimRight(norm(root), "/") + "/"
	seen := map[string]bool{}
	out := make([]string, 0, len(keys))
	for _, k := range keys {
		kn := norm(k)
		r := strings.TrimPrefix(kn, prefix)
		if !seen[r] {
			seen[r] = true
			out = append(out, r)
		}
	}
	sort.Strings(out)
	return out
}

func emit(doc map[string]any) {
	enc := json.NewEncoder(os.Stdout)
	enc.SetEscapeHTML(false)
	_ = enc.Encode(doc)
}

func dashOrEmpty(s string) string {
	if s == "-" {
		return ""
	}
	return s
}

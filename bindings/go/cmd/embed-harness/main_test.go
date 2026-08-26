package main

import (
	"reflect"
	"testing"
)

// CP17: loaderKeys separator normalization - a Windows-style root with
// backslash separators must still strip a forward-slash key prefix.
func TestRelativeKeysSeparatorNormalization(t *testing.T) {
	root := `C:\tmp\nift-embed-case-xyz`
	keys := []string{
		`C:/tmp/nift-embed-case-xyz/content/part.html`,
		`C:/tmp/nift-embed-case-xyz/templates/template.html`,
		`other/path`,
	}
	got := relativeKeys(root, keys)
	want := []string{"content/part.html", "other/path", "templates/template.html"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("relativeKeys(%q, %v) = %v, want %v", root, keys, got, want)
	}
}

func TestRelativeKeysPosix(t *testing.T) {
	root := `/tmp/nift-embed-case-xyz`
	keys := []string{`/tmp/nift-embed-case-xyz/content/a.html`}
	got := relativeKeys(root, keys)
	if !reflect.DeepEqual(got, []string{"content/a.html"}) {
		t.Fatalf("posix relativeKeys = %v", got)
	}
}

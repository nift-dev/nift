// CP18 part B: Go binding raw render + repeated/server render workload.
package main

import (
	"fmt"
	"os"
	"time"

	"nift.dev/embed"
)

func main() {
	e := nift.NewEngine()
	e.SetRoot("/")
	e.SetString("site", "nift")
	page := "<p>$[site]</p>"
	tpl := "<main>@content</main>"
	const n = 50000
	start := time.Now()
	for i := 0; i < n; i++ {
		if _, err := e.Render(page, tpl, nil); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
	}
	raw := float64(time.Since(start).Nanoseconds()) / float64(n)

	start = time.Now()
	for i := 0; i < 1000; i++ {
		c := nift.NewContext()
		c.SetString("who", "w")
		if _, err := e.Render(page, tpl, c); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(1)
		}
		c.Close()
	}
	server := time.Since(start)
	e.Close()
	fmt.Printf("go raw=%d ns/render server=%d ms/1000\n", int64(raw), server.Milliseconds())
}

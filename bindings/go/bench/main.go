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
	const rounds = 3
	rawBest := 1e300
	reqBest := 1e300
	for r := 0; r < rounds; r++ {
		start := time.Now()
		for i := 0; i < n; i++ { // raw: no request Context, engine-default binding
			if _, err := e.Render(page, tpl, nil); err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
		}
		raw := float64(time.Since(start).Nanoseconds()) / float64(n)
		if raw < rawBest {
			rawBest = raw
		}
		start = time.Now()
		for i := 0; i < 1000; i++ { // request-loop: fresh Context per request
			c := nift.NewContext()
			c.SetString("who", "w")
			if _, err := e.Render(page, tpl, c); err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
			c.Close()
		}
		req := float64(time.Since(start).Milliseconds())
		if req < reqBest {
			reqBest = req
		}
	}
	e.Close()
	fmt.Printf("go raw=%d ns/render request-loop=%d ms/1000 rounds=%d\n", int64(rawBest), int64(reqBest), rounds)
}

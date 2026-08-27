// CP18 part B: Go binding raw render + repeated/server render workload.
package main

import (
	"fmt"
	"os"
	"sort"
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
	// Warm-up round (unreported) so runtime/JIT settles before measuring.
	if _, err := e.RenderSources(nift.RenderSource{Text: page}, nift.RenderSource{Text: tpl}, nil); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	rawSamples := make([]float64, rounds)
	reqSamples := make([]float64, rounds)
	for r := 0; r < rounds; r++ {
		start := time.Now()
		for i := 0; i < n; i++ { // raw: no request Context, engine-default binding
			if _, err := e.RenderSources(nift.RenderSource{Text: page}, nift.RenderSource{Text: tpl}, nil); err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
		}
		rawSamples[r] = float64(time.Since(start).Nanoseconds()) / float64(n)
		start = time.Now()
		for i := 0; i < 1000; i++ { // request-loop: fresh Context per request
			c := nift.NewContext()
			c.SetString("who", "w")
			if _, err := e.RenderSources(nift.RenderSource{Text: page}, nift.RenderSource{Text: tpl}, c); err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(1)
			}
			c.Close()
		}
		reqSamples[r] = float64(time.Since(start).Milliseconds())
	}
	sort.Float64s(rawSamples)
	sort.Float64s(reqSamples)
	e.Close()
	fmt.Printf("go raw=%d ns/render request-loop=%d ms/1000 rounds=%d\n",
		int64(rawSamples[rounds/2]), int64(reqSamples[rounds/2]), rounds)
}

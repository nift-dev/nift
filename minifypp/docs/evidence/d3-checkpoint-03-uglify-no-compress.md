# D3 checkpoint 3: UglifyJS no-compress comparison

Date: 2026-09-13

Both tools processed the identical pinned `d3@6.3.1/dist/d3.js` fixture.
Minify++ used structured mode; UglifyJS 3.19.3 used `compress: false` with
mangling enabled. Both outputs passed `node --check`.

| Output | Bytes | Identifier bytes | Whitespace/unclassified bytes |
| --- | ---: | ---: | ---: |
| Minify++ structured | 336,276 | 156,404 | 13,180 |
| UglifyJS no-compress | 275,344 | 103,244 | 5,862 |
| Delta | 60,932 | 53,160 | 7,318 |

Identifier spelling accounts for 87.2% of the raw output gap. Identifier token
counts are effectively equal (46,019 versus 46,135), so the difference is not
primarily deleted code: UglifyJS emits much shorter names for the same broad
amount of syntax. Retained whitespace accounts for another 12.0%. Combined,
these categories explain approximately 99.2% of the no-compress gap.

The result confirms the benchmark-focused order: recover safe D3 mangling
coverage first, then address the remaining printer whitespace. Compressor
passes are not the primary explanation for this comparison.

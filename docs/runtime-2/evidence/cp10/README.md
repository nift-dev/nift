# CP10 evidence — script lexer and parser

Branch `runtime-2-cp10` (from `runtime-2` at `fbca7bc`), Linux x86-64,
2 vCPU, g++ -O2. Record: `../../checkpoints/cp10-parser.md`.

## Files
| File | Content |
| --- | --- |
| `corpus-full.json` | `rt_parser_corpus.py --paren full --fuzz-iters 200000` (5,000 `gen.py` programs, seed 1010): per-corpus counts, Opaque counts, error corpus, fuzz, paren oracle |
| `asan.txt` | `make test-rt-parser-asan` output |
| `callgrind.txt` | instruction counts for the parse-throughput comparison |

## Reproduce
```
make nift .build/rt-legacy-oracle .build/rt-parse-dump .build/rt-parser-unit
make test-rt-parser                     # unit + corpus (quick paren oracle), ~2.5 min
python3 tests/runtime2/rt_parser_corpus.py --build .build --nift ./nift \
    --paren full --fuzz-iters 200000 --record corpus-full.json      # ~12 min
make test-rt-parser-asan
# throughput: FILES = vm-corpus + semantics + cp410 probes + gen.py --count 500 --seed 1010
valgrind --tool=callgrind .build/rt-parse-dump bench {1,3} $FILES
valgrind --tool=callgrind .build/rt-legacy-oracle bench {1,3} $FILES
```

## Gate 1 — accept / reject (in-process legacy translator oracle)

| Corpus | Scripts | Legacy accepts | rt accepts | Legacy rejects (identical in rt) | Mismatches |
| --- | ---: | ---: | ---: | ---: | ---: |
| `tests/rt/syntax-errors` (hand-written, every message) | 34 | 0 | 0 | 34 | 0 |
| `tests/runtime2/vm-corpus` | 44 | 44 | 44 | 0 | 0 |
| `tests/runtime2/semantics` | 57 | 57 | 57 | 0 | 0 |
| `tests/runtime2/rt-diff-corpus` | 15 | 15 | 15 | 0 | 0 |
| `docs/evidence/cp410-competitiveness/probes` | 120 | 120 | 120 | 0 | 0 |
| `tests/*.f` | 1 | 1 | 1 | 0 | 0 |
| scripts from `tools/rt-diff/extract_smoke.py` (every `.f` heredoc state, modules included) | 1,759 | 1,759 | 1,759 | 0 | 0 |
| `tools/rt-diff/gen.py --count 5000 --seed 1010` (mains and modules) | 6,587 | 6,587 | 6,587 | 0 | 0 |
| **Total** | **8,617** | **8,583** | **8,583** | **34** | **0** |

No committed or generated corpus script is rejected by legacy at parse time,
so the reject side is covered by the hand-written set and the error corpus.
The CLI prints the same text: for example `nift tests/rt/syntax-errors/nested_while.f`
gives `error: <abs>/nested_while.f:4:7: function while requires '(...)'`, exit 1;
`parse_dump dump` gives the same position and message with
`[native.translation_error]`.

## Gate 2 — error corpus
`tests/rt/error_corpus.txt`: 1,500 distinct structural mutants of corpus
scripts (≤ 1,536 bytes) that legacy rejects at parse time. Built by
`rt_parser_corpus.py --regen-errors 1500` (`legacy_oracle mutate 1010 1500`:
5,483 mutants tried; 3,892 accepted by legacy, all of them accepted by rt;
1,500 rejected). rt: 1,500/1,500 identical (message, line, column); legacy
re-check: 1,500/1,500 (no drift). All 24 translation messages occur:

| Message | Cases |
| --- | ---: |
| fn requires '(name(args))' | 365 |
| fn requires a block | 355 |
| export requires '(binding)' | 236 |
| function if has no matching ')' | 104 |
| function while requires a block | 72 |
| function while requires '(...)' | 67 |
| function for requires a block | 52 |
| function if requires a block | 43 |
| struct requires '(name)' | 36 |
| struct requires a block | 34 |
| function for requires '(...)' | 23 |
| try requires a balanced block | 21 |
| function else requires block | 16 |
| function else if malformed | 15 |
| catch(identifier) requires a block | 14 |
| catch requires '(identifier)' | 12 |
| try requires catch(identifier) | 10 |
| function else if requires block | 7 |
| open comment '/*' has no close '*/' | 6 |
| enum requires 'Name { members }' | 4 |
| function if requires '(...)' | 3 |
| catch requires one valid identifier | 2 |
| open comment '@/*' has no close '*/' | 2 |
| throw requires an Error expression | 1 |

## Gate 3 — paren oracle (precedence and associativity)
8,566 accepted scripts (all corpora above except the hand-written errors;
17 entries listed in `tools/rt-diff/ignore.txt` skipped: clock, thread
interleaving). 7,987 scripts received at least one inserted pair of
parentheses, 167,754 pairs in total. Original vs reprint on legacy `nift`:
**0 divergent**; 7,094 of the identical runs exit 0.

Two artefacts were found and fixed in the oracle itself during the work (not
parser divergences): rewriting inside string literals changed output where
the loop path prints `$[...]` literally (semantics.md), so string literals are
no longer rewritten; and `smoke/v48_map_set_scaling_guard` prints a clock
difference, so the rt-diff ignore list is honoured.

## Opaque nodes on the corpora (units that would fall back)
| Corpus | Units with Opaque | Nodes by reason |
| --- | ---: | --- |
| vm-corpus | 1 | legacy-runtime-error 1 (`2 ** 10`) |
| semantics | 2 | legacy-runtime-error 2 (`2 ** 10`; nested quotes in interpolation) — both documented run-time errors |
| smoke scripts | 28 | legacy-runtime-error 12 (shell lines the heredoc extraction captured, `"say "hi" now"` ×6), template-text 21 (`@script` blocks, unterminated `import(` ×9, HTML-rendered lambda bodies ×2) |
| gen.py (6,587) | 0 | — |
| cp410 probes, rt-diff-corpus, tests/*.f | 0 | — |

## Fuzz and sanitizers
- `parse_dump fuzz 1010 200000` over vm-corpus, semantics and 200 `gen.py`
  programs (alternating structural and random-byte mutations, 1–8 edits):
  150,135 accepted, 49,865 rejected, no crash, slowest single parse 36 ms
  (machine shared with the paren-oracle run). Deep inputs (100/1,000/10,000
  nested `(`, `[`, unary `-`, `+ 1` chains) end in Opaque(nesting-limit).
- 10,000- and 50,000-deep nested `if` blocks: rt accepts with one
  Opaque(nesting-limit); legacy `nift` crashes (SIGSEGV, stack overflow) at
  10,000 — recorded, not changed (D-006).
- ASan/UBSan: see `asan.txt`.

## Throughput (callgrind)
877 files, 588,766 bytes; marginal cost per pass = (3 passes − 1 pass) / 2.

| | 1 pass | 3 passes | Per pass | Per KB |
| --- | ---: | ---: | ---: | ---: |
| legacy translation | 148,912,898 | 429,928,432 | 140,507,767 | 244,375 |
| rt parse (full AST) | 100,589,597 | 285,967,128 | 92,688,766 | 161,208 |

rt/legacy = 0.66. Legacy's figure covers translation only; it parses every
expression again each time the statement is evaluated, which rt does once.

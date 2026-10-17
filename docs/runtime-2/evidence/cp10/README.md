# CP10 evidence — script lexer and parser

Branch `runtime-2-cp10` (from `runtime-2` at `fbca7bc`), Linux x86-64,
2 vCPU, g++ -O2. Record: `../../checkpoints/cp10-parser.md`.

## Files
| File | Content |
| --- | --- |
| `corpus-full.json` | `rt_parser_corpus.py --paren full --fuzz-iters 200000 --negative-count 1500` (5,000 `gen.py` programs, seed 1010), after the adversarial review fixes: per-corpus counts, Opaque counts, error corpus, fuzz, 1 MB stack, negative gate, paren oracle |
| `asan.txt` | `make test-rt-parser-asan` output (after the review fixes) |
| `callgrind.txt` | instruction counts for the parse-throughput comparison |

## Reproduce
```
make nift .build/rt-legacy-oracle .build/rt-parse-dump .build/rt-parser-unit
make test-rt-parser                     # unit + corpus + negative gate (quick paren oracle), ~1 min
python3 tests/runtime2/rt_parser_corpus.py --build .build --nift ./nift \
    --paren full --fuzz-iters 200000 --negative-count 1500 --record corpus-full.json   # ~4.5 min
python3 tests/runtime2/rt_parser_negative.py --seed N --count 1500   # one negative-gate seed
make test-rt-parser-asan
# throughput: FILES = vm-corpus + semantics + cp410 probes + gen.py --count 500 --seed 1010
valgrind --tool=callgrind .build/rt-parse-dump bench {1,3} $FILES
valgrind --tool=callgrind .build/rt-legacy-oracle bench {1,3} $FILES
```

## Gate 1 — accept / reject (in-process legacy translator oracle)

| Corpus | Scripts | Legacy accepts | rt accepts | Legacy rejects (identical in rt) | Mismatches |
| --- | ---: | ---: | ---: | ---: | ---: |
| `tests/rt/syntax-errors` (hand-written, every message) | 34 | 0 | 0 | 34 | 0 |
| `tests/rt/legacy-cases` (adversarial-review probes) | 247 | 212 | 212 | 35 | 0 |
| `tests/runtime2/vm-corpus` | 44 | 44 | 44 | 0 | 0 |
| `tests/runtime2/semantics` | 57 | 57 | 57 | 0 | 0 |
| `tests/runtime2/rt-diff-corpus` | 15 | 15 | 15 | 0 | 0 |
| `docs/evidence/cp410-competitiveness/probes` | 120 | 120 | 120 | 0 | 0 |
| `tests/*.f` | 1 | 1 | 1 | 0 | 0 |
| scripts from `tools/rt-diff/extract_smoke.py` (every `.f` heredoc state, modules included) | 1,759 | 1,759 | 1,759 | 0 | 0 |
| `tools/rt-diff/gen.py --count 5000 --seed 1010` (mains and modules) | 6,587 | 6,587 | 6,587 | 0 | 0 |
| **Total** | **8,864** | **8,795** | **8,795** | **69** | **0** |

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
After the review: 8,813 scripts (all corpora above except the hand-written
errors, review probes included; 17 entries listed in
`tools/rt-diff/ignore.txt` skipped: clock, thread interleaving). 8,031
scripts received at least one inserted pair of parentheses, 173,626 pairs
in total (Await nodes are now wrapped too). Original vs reprint on legacy
`nift`: **0 divergent**; 7,206 of the identical runs exit 0. (Before the
review: 8,566 scripts, 167,754 pairs, 0 divergent.) With `==` mutated to
left-associative, `legacy-cases/c2_eqchain.f` and `c2_assoc.f` diverge.

Two artefacts were found and fixed in the oracle itself during the work (not
parser divergences): rewriting inside string literals changed output where
the loop path prints `$[...]` literally (semantics.md), so string literals are
no longer rewritten; and `smoke/v48_map_set_scaling_guard` prints a clock
difference, so the rt-diff ignore list is honoured.

## Gate 5 — negative gate (review T1)
`tests/runtime2/rt_parser_negative.py`: programs rt reads with zero Opaque
nodes run on legacy `nift`; a shape error, a paren-oracle difference on a
generated expression program (unless both runs fail) or a string printed
differently from rt's reading fails the gate.

| Run | Programs | Regular (rt, 0 Opaque) | Failures |
| --- | ---: | ---: | ---: |
| `make test-rt-parser` (seed 1010, 1,200 expression programs) | 1,947 | 869 | 0 |
| `corpus-full.json` (seed 1010, 1,500) | 2,372 | 1,049 | 0 |
| seeds 1–12, 1,500 each | 28,464 | 13,178 | 0 |

Review harness `exprfuzz.py`/`exprfuzz2.py`/`exprfuzz3.py` (1,500 programs
each, seeds 1–3; 13,500 programs): before the fixes 179 malformed-expression
hits on `exprfuzz.py` seed 1 alone; after, 0 on that run and 37 in total,
all value-dependent: `.length()` on an object or boolean (`o.length()`,
`o.m.length()`, `{"z": 1}.length()`, `t.length()`, `f(true).length()`), or
calling a member that holds a number or object (`o.m(1)`, `o.m.n(1)`,
`o.k(1)`); legacy reports these with the same "unknown value or malformed
expression" text, and the same shapes with values of a supporting type
(`xs.length()`, `o.f(1)`) run. The negative gate generates method calls only
on supporting receivers for that reason.

Review probes (`c1/`, `c2/`, now `tests/rt/legacy-cases`): every file is
Opaque in rt or runs on legacy without a shape error; the legacy failures on
regular nodes are value errors (unbound names, type errors, missing import
files, `export` of an unknown binding, `&=` on a non-atomic).

## 1 MB stack (review R1)
22 programs (if, fn, lambda, try, struct methods, parentheses, arrays,
calls, unary and postfix chains, interpolation; 300 and 5,000 deep) under
`ulimit -s 1024`: all parse, the excess is Opaque(nesting-limit); also with
the ASan/UBSan build (`asan.txt`). Before (block bound 4,096): 4,096 nested
`if` or `fn` blocks crashed `parse_dump` with SIGSEGV at 1 MB.

## Lint (CP09 `tools/rt-lint.py`, run from a scratch copy)
`src/rt`: before 15 problems (two files over 400 lines, `expr_parser.cpp`
without its header, 12 literal comparisons); after the split: 0.

## Opaque nodes on the corpora (units that would fall back)
| Corpus | Units with Opaque (before review → after) | Nodes by reason (after) |
| --- | ---: | --- |
| vm-corpus | 1 → 1 | legacy-runtime-error 1 (`2 ** 10`) |
| semantics | 2 → 3 | legacy-runtime-error 3 (`2 ** 10`; nested quotes in interpolation; `"abc"[1]`) — all documented run-time errors |
| smoke scripts | 28 → 41 | legacy-runtime-error 40 (shell lines the heredoc extraction captured, `"say "hi" now"` ×6, `true`/`false`/`null`/`42`/`4.5` `.to_string()` ×15, `{"a": "esc \" inside"}` ×6, `throw (1)` ×7), template-text 21 (`@script` blocks, unterminated `import(` ×9, HTML-rendered lambda bodies ×2), struct-body 2 (variadic constructor), irregular-legacy-parse 1 (`"SELECT $1, \"id $1\" …"`) |
| gen.py (6,587) | 0 → 0 | — (2,068 during the first round of fixes, see the record) |
| cp410 probes, rt-diff-corpus, tests/*.f | 0 → 0 | — |
| legacy-cases (247, review probes) | — → 83 | by design |

## Fuzz and sanitizers
- `parse_dump fuzz 1010 200000` over vm-corpus, semantics and 200 `gen.py`
  programs (alternating structural and random-byte mutations, 1–8 edits):
  150,135 accepted, 49,865 rejected, no crash, slowest single parse 2.5 ms
  after the review (36 ms before, machine shared with the paren-oracle run). Deep inputs (100/1,000/10,000
  nested `(`, `[`, unary `-`, `+ 1` chains) end in Opaque(nesting-limit).
- 10,000- and 50,000-deep nested `if` blocks: rt accepts with one
  Opaque(nesting-limit); legacy `nift` crashes (SIGSEGV, stack overflow) at
  10,000 — recorded, not changed (D-006).
- ASan/UBSan: see `asan.txt`.

## Throughput (callgrind)
Measured before the adversarial review (the review fixes add checks but no
new passes; not re-measured).

877 files, 588,766 bytes; marginal cost per pass = (3 passes − 1 pass) / 2.

| | 1 pass | 3 passes | Per pass | Per KB |
| --- | ---: | ---: | ---: | ---: |
| legacy translation | 148,912,898 | 429,928,432 | 140,507,767 | 244,375 |
| rt parse (full AST) | 100,589,597 | 285,967,128 | 92,688,766 | 161,208 |

rt/legacy = 0.66. Legacy's figure covers translation only; it parses every
expression again each time the statement is evaluated, which rt does once.

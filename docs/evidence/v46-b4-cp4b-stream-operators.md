# v4.6 Batch 4 CP4b+ stream insertion/extraction operators

Status: complete. Dedicated independently reviewed checkpoint that adds the
Strut-style stream operators on top of CP4b (`e648154`, not amended).

## What was added

```nift
ifs := ifstream("data.txt")
s := ""
i := 0
ifs >> s >> i          # whitespace-token extraction with destination-type conversion
ifs.close()

ofs := ofstream("report.txt")
ofs << "hello" << " " << 42 << " " << true     # chained insertion
ofs.close()
```

Operators work identically on streams opened later:

```nift
ifs := ifstream(); ifs.open("data.txt"); ifs >> s
ofs := ofstream(); ofs.open("out.txt");  ofs << s
```

## Design

- **Single write implementation.** `<<` and the explicit `write`/`write_line`/
  `write_bytes` methods all route through `Parser::stream_write_value`, so the
  formatting/render rules and `stream.write_failed` backend classification are
  shared. `ofs << error("boom")` is rejected (fatal non-renderable); the
  deliberate path remains `ofs << error("boom").stringify()`.
- **Single extraction implementation.** `>>` reads one whitespace-delimited
  token via `Parser::stream_extract_token` and converts it to the
  destination's type using the existing scalar machinery. String destinations
  receive the raw token; numeric and boolean destinations are parsed
  (`scalar_literal`) and must match the destination family; other destination
  types are unsupported and fail as data-conversion errors.
- **Chaining** is left-associative by splitting on the rightmost top-level
  operator: `ofs << a << b << c` writes a, then b, then c; `ifs >> a >> b >> c`
  reads a, then b, then c. Sequential semantics, not transactional: if `b`
  fails, `a` remains assigned and `c` is untouched.
- **Destinations** for `>>` must be a simple mutable binding; const,
  location-ref (aliased), and unsupported-type destinations are fatal
  programmer errors.

## Extraction distinctions

```text
backend read failure            -> stream.read_failed (recoverable)
normal EOF before a token       -> destination unchanged, not an error
token cannot be converted       -> fatal conversion/data error
RHS not assignable/mutable      -> fatal programmer error
output stream used with >>      -> fatal wrong-direction misuse
unopened/closed/invalid stream  -> fatal lifecycle misuse
```

## Precedence note

To keep near-zero overhead for ordinary expressions, `<<`/`>>` recognition is
integrated into the comparison-operator path. Chaining and mixing with the
relational `<`/`>` operators behave as expected (`s << a < b` is
`s << (a < b)`), but an unparenthesized `<<`/`>>` in the left side of an
equality/`<=`/`>=` expression parses the comparison at the top level
(`s << x == y` is `(s << x) == y`). Writing a comparison result to a stream
uses parentheses: `s << (x == y)`.

## Shell-command fallback is intentional and preserved

Unrecognized script statements still follow Nift's normal external-command
fallback. The operators are recognized only when the statement is actually
resolved as a stream operation:

- At expression level, `<<`/`>>` is handled lazily inside the existing
  comparison-operator loop, so only an expression whose first top-level
  `<`/`>` is a double sequence pays the stream path.
- At statement level, a command-shaped line is intercepted only when its first
  token resolves to a Nift stream binding; every other line keeps the existing
  command dispatch unchanged.

Regression tests verify `echo hello`, `printf x > f`, `cat f >> g`, `true`,
and other command-shaped lines behave identically to baseline.

## No duplicate evaluation

`stream << producer()` calls `producer()` exactly once; chained `<<` evaluates
each operand once; `>>` assigns the destination once.

## Wall

```sh
make test-v46-b4-cp4b-stream-operators
```

runs the immutable Batch 4A gate, pre-CP4, CP4a, CP4b, and the focused
stream-operator wall. Performance comparison vs `e648154` is documented in
`v46-b4-cp4b-stream-operators-performance.md`.
# Jsonic++ development handover

Jsonic++ is the standalone canonical project for the small dependency-free C++17 JSON implementation historically developed inside Nift and also used privately by Minify++.

- Canonical repository: `jsonic-cc/jsonic` at `https://github.com/jsonic-cc/jsonic`.
- Public website: `https://jsonic.cc` (source `jsonic-cc/jsonic-cc.github.io` on `stage`; generated output on `main`).
- Issue tracker: `https://github.com/jsonic-cc/jsonic/issues`.
- Release/development version: none defined in authoritative project files or handover notes; Jsonic++ is not scheduled for a tagged CLI release in this phase.
- Installer infrastructure: intentionally not provided. Jsonic++ is a dependency-free, header-only library and deliberately does not ship the Minify++/Markup++ curl-installer family (`install.sh`, `download.sh`, `update.sh`, `uninstall.sh`) or CLI release packaging. No `https://jsonic.cc/install.sh` endpoint is expected, and no curl-install command should be documented. This is an intentional project/distribution decision, not a tracked gap or outstanding work.

## Identity and boundary

- Product: **Jsonic++**.
- Tagline: **a tiny, embeddable JSON parser for C++.**
- Public header: `include/json.h`.
- Public namespace/type: `json::Document` plus `json::Type`.
- Toolchain: C++17; header-only parser/value implementation.
- Product boundary: parse JSON correctly, represent/query values, serialize when useful, and provide actionable errors. Strict RFC 8259 parsing is the default; configuration consumers may independently opt into comments and trailing commas. Do not grow into JSON Pointer/Patch, binary encodings, networking, schema frameworks, or a general serialization platform without a separately justified contract.

## Source of truth and vendoring

`include/json.h` is the canonical standalone source.

Current synchronized consumers:

1. **Nift core** vendors the exact header at `nift/jsonic/include/json.h`. `nift/src/Json.h` is intentionally only a compatibility wrapper so historical Nift includes remain stable.
2. **Minify++** vendors the same header privately at `minify/src/Json.h` because Minify++ remains independently vendorable/dependency-free.
3. **Nift's embedded Minify++** mirrors standalone Minify++, including its private `src/Json.h`; therefore a Jsonic++ change ultimately has to reconcile all three copies.

The maintenance direction is:

```text
Jsonic++ include/json.h
        ↓ validate standalone
Nift jsonic/include/json.h
        ↓ Nift JSON/schema/parser/incremental tests
Minify++ src/Json.h
        ↓ Minify++ full format/semantic tests
Nift minifypp/src/Json.h
        ↓ Nift minification integration
```

Never edit one vendored copy as an independent fork without recording why. Prefer making parser changes in Jsonic++, proving them there, synchronizing consumers, then running each consumer's own integration contracts.

Use:

```bash
make check-nift-sync NIFT_DIR=/path/to/nift
make check-minify-sync MINIFY_DIR=/path/to/minify
```

## Testing contract

Parser work should test both acceptance and rejection. Important families include JSON grammar, number grammar/range handling, duplicate keys, escapes, Unicode surrogate handling, deeply nested structures, serialization round trips, named-array streaming, malformed/error paths, and memory/lifetime safety.

The additive parsing surface consists of `ParseOptions`,
`DuplicateKeyPolicy`, and `ParseDiagnostic`. Preserve these contracts:

- no options means strict RFC 8259 JSON;
- comments and trailing commas are independent opt-ins;
- duplicate members are preserved by default and rejected only on request;
- diagnostic offsets are zero-based bytes while lines/columns are one-based;
- 512 remains the default depth limit, and consumers may lower it;
- do not burden every `Document` with source spans without a separately proven
  consumer need.

A substantial parser checkpoint should run at least:

```bash
make test
make test-sanitize
```

For lifetime/resource-safety work, `make memory-safety-checkpoint-1a` runs the maintained long-lived Jsonic++ corpus under ASan/LSan/UBSan and a separate non-sanitized RSS soak. `make valgrind-memory-safety-checkpoint-1a` is the independent Linux confirmation gate when Valgrind is available. Checkpoint 1A validated the corpus without requiring a parser implementation change. Checkpoint 1B then passed independently under Valgrind 3.26.0 on Linux: 40 corpus iterations, 0 errors, 0 bytes in use at exit, all 6,579,515 allocations freed. Exact evidence is recorded in `docs/MEMORY-SAFETY.md` and `docs/evidence/memory-safety-checkpoint-1b-valgrind.json`.

The independent `jsonic-cc/jsonic-conformance` repository is now the external
correctness gate. Its initial 810-case corpus found and permanently tests
duplicate-member acceptance, malformed raw UTF-8 rejection and bounded nesting.
Jsonic++ must retain 810/810 before benchmark work or public conformance claims.

and then the relevant Nift/Minify++ integration suites after synchronization. External conformance corpora and fuzzing are desirable production gates; preserve exact corpus/version evidence rather than converting one successful run into a timeless claim.

## Human-directed agentic engineering

Treat tests and conformance rules as executable contracts. Human direction owns scope and acceptance; agents may investigate, implement, fuzz and adversarially challenge the parser, but should provide evidence rather than assurances. Convert discovered failure classes into permanent regressions and leave handover state whenever a checkpoint changes parser semantics or consumer synchronization.

## Deeper handovers

- `docs/handover/ARCHITECTURE.md`
- `docs/handover/DEVELOPMENT.md`
- `docs/handover/TESTING.md`
- `docs/handover/DECISIONS.md`
- `docs/handover/ROADMAP.md`
- `docs/handover/PROJECT-HISTORY.md`

## Definition of done for parser changes

A parser change is not complete until standalone tests are green, synchronized copies are reconciled, affected consumer integration is green, public docs/release notes are reviewed, and this handover is updated if the synchronization model or durable contracts changed.

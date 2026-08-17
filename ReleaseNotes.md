# Nift — Release Notes

## Unreleased

- Added config-declared project contracts. Optional `config.contracts` entries map stable project-wide namespaces to JSON sources, and `$[...]` references lazily load those sources through the existing immutable JSON cache. Referenced outputs track both the contract source and `.nift/config.json` as dependencies.
- Contract namespaces are reserved project-wide: `@json` and `@for` bindings cannot shadow them. Missing/malformed sources, missing members, invalid declarations, and unsafe contract paths fail with controlled diagnostics.
- Added focused and standalone contract coverage for lazy loading, dependency/config remapping, parameter/control-flow integration, collision rules, path safety, and failure behavior.

- Tracked entries may omit `template`; Nift then fully parses the content file
  itself as the top-level source. Historical empty template strings remain a
  compatible alias, existing `@content` templates remain compatible, and new
  project scaffolds no longer create identity CSS/JavaScript templates.

## v1.0.42

- Textual directive parameters now support single-pass `$[...]` interpolation for `@input`, `@dep`, `@pathto`/`@pathtofile`, `@json` source/schema paths, `@getenv`, and `@ent`. Binding names and control-flow grammar remain static; substituted `@...` or `$[...]` text is data and is never recursively parsed.
- Dynamic parameter-selected inputs, dependencies, requirements, and JSON sources participate in the existing incremental and transactional contracts.
- Added an independent 73-check parameter-interpolation contract covering scalar types, escaping, lexical scope, injection boundaries, path safety, A-to-B dependency replacement, and failed-build recovery.

- Renamed the embedded standalone minifier project to **Minify++**. Its standalone executable is `minify`; Nift's opt-in `minify-exts`/`minify` build contract is unchanged.

- Memory-performance checkpoint for large tracked projects.
- Replaced the three node-based `unordered_set<string>` validation tables used while loading `tracked.json` with compact post-parse sorted validation. Duplicate names and derived content/output collisions remain rejected, while temporary validation memory is substantially lower.
- Releases the raw `tracked.json` source buffer before derived-path uniqueness validation so the two largest temporary allocations do not overlap.
- On the deterministic 10,000-page modified-mode fixture, peak RSS fell from roughly 13 MiB in v1.0.41 to roughly 10.5–11.5 MiB depending on the build case, bringing the rewrite back to the retained stripped-Nift 10k memory range.
- Added `benchmark-memory-10k`, with a deliberately conservative 16 MiB peak-RSS regression guard on Linux.

## v1.0.41

- Continued the 10,000-page performance recovery after the v1.0.40 O(n²) tracking-loader fix. Incremental metadata containment checks now cache canonical safety at the parent-directory level and perform the expensive full canonical check only for symlink leaves. This preserves the symlink-retargeting safety invariant while avoiding repeated canonicalisation of every ordinary content/template file.
- Reused each page-info modification timestamp while checking its dependencies instead of restatting the same page-info file for every dependency.
- Added `benchmark-10k`, a reproducible full/no-op/single-page/shared-template benchmark fixture alongside the existing tracked-loader scaling guard.
- Embedded Minify++ is synchronized to standalone Minify++ 1.0.5; the Nift integration still consumes it only through the public Minify++ API and remains opt-in through configured minification extensions.
- On the development host, the 10k modified-mode checkpoint measured roughly 0.21–0.26 s median full builds, 0.09–0.10 s no-op incremental checks, 0.09–0.11 s single-page updates and 0.31–0.35 s rebuilds after a shared-template change across the final profiling runs. These are checkpoint measurements, not portable guarantees.

## v1.0.40

- Restored near-linear tracked-project loading by replacing repeated duplicate/collision scans in `load_tracking()` with indexed `unordered_set` validation for names, derived content paths and derived output paths. This fixes the 10,000-page O(n²) regression introduced by safety validation.
- Added `test-tracking-scaling`, a synthetic 2k/10k project-open benchmark with a scaling-ratio guard so quadratic tracking validation cannot silently return.
- Began a dedicated incremental-performance pass. The remaining no-op/single-page gap is now isolated from project loading and points primarily at per-page metadata/dependency validation.

## v1.0.39

- Embedded Minify++ updated exactly to standalone Minify++ 1.0.3, including its new non-JS idempotence gate and larger JavaScript/JSX adversarial corpora.
- Nift integration continues to consume Minify++ only through the public `<minify/Minify.h>` API.
- Website dark-mode palette rebalanced from residual navy/teal surfaces toward near-black graphite cards/chrome with brighter emerald/lime Nift accents.

## v1.0.38

- Continued embedding Minify++ through its standalone public API; embedded transformation behavior remains aligned with Minify++ 1.0.1.
- Nift documentation now delegates detailed minifier correctness evidence to Minify++ while retaining integration/config/incremental safety coverage.
- No change to the safe `nift minify` contract: sibling `.min.*` output by default, explicit `-i` / `--in-place` replacement.

## v1.0.37

- Renamed the independent embedded minifier project to **Minify++**. The embedded project now lives under `minifypp/`, exposes `<minify/Minify.h>`, uses the `minify` namespace, and builds the standalone `minify` CLI.
- Nift continues to expose `nift minify` as a convenience command while consuming Minify++ only through its public library API.
- Minify++ remains non-destructive by default (`file.js` -> `file.min.js`) with explicit `-i` / `--in-place` replacement.
- Expanded Minify++'s generated executable JavaScript semantic corpus to 7,407 programs and JSX/TSX syntax/idempotence corpus to 140 programs.
- Fixed JSX-expression recognition of TSX generic arrows such as `<T,>(x:T) => ...`, which could otherwise be mistaken for nested JSX.

## v1.0.36

- Re-architected the minifier as a self-contained embedded subproject under `minifypp/`, with its own public include tree, implementation, private JSON parser, standalone CLI, Makefile and test entry points. Nift now consumes the minifier only through its public library API.
- Changed `nift minify file.ext` to the safe default `file.min.ext`; added `-i` / `--in-place` for explicit source overwrite.
- Added standalone CLI regression coverage for default sibling output, in-place mode, malformed transactional failure and unknown options.
- Expanded the executable JavaScript semantic differential matrix from 3,363 to 4,705 programs.
- Expanded the generated JSX/TSX syntax/idempotence corpus from 90 to 110 programs.
- Added `ARCHITECTURE_RULES.md` as a concrete design-review checklist for the stripped rewrite.
- Minifier format remains 1.0.

## v1.0.35

- Continued hostile minifier testing; minifier format remains 1.0.
- Expanded the executable JavaScript semantic differential matrix from 1,707 to 3,363 programs.
- Expanded the generated JSX/TSX syntax/idempotence corpus from 67 to 90 programs.
- Fixed generic JSX tags whose type arguments contain function types such as `<Comp<(x:number)=>string> ... />`; the `>` in `=>` is no longer mistaken for a generic closer.
- Added deeper async/generator/class-returned JSX, generic type arguments, regex-heavy expressions and modern JavaScript statement/regex combinations.
- All 19 focused Nift source-tree targets remain green.

## v1.0.34

- Continued hostile minifier testing while keeping minifier format version 1.0.
- Reworked the generated JavaScript semantic harness to batch Node execution while still minifying every source independently.
- Expanded executable JavaScript semantic coverage from 783 to 1,707 programs.
- Expanded the generated JSX syntax/idempotence corpus from 44 to 67 programs.
- Added modern CSS container/layer/:has()/nesting coverage, HTML template/textarea/raw-module-script coverage, and additional XML/SVG namespace/entity/CDATA/text preservation cases.
- Kept all existing JavaScript, JSX, HTML, CSS, JSON, XML and SVG minifier gates green.

## v1.0.33

- Continued hostile minifier testing; minifier format remains 1.0.
- Expanded the executable JavaScript semantic differential matrix from 591 to 783 programs.
- Expanded the generated JSX syntax/idempotence corpus from 24 to 44 programs.
- Fixed JSX-root detection when regular-expression character classes contain `<` or `>` characters.
- Added harder generic JSX component types, nested JSX-returning functions/classes, nullish/optional-call boundaries, regex-heavy expressions and nested attribute objects.

## v1.0.32

- Continued hostile minifier testing while keeping minifier format version 1.0.
- Added a generated JSX syntax/idempotence corpus validated against `tsc --noCheck --jsx preserve`.
- Fixed nested JSX after expression-prefix keywords such as `return` inside JSX expression blocks.
- Fixed generic JSX component tags such as `<Component<number> ... />`, whose generic `>` previously terminated the tag scan early.
- The JavaScript executable semantic matrix remains green at 591 programs.

## v1.0.31

- Continued hostile JavaScript minifier semantic testing; minifier format remains 1.0.
- Fixed regex classification after labelled blocks nested directly beneath control statements such as `if (x) label: {}`.
- Expanded the generated executable JavaScript semantic matrix to 591 programs while retaining division counter-cases.

## v1.0.30

- Continued hostile semantic testing while keeping minifier format version 1.0.
- Fixed direct function-expression and async-function-expression division being mistaken for regex syntax after the function body.
- Fixed regex-statement detection after `catch { ... }` without a catch binding.
- Expanded the generated Node semantic matrix from 222 to 379 executable programs with harder Unicode/escaped regexes, catch-without-binding, static blocks, labelled loops and function-expression counter-cases.

## v1.0.29

- Continued hostile semantic testing of the integrated minifier; minifier format remains 1.0.
- Expanded the generated JavaScript before/after semantic matrix from 143 to 222 executable programs.
- Fixed regex-literal detection after labelled blocks without biasing ternary/object-literal braces toward regex parsing.
- Added permanent labelled-block regex and ternary-object division counter-regressions.

## v1.0.28

- Continued hostile semantic minifier testing; no new minifier defect was found in this pass and minifier format remains 1.0.
- Expanded the generated Node differential matrix across do/while, try/finally, async and generator function declarations, default/derived class declarations, nested class declarations, and matching function/class-expression division counter-cases.

## v1.0.27

- Continued semantic stress testing of the integrated minifier; minifier format remains 1.0.
- Fixed the mirror-image JavaScript class ambiguity: regex literals after class declarations and division after class expressions now remain distinct.
- Added direct and Node differential regressions for anonymous/named class-expression division while retaining class-declaration regex coverage.

## v1.0.26

- Continued hostile semantic testing of the integrated minifier while keeping minifier format version 1.0.
- Fixed JavaScript regex-statement detection after class declarations without breaking object-literal division after `}`.
- Added a generated Node semantic corpus that combines block/class/function/control-flow endings with several regex literal forms and explicit division counter-cases.
- Expanded permanent minifier regressions for class-declaration regexes, including regex character classes that contain comment-looking characters.

## v1.0.25

- Continued hostile minifier semantic testing while keeping the minifier format version at 1.0.
- Fixed JavaScript regex-statement detection after statement blocks, function declarations and try/catch blocks, plus `for await (...)` control parentheses.
- Added direct and Node semantic differential regressions for those regex-vs-division boundaries, including object-expression division to guard the opposite interpretation.
- Expanded JSX/JavaScript hostile coverage without changing the public minification configuration.

## v1.0.24

- Kept the minifier format version at 1 (documented as 1.0) while pre-release hardening continues.
- Fixed JSX expressions containing nested JSX (including JSX-valued attributes) being routed through the plain JavaScript scanner and rejected. Nested JSX regions are now balanced and recursively handled by the JSX-aware path.
- Added permanent nested-JSX regression coverage and an executable JavaScript semantic-differential smoke test against Node when Node is available.

## v1.0.23

- Hardened the standalone/integrated minifier after another hostile semantic pass.
- Fixed JSX root detection misclassifying compact JavaScript comparisons such as `a<b&&c>d` and generic-looking expressions such as `foo<Bar>(x)` as JSX.
- Preserved required token boundaries around non-ASCII JavaScript/CSS identifiers such as `const π = 3`.
- Fixed HTML raw/preformatted close-tag matching so prefixes such as `</scriptx>` and `</prex>` do not terminate `<script>`/`<pre>` regions.
- Preserved XML processing-instruction data verbatim because PI whitespace is application-defined.
- Replaced naive JSX `{...}` brace matching with a JS-aware scanner that skips strings/templates, line/block comments and regular-expression literals, preventing braces inside those constructs from terminating JSX expressions.
- Hardened persisted minifier semantics while keeping the pre-release minifier format version at 1.0.
- Added Node differential execution checks during the hostile pass across ASI, regex/division, Unicode identifiers, templates, optional chaining, private fields, BigInt, labels and empty statements.

## v1.0.22

- Integrated the standalone minifier into tracked builds. Project-wide `config.json` may define `"minify-exts": [".html", ...]`; tracked entries may override that decision with `"minify": true` or `"minify": false`.
- Added project-independent `nift minify <files...>` for in-place minification of supported files anywhere the process can read/write.
- Supported minifier formats are final web artifacts: HTML/HTM, CSS, JavaScript (JS/MJS/CJS), JSX, JSON, XML and SVG. TypeScript/TSX/SCSS remain intentionally outside the minifier.
- Minification is transactional with rendering: a minifier error occurs before output/page-info are committed, preserving the last successful build.
- Page metadata now records effective minification state and a minifier format version so changes to configuration or future minifier semantics can invalidate incremental output safely.
- Added focused persistence/concurrency/failed-build-state coverage and integrated-minification coverage, plus additional ruthless black-box regressions.
- Hardened JSX attribute scanning for comparisons and nested expressions inside `{...}`.

## v1.0.21

- Ruthless post-feature hardening pass across JSON Schema, JSON bindings, control flow, sorting, loop metadata, internal `reqs`, and path/dependency handling.
- Fixed `@dep(...)` accepting parent-traversal paths outside the Nift project. `@dep` now applies the same project-containment boundary expected of project-local build inputs and stores the normalized project-relative dependency.
- Tightened concrete `@pathto(...)` path handling to reject paths outside the Nift project, matching its documented project-local contract.
- Expanded direct JSON Schema smoke coverage for supported shape validation, bounds, type unions, composition edge cases, boolean subschemas, local JSON Pointer escaping, invalid schema shapes, and bounded recursive references.
- Expanded parser/integration coverage for JSON Schema edge cases, empty/nested/sorted loops, scalar truthiness, object-key sorting, malformed sort clauses, corrupted/legacy `reqs` metadata, missing tracked-output requirements, and repair-on-rebuild semantics.
- UBSan build plus the ruthless adversarial extension completes without sanitizer findings.


## v1.0.20

- Added internal page `reqs` metadata populated automatically by ordinary `@pathto(...)`. Incremental/status checks treat a missing required path as a rebuild reason, but do not treat modification of a requirement as a rebuild reason. A missing req never aborts before rendering: `build-updated` runs the normal page build, allowing changed source to remove/fix the reference; if the reference remains invalid, the ordinary `@pathto` parser error is reported.
- Reqs have no public directive or sidecar format: they are an implementation detail of project-aware path correctness.

## v1.0.18

- Added optional JSON Schema validation to `@json(path, binding, schema)` using a documented Draft 2020-12-compatible subset. Schema files are project-bound, cached like other JSON inputs, and automatically participate in dependency-aware rebuilds.
- Added lexical `loop` metadata for `@for`: `$[loop.index]`, `$[loop.index0]`, `$[loop.first]`, `$[loop.last]`, and `$[loop.length]`. The name `loop` is reserved so user bindings cannot collide with the built-in metadata object; nested loops restore the outer metadata on exit.
- Added stable, non-mutating loop ordering with `@for(item : items by item.field asc)` and `desc`. Sort keys must be consistently numeric or consistently string-valued; invalid/missing/mixed keys fail explicitly rather than coercing.
- Added a dedicated JSON Schema C++ smoke suite and expanded JSON-binding, control-flow, and adversarial regression coverage for schema dependencies, validation failures, sorting, metadata scoping, and reserved-name collisions.

## v1.0.17

- Added `<`, `<=`, `>` and `>=` to `@if` conditions. Numbers use numeric ordering and strings use lexicographic ordering. Ordering requires both operands to have the same comparable type; Nift does not coerce between JSON types.
- Expanded control-flow tests for all four ordering operators, path-to-path ordering, operator text inside quoted strings, and invalid mixed/non-orderable comparisons.

## v1.0.16

A focused control-flow indentation correction.

- Multiline `@for` and `@if` block bodies now treat indentation inside `{...}` as source formatting rather than extra output indentation.
- Rendered block content aligns to the directive insertion point, matching `@input`/`@content` indentation semantics.
- Nested control flow composes indentation from the current insertion point while preserving relative indentation inside the block.
- Inline control-flow directives align repeated multiline output to the actual insertion column.
- Added direct control-flow smoke coverage plus regression/adversarial coverage for loops, conditions, `else`, nested blocks and `@input` inside loops.

## v1.0.15

A ruthless regression/source-audit release focused on correctness under hostile state and incremental edge cases.

- Hardened tracking/path validation, including duplicate derived output/content collisions.
- Prevented destructive `track`, `cp` and `mv` collisions with existing untracked files.
- Made user dependency sidecars participate correctly in invalidation and page lifecycle operations.
- Upgraded dependency hashing from 32-bit to 64-bit FNV-1a after constructing a real 32-bit collision reproducer.
- Preserved sub-second filesystem timestamps for modified-mode incremental checks.
- Rebuild when tracked metadata such as title/template/content/output mapping changes.
- Hardened watch reconciliation for missing directories, corrupt state and name collisions.
- Added lexical scoping for JSON bindings declared inside control-flow blocks.
- Tightened JSON grammar/Unicode handling and duplicate-key rejection.
- Tightened config/tracking validation and CLI command arity.
- Added compiler-generated header dependencies to the Makefile.
- Expanded adversarial regression coverage substantially.

## v1.0.14

### Structured JSON control flow

- Added `@if(condition){...}` with ordinary `else if (...) {...}` and `else {...}` chains.
- Added `@for(item : array){...}` for JSON array iteration.
- Added `@for((key, val) : object){...}` for JSON object iteration.
- Conditions support JSON/page-metadata truthiness, `!value`, `==` and `!=` scalar comparisons.
- Loop values remain full JSON documents, so member/index access can be chained arbitrarily with `$[...]`.
- Loop bindings are lexical to the loop body and nested loops can safely shadow and restore JSON bindings.
- Loop variables cannot use built-in metadata names such as `title` or `name`.
- Object iteration preserves the JSON document's stored member order.
- Unselected `@if` branches are not parsed, so build-time effects such as `@input`/`@dep` occur only in the selected branch.
- Control flow driven by `@json` remains dependency-aware because the JSON source file is already recorded as a page dependency.

### JSON/cache lifecycle

- Site-wide JSON documents loaded with `@json` remain immutable and shared through the per-build JSON cache across page workers.
- Project config and tracking state remain loaded once into native project structures rather than being reparsed/exposed as implicit template JSON bindings.
- Previous page-info JSON is intentionally not preloaded into template scope because it is build-state that may be absent or stale before rendering.
- Expanded the black-box regression baseline to 363 assertions/tests, including adversarial structured-control-flow coverage.

## v1.0.13

### Structured JSON data in templates

- Added `@json(path, name)` to bind a project JSON file to a page-scoped name.
- Added arbitrary chained JSON access through metadata-style expressions such as `$[site.example[3].deep.items[0].name]`.
- JSON bindings are visible through nested `@input(...)` calls because inputs share the current page render context.
- Strings, numbers, booleans and null values render directly; rendering an object or array without selecting a member/element is an informative build error.
- Missing members, invalid/out-of-range indices, malformed JSON, invalid/duplicate aliases, missing files and project-path traversal produce source-aware errors.
- `@json` files are recorded automatically as page dependencies and participate normally in modified, hash and hybrid incremental builds.
- Parsed JSON documents are cached once per build and shared immutably across page workers.
- Expanded the deep regression suite from 279 to 304 assertions/tests; full suite passes.

## v1.0.12

### Simplified comments

- Removed parsed block comments from the language.
- `@#-- ... --#` is no longer recognized as a special parsed-comment form.
- Nift comments are now consistently non-executing: `<#-- ... --#>` for raw multiline comments and `@# ...` / `@// ...` for raw single-line comments.
- Normal HTML `<!-- ... -->` comments remain part of generated HTML.
- This removes surprising “commented text still executes” behaviour and simplifies the parser.

## v1.0.11

### Preformatted indentation compatibility

- Fixed indentation leaking from an `@content`/`@input` insertion point into `<pre*>` block contents.
- Insertion now preserves stripped Nift's rule: normal lines inherit the caller's indentation, while newlines inside a preformatted block do not receive template indentation.
- The fix applies equally to tracked content and recursively parsed input files.
- Added focused regression coverage for `<pre class="...">` and ordinary `<pre>` blocks through both `@content` and `@input`.

## v1.0.10

### Parsed tracked content compatibility fix

- Fixed a fundamental parser regression where `@content` appended tracked content as raw text instead of parsing it as Nift source.
- Nift expressions inside tracked content now work as intended, including `@pathto(...)`, `@input(...)`, `$[...]`, `@getenv(...)`, `@ent(...)` and `@dep(...)`.
- Tracked content now participates in the parser input stack so content/input recursion is detected cleanly.
- Added focused regression coverage for expressions and nested inputs inside tracked content.

## v1.0.9

### Installation and Makefile portability

- Added `make install` and `make uninstall`.
- Added conventional `PREFIX`, `BINDIR` and `DESTDIR` overrides for Unix packaging and custom installs.
- Unix-like systems install to `/usr/local/bin` by default.
- Windows GNU Make builds use an `.exe` target and default to a per-user Nift directory under `%LOCALAPPDATA%`.
- Removed the hard-coded `/tmp` path from the standalone JSON test; test binaries now live under the local `.build/` directory and are removed by `make clean`.

This file records the development history of the current C++ implementation. It began as a clean implementation of stripped Nift behaviour and progressively became the primary replacement candidate.

## v1.0.8

### Presentation and repository polish

- Replaced the development-oriented README with a public-facing GitHub README covering Nift's purpose, quick start, templating model, incremental builds, commands, project structure, performance, testing and contribution guidance.
- Redesigned `nift about` as a proper project introduction with terminal colour, a compact Nift wordmark, a short description and the official `https://nift.dev` address.
- Added `about` to `nift commands`.
- Added this release history.

## v1.0.7

### Performance replacement-candidate pass

- Parallelised incremental `build_reasons` analysis used by `build-updated` and `status`.
- Replaced expensive filesystem-resolving relative-path operations on hot project paths with lexical path operations.
- Added bounded dependency-hash caching and per-build hash refresh deduplication.
- Reduced repeated hashing/writing of shared dependencies.
- Optimised hot page-info JSON serialization.
- Improved renderer hot paths and shared raw-source file reads without caching rendered partial output.
- Restored negative `build-threads` multiplier semantics.
- Ruthlessly optimised the standalone JSON implementation:
  - compact flat object storage instead of allocation-heavy tree maps;
  - faster common string parsing;
  - lower successful-parse diagnostic overhead;
  - streaming loading for large `tracked.json` arrays;
  - fewer temporary allocations.
- Reduced peak memory substantially on the 10,000-page development fixture.
- Reached parity with or outperformed stripped v0.9 across the principal 10,000-page modified/hash benchmarks used during development.
- Kept the deep regression suite green.

## v1.0.6

### Plain `build-auto` logs

- Added an explicit scoped plain-output mode to the console layer.
- Removed ANSI colour sequences from `.nift/build-auto.log`, including output emitted by build worker threads.
- Preserved normal colour in the interactive `build-auto` control banner.

## v1.0.5

### Quieter `build-auto`

- Removed empty `not-tracking` output from successful `info` queries.
- Redesigned `build-auto` as a quiet continuous mode.
- Added immediate `q` to quit on interactive terminals.
- Added `.nift/build-auto.log` for meaningful build output.
- Changed the log writer to overwrite the file only when its contents actually change.
- Kept non-interactive `build-auto` suitable for CI/test harnesses.

## v1.0.4

### Status and inspection output

- Limited elapsed-time output to finite build commands instead of printing it for every command.
- Redesigned `status` as a dry-run incremental analysis similar in spirit to `git status`.
- `status` now reports which pages need rebuilding and why without modifying outputs or page metadata.
- Added compact grouping for large fan-out status results.
- Redesigned `info`, `info-all`, `info-names`, `info-tracking` and `info-watching` around structured JSON-style output.
- Added syntax colouring for interactive JSON output.
- Made redirected inspection output strict plain JSON for scripting.

## v1.0.3

### Safer project creation

- Prevented `nift init` from running inside an existing Nift project.
- Moved the existing-project check before all filesystem mutations.
- Preserved support for initialising Nift inside an ordinary non-empty directory.

## v1.0.2

### Build summaries and explanations

- Added accurate built/up-to-date/failed result accounting.
- Added rebuild-reason reporting to `build-updated`.
- Added concise successful page counts to full and targeted builds.
- Added elapsed-time summaries to finite build commands.
- Added anti-spam grouping for large rebuild sets.
- Reserved the 📦 emoji for the single final successful build summary instead of printing it per page.
- Changed incremental builds to queue only pages that actually need rebuilding.

## v1.0.1

### Real-project compatibility fixes

- Allowed bare parameterised function names such as prose `@input` to remain literal text while preserving errors for malformed actual calls.
- Fixed compatibility with read-only `.info.json` files produced by stripped Nift by making metadata writable only when required and restoring its read-only state.
- Verified the rewrite against an early real Nift documentation-site redesign.

## v1.0.0

### Initial architecture-compatible rewrite

- Reimplemented stripped Nift in modern C++17 while retaining a familiar multi-file architecture.
- Added separate project, parser, watch, filesystem, CLI and build-progress components rather than a single translation unit.
- Added a standalone human-readable JSON implementation with `Document& operator[]` and explicit JSON value types.
- Implemented multithreaded builds.
- Implemented modified, hash and hybrid incremental modes.
- Implemented tracked pages, user dependencies, recursive directory dependencies and watch state.
- Implemented the stripped templating surface including `@content`, `@input`, `@pathto`, `@dep`, `@getenv` and `@ent`.
- Added structured parser/build diagnostics with tracked name, source path, line/column and source context where available.
- Added TTY-aware colour and cleaner command output.
- Added delayed build-progress reporting so fast builds remain silent while longer builds show progress.
- Reached full compatibility with the then-current 280-assertion deep regression suite.

---

The rewrite version is intentionally separate from Nift's public product version while the replacement implementation is being evaluated.

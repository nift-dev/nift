# Minify++ architecture

This is the living implementation-oriented architectural map of Minify++. It
records the scanner model, language-specific risks, integration boundaries,
testing seams, performance assumptions, production hazards, and source-orientation
work required before substantial implementation changes.

The current standalone source, tests, Git history, benchmarks, Nift embedded copy,
and public documentation are authoritative. The inherited implementation knowledge
behind this document is less exact than for Nift; stable concepts are strong
guidance, while exact files, classes, and functions must be verified. Never rewrite
the implementation merely to make it resemble this description.

## Reconciled current architecture

This section records the August 2026 source reconciliation. It supersedes inherited
guesses elsewhere in this document. Re-run and revise it whenever the public API,
scanner organization, format set, CLI write model, tests, or embedded relationship
changes.

### Concrete source map

The standalone implementation is intentionally compact:

~~~text
include/minify/Minify.h
    public API, Format enum, format_version

src/Minify.cpp
    shared byte/word/whitespace helpers
    JSON, CSS, HTML, JavaScript, JSX, XML and SVG minifiers
    extension dispatch and public run dispatcher

src/Json.h
    private JSON parser used to validate JSON before whitespace removal

cli/main.cpp
    argument parsing, per-file orchestration, extension selection and file writes

Makefile
    C++17 library/CLI build and test targets

tests/
    focused C++ smoke tests
    Node JavaScript differential
    generated JavaScript semantic corpus
    generated JSX/TSX corpus
    generated cross-format/idempotence corpus
    cross-format adversarial and CLI smoke tests
~~~

The public API is a set of stateless string-to-string functions:

~~~text
html, css, javascript, jsx, json, xml, svg
format_for_extension
run
~~~

Each returns a boolean, writes a complete output string, and supplies a textual
error. The public Format enum contains Html, Css, JavaScript, Jsx, Json, Xml, and
Svg. Format version is currently 1. Supported extensions are .html/.htm, .css,
.js/.mjs/.cjs, .jsx, .json, .xml, and .svg, matched case-insensitively.

The durable architectural description is: **a collection of conservative forward
scanners behind a tiny stateless API**. For minification, an uncertain optimization
should preserve input because missed compression is preferable to changed program
or document semantics.

### Current architectural pressures

These are pressures to monitor rather than automatic redesign tasks:

- `Minify.cpp` grows as seven format grammars evolve; split it by coherent format
  ownership when navigation/change collisions justify it, while keeping the public
  functional API and avoiding a ceremonial class hierarchy;
- JavaScript/JSX logic risks becoming an implicit partial parser whose grammar is
  difficult to reason about; respond with precise structural understanding,
  conservative transforms, and evidence rather than a universal scanner abstraction;
- CLI writes use a sibling temporary directory and replacement commit, preserve
  existing permission bits, and reject symbolic-link destinations; broader
  platform rename/durability evidence remains a release concern;
- standalone and embedded byte equality is enforced on demand by the explicit
  18-file `check-nift-sync` gate; running it remains a checkpoint responsibility;
- structured error offsets may eventually improve CLI/Nift diagnostics without
  requiring a large error hierarchy.

A shared cursor utility is worthwhile only for genuinely common mechanics such as
position, bounds, and error offsets. HTML, CSS, JavaScript, JSX, JSON, XML, and SVG
must not be forced into shared semantic states merely because they scan strings.

This materially broadens the inherited three-format model. JSX is explicitly
supported, including substantial TSX-aware disambiguation, while TypeScript as a
general minification mode is not exposed. JSON is structurally validated. XML and
SVG are deliberately conservative lexical modes rather than validating parsers.

### Verified scanner organization

All formats consume complete input strings, clear and reserve an output string,
then scan forward. There is no universal token stream, full AST, global mutable
scanner state, repeated erase loop, or broad regular-expression replacement.
Shared helpers classify whitespace, conservatively treat non-ASCII bytes as
word-like, perform case-insensitive prefix matching, and emit a pending separator
only when adjacent word-like bytes would merge.

The CSS scanner is a direct stateful loop. It preserves quoted strings and escapes,
removes ordinary block comments, preserves /*! comments, tracks pending
whitespace, removes whitespace around a small delimiter set, and conservatively
retains authored spacing adjacent to plus and minus for evolving CSS math syntax.
It does not deeply parse URL or custom-property grammar; safety comes from limited
transformations and conservative token separation.

The HTML scanner distinguishes ordinary text, tags with quoted attributes,
comments, and raw pre/textarea/script/style content. It collapses rather than
deletes observable inter-element whitespace. It preserves conditional/server-style
comments and removes ordinary comments without inventing separators.

Contrary to one inherited architectural possibility, HTML does not delegate inline
script or style bodies to the JavaScript/CSS minifiers. It identifies a
case-insensitive raw closing tag with a boundary check and copies the body
verbatim. This is a deliberate conservative design and avoids mixed-language
scanner risk at the cost of missed compression.

The JavaScript scanner is a forward lexical/context scanner. It removes a line
terminator only after an explicit semicolon or opening brace and preserves other
significant newlines for ASI and future syntax. It removes comments, preserves
/*! comments, tracks control parentheses and block-versus-expression braces, and
maintains whether a regex may begin. Conservative JavaScript-only candidates
include guarded semicolon, boolean, radix-integer and string-quote shortening.
Regex copying handles escapes, character classes, and alphabetic flags. Separator
logic protects word joining, plus-plus, minus-minus, slash-slash, and numeric
literal/member access.

JavaScript backtick literals are currently copied as opaque quoted regions. This
preserves nested-template bytes safely but does not minify expressions within a
template literal. Tests explicitly protect raw template text and nested backticks.

JSX has a separate scanner layered around the JavaScript function. It finds JSX
roots conservatively, preserves markup and JSX text, recursively minifies brace
expressions, handles fragments and nested elements, skips regexes before looking
for markup, and distinguishes several TSX generic-arrow/type contexts from JSX.
The conservative JSX contract additionally preserves every non-trivia TSX token:
it shares comment and whitespace removal, but not JavaScript semicolon or literal
rewriting. This is substantially more capable than the inherited uncertainty
about JSX.

JSON first parses through the private Json Document implementation, rejects invalid
JSON, then removes whitespace outside quoted strings. XML/SVG share a conservative
XML-like scanner that removes comments, preserves CDATA and processing
instructions, collapses markup whitespace, and preserves all text whitespace.
The svg_mode parameter currently does not change behavior.

### Verified CLI and error model

The standalone CLI supports help/version, -i/--in-place, and one or more files.
Default output is stem.min.extension. Unknown options return status 2; no files or
any per-file failure returns non-zero. Multi-file processing is per-file
best-effort: failures set the final status but later files continue.

The CLI verifies regular-file status and extension before minification. It reads
the entire file, runs the library, then writes the destination. Library errors
clear output for the principal malformed-input paths and propagate to the CLI.

The standalone CLI checks stream-open/read state independently from regular-file
classification, so unreadable input is not mistaken for valid empty input. It
prepares complete output in a unique sibling temporary directory, preserves an
existing destination's permission bits, and commits by rename. Platforms whose
standard rename cannot replace an existing file use a recoverable backup/restore
fallback. Symbolic-link destinations are rejected explicitly rather than followed
or silently replaced. This provides a strong file-level commit boundary, while
durability across power loss and ownership/timestamp preservation are not claimed.

### Verified Nift relationship

The 19 mirrored standalone contract files—source, public header, private JSON
parser, CLI, Makefile, README, release notes, sync script, and all tests—are
currently byte-identical to their counterparts under Nift's minifypp subtree.
The embedded directory additionally contains local build artifacts. Run
`make check-nift-sync NIFT_MINIFYPP_DIR=/path/to/nift/minifypp` at synchronization
checkpoints. Standalone-only handovers and repository configuration are excluded
deliberately rather than treated as drift.

Nift compiles minifypp/src/Minify.cpp directly into its executable and includes
only the public header at the semantic boundary. ProjectInfo validates configured
minify extensions through format_for_extension, supports global extension settings
and per-tracked-item boolean overrides, calls run after complete Nift rendering,
and replaces the render buffer only on minifier success.

Nift records whether minification was effective plus format_version in per-page
build metadata. Changes to the setting or version become explicit rebuild reasons.
Unsupported forced formats and minification failures return before generated output
is written. Integration tests protect opt-in behavior, overrides, config
validation, version invalidation, failure preservation, parallel stateless use,
all seven formats, and the Nift minify CLI.

Nift's final file write also directly truncates its destination rather than using
an atomic rename. Minifier failure preserves previous output because it happens
before that write; write failure itself is not fully transactional. Page metadata
is written after output, so metadata-write failure can also leave output newer
than recorded successful state.

### Verified evidence and remaining tooling gaps

Current checked-in evidence includes focused format tests, CLI smoke, cross-format
malformed/idempotence tests, 15,459 generated executable JavaScript programs, 180
generated JSX/TSX syntax and idempotence cases, and 111 generated non-JavaScript
documents with a JSON structural oracle. The JavaScript function deliberately
preserves newlines, which is more conservative than many minifiers and significantly
reduces ASI risk.

In this reconciliation, the focused C++ smoke test, Node semantic differential,
15,459-program generated JavaScript corpus, cross-format adversarial gate, CLI
smoke, and Nift integration smoke passed. After tsc 7.0.2 became available, the
generated JSX/TSX gate also passed all 180 programs. The native 111-document
idempotence loop and its final Node `spawnSync` JSON structural oracle pass in an
unrestricted process environment. The same oracle can stall under the Codex
desktop process wrapper's stream restrictions; retain that as an execution-
environment limitation, not a skipped product result.

The repository contains repeatable deterministic multi-format fuzz-smoke,
ASan/UBSan, and per-format throughput/output-size/RSS benchmark targets. The
production audit's 70,000-case sanitizer run found and helped minimize three
token-manufacturing families: CSS comment delimiters, JSX openers, and HTML/XML
comment/CDATA-like prefixes. All retained cases and the full sanitizer workload
are green. The Makefile enforces C++17, `-O2`, `-Wall`, `-Wextra`, and `-pedantic`
for ordinary builds; the sanitizer target uses `-O1`, debug symbols, ASan, UBSan,
and frame pointers.

### Reconciliation conclusions

The inherited model was correct about the small scanner architecture,
whole-buffer API, language-specific semantics, conservative transformation
philosophy, stateless Nift integration, and correctness-first production bar.

It was stale or incomplete in four important ways:

1. The product supports seven formats, not three.
2. JSX/TSX-aware scanning is implemented and heavily tested.
3. HTML raw script/style is preserved, not delegated for nested minification.
4. Atomic output replacement, sanitizer/fuzz targets, and retained benchmarks are
   desired architecture but not current implementation.

The highest-value remaining architecture work is clean-package/platform evidence
and candid release review. Scanner semantic changes should still begin with
evidence, not rewrites.

## Product and architectural boundary

Minify++ takes ordinary supported HTML, CSS, or JavaScript, applies only
transformations justified as semantics-preserving for that language, and emits a
smaller equivalent artifact with little operational machinery:

~~~text
input bytes
→ explicit language selection
→ language-specific scanner
→ semantics-preserving compaction
→ complete output buffer
→ safe output commit
~~~

Its scope is minification, not bundling, transpilation, tree-shaking, module
resolution, project building, or broad semantic optimization. A universal web AST
would conflict with the intended small architecture unless evidence shows that a
scanner cannot safely support the claimed language surface.

A scanner is not blind character deletion. It must know enough lexical context to
distinguish syntax from data. A space may prevent token joining; a newline may
change JavaScript control flow; a comment may separate operators; a less-than sign
may be HTML markup or JavaScript data; a slash may be division, regex, or comment.

## Top-level design

The expected conceptual shape is:

~~~text
CLI and file orchestration
        ↓
extension or explicit mode dispatch
        ├── HTML scanner
        ├── CSS scanner
        └── JavaScript scanner
        ↓
completed output
        ↓
safe in-place replacement or caller-owned result
~~~

Language selection and unknown-extension behavior must be explicit. Do not guess
from arbitrary contents unless that becomes a tested public feature. The current
CLI defines supported extensions and multi-file behavior.

Keep HTML, CSS, and JavaScript algorithms separately understandable. Shared
helpers may cover bounds-safe lookahead, whitespace, span appending, or error
plumbing, but identifier and separator semantics often differ by language.
Avoid generic utility-library creep and universal token abstractions.

Whole-buffer input and output is a reasonable design for normal web assets. It
simplifies lookahead, state, error handling, and transactionality. Streaming or a
large intermediate representation should be justified by measurement and semantic
need rather than assumed superiority.

## Output and CLI transactionality

For in-place operation, never destroy the original before reading and minifying
successfully. The desired ordering is:

~~~text
read complete input
→ generate complete output
→ write temporary/replacement safely
→ commit
~~~

Audit behavior for disk-full, permission, rename, and partial-write failure.
Temporary replacement must consider permissions, ownership, timestamps, symlinks,
and platform-specific rename behavior. Opening the source with truncation before
minification completes is a production blocker.

For multiple files, document whether processing is independent, stops on first
failure, or validates all possible inputs first. Do not imply an all-or-nothing
project transaction unless implemented. Exit status zero must mean the documented
request succeeded; diagnostics paired with success status are unacceptable for
automation.

Validate existence, regular-file status, readability, extension, and symlink
semantics before destructive work. Standard input/output support is optional, not
an architectural requirement.

## Scanner invariants

At every position the active language scanner must know enough to answer:

~~~text
Is this byte syntax or data?
Can whitespace be removed or collapsed?
Can a comment be removed?
Is a replacement separator or newline required?
Does this delimiter change lexical state?
Am I inside a context that must be copied conservatively?
~~~

Prefer explicit state machines, small scanner functions, and lightweight prior
token categories over global regular-expression passes. Broad “remove comments,
collapse whitespace, remove punctuation spaces” regex pipelines are dangerous for
all three languages.

Every lookahead and lookbehind must be bounds-safe. Exercise end-of-file after
slash, backslash, star, quote, backtick, less-than, exclamation, and hyphen.
Avoid signed/unsigned and npos arithmetic hazards.

A forward input-to-output scan avoids repeated middle-of-string erase operations
and their quadratic shifting. Reserving approximately input size and appending
unchanged spans can be both simple and fast. Avoid simultaneously retaining input,
output, token stream, AST, normalized copy, and scratch copy unless required.

Malformed input must never cause out-of-bounds access, infinite loops, memory
corruption, or partial overwrite. The product may reasonably require valid syntax
and return a controlled error; it need not reproduce browser/compiler recovery.

## HTML model

HTML minification must distinguish at least the conceptual contexts:

~~~text
text
tag and declaration
single/double quoted attribute
comment
script/style raw text
preformatted or whitespace-sensitive content
~~~

Whitespace between tags can be visible text. For example, the space between two
inline elements may prevent words joining. Collapsing ordinary text whitespace may
be safe in some contexts; deleting it indiscriminately is not. A conservative
minifier need not maintain a huge element-category database merely to save a few
bytes.

Preserve whitespace in pre and textarea content. Attribute separators are lexical
boundaries. Quote removal, boolean-attribute rewriting, entity rewriting, and
optional-tag removal are semantic optimizations outside the conservative core
unless explicitly implemented and deeply tested.

Script and style bodies are not HTML. If inline content is minified, the HTML
scanner should identify raw-text boundaries and delegate complete contents to the
JavaScript or CSS scanner, with controlled error propagation. HTML scanning must
not interpret arbitrary quotes, angle brackets, or templates inside raw text as
HTML.

Audit comment handling, doctype validity, template elements, SVG and MathML
foreign content, raw-text closing tags, malformed comments, and leading/trailing
text whitespace. Browser/DOM probes are useful where golden output cannot prove
visible equivalence, but normalized DOM equality alone can hide significant text
nodes.

## CSS model

CSS requires explicit normal, comment, quoted-string, URL-like, and opaque/value
contexts plus token-boundary awareness.

Removing a block comment is not always equivalent to deleting its bytes: the
surrounding tokens may require a separator. Strings preserve their data and
escapes. URL forms can contain quotes, escapes, parentheses, spaces, and data URLs.
The scanner must not find their end naïvely.

Whitespace around many delimiters is removable, but only when adjacent lexical
tokens cannot merge. calc expressions have spacing/token rules. Custom-property
values can contain arbitrary token sequences interpreted after substitution, so
they deserve conservative handling.

Final-semicolon removal may be safe under defined conditions but needs tests around
custom properties, empty declarations, and malformed values. Unit-zero rewriting,
color shortening, and broader value optimization are semantic transformations and
should not be added merely for benchmark comparisons.

The support inventory should cover comments, strings, URLs/data URLs, custom
properties, calc and var, modern at-rules, nesting, and modern color functions.

## JavaScript model

JavaScript is the highest semantic risk. A compact scanner needs enough lexical or
light syntactic context for:

~~~text
single and double quoted strings with escapes
template raw text and nested template expressions
line and block comments
regex literals and character classes
division
identifiers, keywords, numbers, and operators
line-terminator-sensitive grammar
modern syntax
~~~

Template literals alternate between raw data and JavaScript expressions. Nested
templates, braces, strings, regexes, and escapes make “find next backtick”
incorrect. State or a stack must restore nested template contexts reliably.

Comment removal may require nothing, a space, or a newline. Automatic semicolon
insertion makes blind newline removal unsafe around return, throw, break, continue,
postfix increment/decrement, async forms, and related constructs. The classic
return-newline-object case is a production-critical regression family.

Slash classification cannot depend on one preceding character. Regex versus
division requires prior token/context information. Regex scanning must track
escapes, character classes, closing slash, and flags. Removing whitespace can also
create comments or new operators: plus-plus, minus-minus, shifts, slash-slash, and
modern punctuation combinations all need separator logic.

Inventory numeric forms, decimal-dot ambiguity, bigint and separators, optional
chaining, nullish operators, arrows, classes/private fields, modules, async/await,
generators, hashbangs, and Unicode escapes. Determine the effective ECMAScript
surface from code and tests.

Do not assume JSX or TypeScript support because a file ends in .js or belongs to a
web workflow. JSX introduces another language transition system; TypeScript should
normally compile to JavaScript before Minify++. Unsupported or unsafe syntax must
be rejected or documented rather than silently corrupted.

Unicode identifiers require special attention. Byte-oriented scanning can copy
UTF-8 safely because punctuation is ASCII, but ASCII-only identifier classification
can remove necessary separators near non-ASCII text. Treating unknown non-ASCII
bytes conservatively as identifier-like may be preferable to false confidence.

## Malformed input and error model

Deliberately test empty, whitespace-only, and comment-only input; unterminated
HTML/CSS/JS comments; unterminated strings, regexes, and templates; escaped final
characters; deep nesting; huge tokens; invalid UTF-8 or binary input; and every
scanner state at EOF.

Decide whether input is treated as bytes, expected to be UTF-8, or encoding
validated. Do not normalize Unicode accidentally. If nested scanner state uses
recursion, assess stack behavior on adversarial depth; an explicit state stack may
be safer.

Error reporting must distinguish unsupported extension, read failure, malformed
supported input, minification failure, and output commit failure. A lexical scanner
need not claim full syntax validation if it does not perform it.

## Nift integration and source ownership

The healthy boundary is:

~~~text
standalone Minify++ canonical semantic engine
→ small embedded library API
→ Nift completed render
→ minify only configured extensions
→ Nift output commit
~~~

Minify++ should know nothing about TrackedInfo, Nift templates, dependencies, or
project state. Nift should know little about scanner internals. Integration should
be native/in-process rather than shelling out through temporary files.

Map the embedded API, input/output ownership, language selection, and error
propagation. Minification failure should fail the Nift build under the documented
contract rather than silently fall back to unminified output. It should occur
before replacement of a previous valid generated artifact where possible.

Verify whether standalone source is canonical and whether every embedded file is
identical, wrapped, intentionally adapted, or stale. If standalone is canonical,
normal work is: change and test standalone, checkpoint, synchronize embedded
source, then test Nift. Never overwrite an embedded copy that may be ahead.
Machine-checkable comparison is preferable to prose.

## Performance model

The target shape is approximately linear:

~~~text
time: O(n)
memory: input + output
scanner state: O(1) or O(nesting depth)
~~~

Avoid repeated erase operations, rescanning from the start, many whole-file regex
passes, or unneeded AST/token-vector allocation. Measure many small files as well
as large throughput: startup, dispatch, per-file allocation, and filesystem cost
matter for websites.

Embedded Nift benchmarks should compare no minification, individual languages, and
all configured formats on realistic output. Record input/output bytes, time, and
RSS, but never rank compression independently of semantic evidence.

Parallel standalone processing may add more complexity than benefit. In Nift,
per-artifact parallelism can multiply input/output buffers and peak memory. Map the
current concurrency model before changing it. Scanner state should be local and
reentrant, never shared mutable global state.

## Evidence architecture

Minify++ needs complementary layers:

1. Focused golden tests for lexical decisions and exact regressions.
2. Semantic equivalence, especially executing original and minified JavaScript and
   comparing controlled stdout, status, serialized results, and side effects.
3. Real-world corpora from owned websites, templates, and modern assets.
4. Browser/parser probes for selected HTML and CSS semantic risks.
5. ASan, UBSan, adversarial cases, and separate fuzz targets for each scanner.
6. Standalone CLI and Nift integration tests.
7. Performance, memory, determinism, and possibly idempotence properties.

Reference minifiers are discovery tools, not byte-output specifications. A strong
differential compares original runtime, Minify++ runtime, and a reference runtime.
Corpus failures should be reduced into focused regressions while retaining useful
real files.

Potential properties are deterministic output, no crash or hang for arbitrary
bytes, no expansion for supported valid input where the contract permits it, and
idempotence if the design intends canonical one-pass output.

Build an explicit support matrix for HTML, CSS, and JavaScript with implemented,
tested, untested, unsupported, and uncertain categories. Website claims must match
this matrix; accepting an extension does not by itself establish modern-language
support.

Correctness evidence takes priority:

~~~text
JavaScript semantic change
CSS computed-behavior change
HTML text/DOM meaning change
memory-safety failure
input destruction
silent unsupported-syntax corruption
standalone/embedded drift
Nift silent fallback
~~~

are potential production blockers. Missed compression, slightly larger output,
and preserved whitespace are lower severity when semantics remain correct.

## Architectural decisions and warning signs

- Correctness outranks maximum compression.
- Minify++ is a minifier, not a bundler or optimizer pipeline.
- Language-specific scanners remain separately understandable.
- Input is scanned forward into a new output buffer.
- Scanner transformations require lexical/semantic justification.
- Standalone ownership is canonical only after repository verification.
- Nift minification remains opt-in and uses the final-output boundary.
- Supported language versions and subsets must be explicit before production.
- Malformed input fails controllably without data loss.
- Tests, semantic oracles, corpora, sanitizers, and fuzzing jointly establish trust.

Immediately scrutinize global regex transformations, blind whitespace or comment
deletion, one-character regex/division rules, non-escape-aware quote searches,
next-backtick template handling, in-place erase loops, early output truncation,
independent edits to standalone and embedded copies, shared global scanner state,
universal web abstractions, and broad modern-JavaScript claims without inventories.

Good work usually looks like a small scanner-state refinement, a focused regression
matrix, neighboring adversarial cases, semantic-equivalence evidence, and verified
Nift synchronization. An audit that proves current code sound is a valid knowledge
checkpoint; do not manufacture rewrites.

## Required repository reconciliation

Before substantial changes, produce a concrete map containing current files,
entries, state representations, ownership, error paths, tests, and benchmarks for:

~~~text
CLI parsing, language dispatch, file loading, and output commit
HTML text/tag/attribute/comment/raw/preformatted states
HTML script/style delegation and closing-boundary handling
CSS comments, strings, URLs, custom properties, and separator rules
JavaScript comments, strings, templates, regex/division, ASI, tokens, and operators
malformed-input and EOF handling
public library API and error propagation
standalone tests, semantic oracles, corpora, fuzzing, sanitizers, and benchmarks
Nift embedded source, API call, synchronization, and output transaction
build system, warning policy, and supported platforms
~~~

Trace representative cases through source:

- HTML ordinary text and inline-element whitespace.
- Inline script delegation and raw-text closing detection.
- CSS comment-boundary token joining and quoted-string preservation.
- JavaScript return-newline ASI behavior.
- Division, escaped regex with character class and flags, nested templates, and
  comment/operator joining.
- Files ending in each prefix/delimiter character.
- Safe in-place failure and one multi-file partial failure.

Answer from evidence whether scanners are character- or token-oriented; how
regex/division, ASI, nested templates, CSS comment separators, and HTML text spaces
work; what malformed input does; how Unicode is handled; whether JSX, foreign HTML,
custom properties, and modern syntax are supported; whether source copies match;
and exactly how Nift calls and commits Minify++ output.

Report contradictions rather than silently choosing a narrative. Current code may
already match this model, differ for good reasons, contain stronger capabilities,
or expose risky drift.

## Living production roadmap

The roadmap sequence is:

~~~text
architecture and ownership reconciliation
→ explicit language support matrix
→ semantic-risk audits
→ focused regression expansion
→ real-world corpus
→ JavaScript runtime differential
→ selected HTML/CSS browser probes
→ malformed and adversarial audit
→ sanitizers and fuzzing
→ performance and memory
→ CLI/API hardening
→ Nift synchronization and integration
→ precise documentation and website claims
→ clean-package release candidate
~~~

Revise priorities at every checkpoint based on what testing discovers. Production
means understanding the supported lexical and semantic surface well enough that
running Minify++ on supported production assets is a reasonable engineering
decision—not merely passing the tests that happened to exist.

Before production, resist variable renaming, dead-code elimination, constant
folding, CSS value rewriting, HTML optional-tag or attribute rewriting, aggressive
quote removal, and bundling. After the conservative core is proven, language
updates, diagnostics, platforms, standard streams, library packaging, longer
fuzzing, and selected safe transformations can receive separate checkpoints.

Keep this document living. When a scanner assumption changes, update code, tests,
the support matrix, this architecture, and the production roadmap. The objective
is not to preserve this description; it is to explain accurately why a very small
implementation is safe enough to trust.

## Jsonic++ relationship

`src/Json.h` is a vendored copy of standalone Jsonic++ `include/json.h`, not an independently evolving parser. This keeps Minify++ self-contained while giving JSON grammar/conformance work one canonical owner.

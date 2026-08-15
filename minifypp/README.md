# Minify++

**Executable:** `minify`

A small conservative C++17 multi-format minifier library and CLI.

It can be used standalone and is also embedded by Nift as an independent subproject.

The architectural boundary is intentional: Nift consumes only the public API in `include/minify/Minify.h`. The minifier does not depend on Nift's project model, template parser, tracking state, CLI, or build engine. Its private JSON parser is carried inside this directory so the subtree can later be extracted into a standalone repository without importing Nift internals.

Supported formats in format version 1 are HTML, CSS, JavaScript, JSX, JSON, XML and SVG.

```bash
make
make test
./minify app.js        # app.min.js
./minify -i app.js     # overwrite app.js
```

The library API accepts strings and returns strings/errors. File naming and destructive/non-destructive behavior belong to the calling CLI rather than the minification engine.

## Current adversarial gates

- 15,459 executable JavaScript semantic programs.
- 180 JSX/TSX syntax + idempotence programs.
- 111 generated non-JavaScript idempotence documents.
- standalone CLI smoke tests and format-specific C++ smoke tests.

The latest JSX fix distinguishes valid TSX generic arrows such as `<T,>(x:T) => ...` from nested JSX roots.


## v1.0.1 hardening checkpoint

Minify++ remains format/API version 1.0, with the standalone CLI checkpointed as 1.0.1. The generated executable JavaScript differential corpus has grown to **10,707 programs**, and the JSX/TSX syntax + idempotence corpus to **160 programs**. This round added more async/generator/class/destructuring/regex contexts and deeper TSX generic-arrow/type-expression cases. All standalone C++, Node differential, generated, JSX and CLI gates pass.

## v1.0.3 generated-corpus checkpoint

The JavaScript semantic matrix has grown from **10,707 to 15,459 executable programs** and the JSX/TSX syntax + idempotence corpus from **160 to 180 programs**. The generated-JS harness now batches thousands of independent minification inputs through one native process, retaining per-program transformation isolation while eliminating process-startup overhead. New cases cover destructuring-heavy statement boundaries, additional async/generator/class/object contexts, harder regex forms, generic JSX utility/mapped/conditional types, TSX generic arrows, MathML/SVG and nested assertion/satisfies expressions.

## v1.0.2 non-JS contract checkpoint

Added a standalone generated/idempotence gate for **39 HTML/CSS/JSON/XML/SVG documents**. JSON additionally receives a structural semantic-equivalence oracle and malformed-input rejection. XML/SVG are deliberately documented and tested as conservative lexical minifiers rather than validating XML parsers. The public header guard was also renamed from its historical Nift-era name to the standalone `MINIFYPP_MINIFY_H` identity.

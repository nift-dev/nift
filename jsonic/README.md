# Jsonic++

**Jsonic++ — a tiny, embeddable JSON parser for C++.**

Jsonic++ is a dependency-free, header-only C++17 JSON parser extracted from the JSON implementation battle-tested inside Nift and Minify++.

Project home: <https://github.com/jsonic-cc/jsonic>. Public website: <https://jsonic.cc>.

```cpp
#include "json.h"

json::Document value;
std::string error;
if (!json::Document::parse(R"({"name":"Jsonic++"})", value, error)) {
    // handle error
}
std::cout << value["name"].string << "\n";
```

The public header is deliberately just `include/json.h`. The API remains the existing `json` namespace and `json::Document` value type used by Nift. Parsing accepts `std::string`, null-terminated `const char*`, and bounded `std::string_view` input; the view overload avoids copying the source buffer.

Parsed numbers normally use the compact `Type::Number` double representation. Values whose integer precision, signed/fractional zero, or floating-point boundary spelling would otherwise be lost use `Type::StrNumber` internally. Both satisfy `is_number()` and expose the converted value through `num`, while serialization preserves the significant original representation.

## Parsing options and diagnostics

Parsing is strict RFC 8259 JSON by default. Configuration-file consumers can
explicitly enable comments and trailing commas without changing that default:

```cpp
json::ParseOptions options;
options.allow_comments = true;
options.allow_trailing_commas = true;
options.duplicate_keys = json::DuplicateKeyPolicy::Reject;

json::ParseDiagnostic diagnostic;
if (!json::Document::parse(source, value, diagnostic, options)) {
    std::cerr << diagnostic.message << " at "
              << diagnostic.line << ':' << diagnostic.column << '\n';
}
```

`ParseDiagnostic` exposes the zero-based byte `offset` and one-based `line` and
`column`. `ParseOptions::max_depth` defaults to 512 and can be lowered for a
particular input boundary. The existing error-string overloads remain
available, and the streaming `for_each_array_item()` API accepts the same
options. Duplicate members remain preserved in source order by default.

## Build and test

```bash
make test
make test-sanitize
```

## Scope

Jsonic++ parses, represents, queries and serializes ordinary JSON, with a small
opt-in JSON-with-comments profile for configuration files. It deliberately does
not try to become a JSON ecosystem containing JSON Pointer, Patch, binary
encodings, schema frameworks, networking or package-manager machinery.

The parser validates unescaped UTF-8, preserves duplicate object members in
source order, and rejects inputs deeper than the configured nesting limit before
they can exhaust the process stack.

## Vendored copies

Jsonic++ is the intended canonical standalone owner of the parser header. Nift mirrors the standalone project under `jsonic/` and consumes `jsonic/include/json.h`; Minify++ currently vendors the same parser privately at `src/Json.h`. Synchronization is checked with:

```bash
make check-nift-sync NIFT_DIR=/path/to/nift
make check-minify-sync MINIFY_DIR=/path/to/minify
```

See `HANDOVER.md` for maintenance rules.

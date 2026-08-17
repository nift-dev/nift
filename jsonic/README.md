# Jsonic++

**Jsonic++ — a tiny, embeddable JSON parser for C++.**

Jsonic++ is a dependency-free, header-only C++17 JSON parser extracted from the JSON implementation battle-tested inside Nift and Minify++.

```cpp
#include "json.h"

json::Document value;
std::string error;
if (!json::Document::parse(R"({"name":"Jsonic++"})", value, error)) {
    // handle error
}
std::cout << value["name"].string << "\n";
```

The public header is deliberately just `include/json.h`. The API remains the existing `json` namespace and `json::Document` value type used by Nift.

## Build and test

```bash
make test
make test-sanitize
```

## Scope

Jsonic++ parses, represents, queries and serializes ordinary JSON. It deliberately does not try to become a JSON ecosystem containing JSON Pointer, Patch, binary encodings, schema frameworks, networking or package-manager machinery.

## Vendored copies

Jsonic++ is the intended canonical standalone owner of the parser header. Nift mirrors the standalone project under `jsonic/` and consumes `jsonic/include/json.h`; Minify++ currently vendors the same parser privately at `src/Json.h`. Synchronization is checked with:

```bash
make check-nift-sync NIFT_DIR=/path/to/nift
make check-minify-sync MINIFY_DIR=/path/to/minify
```

See `HANDOVER.md` for maintenance rules.

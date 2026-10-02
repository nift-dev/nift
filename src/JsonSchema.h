#pragma once

#include "Json.h"
#include <string>

namespace jsonschema {

// Validates `instance` against a deliberately documented JSON Schema
// Draft 2020-12-compatible subset. The validator rejects unsupported
// validation keywords rather than silently pretending to enforce them.
// Local JSON Pointer references ("#/...") are supported; external $ref
// values are intentionally not resolved by Nift.
bool validate(const json::Document& instance,
              const json::Document& schema,
              std::string& error);

// Reports whether `schema` is a structurally valid schema definition (shape and
// supported keywords). Used to separate a malformed schema definition from a
// valid schema rejecting an instance, so producers can classify the former as
// fatal and the latter as recoverable.
bool schema_valid(const json::Document& schema,
                  std::string& error);

} // namespace jsonschema

#pragma once

// Compatibility include for Nift's embedded standalone-style Jsonic++ parser.
// Jsonic++ is canonical at github.com/jsonic-cc/jsonic; Nift mirrors the project
// under jsonic/ so existing internal includes can remain stable.
#include "../jsonic/include/json.h"

namespace nift_json {
// Nift historically rejected duplicate object keys everywhere it parsed JSON.
// Jsonic++ v1.0.0 defaults to RFC-preserving duplicate keys, so Nift opts back
// into strict rejection through this wrapper to keep the documented contract.
inline bool parse(const std::string& text, json::Document& result, std::string& error) {
    json::ParseOptions options;
    options.duplicate_keys = json::DuplicateKeyPolicy::Reject;
    return json::Document::parse(text, result, error, options);
}
}  // namespace nift_json

#pragma once
// Immutable compiler definition retained for a persistent named callable.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nift::rt {
namespace infer_detail { struct Ty; }
struct SessionCallFact {
    std::shared_ptr<const std::string> source;
    std::uint32_t begin = 0, end = 0;
    std::shared_ptr<const infer_detail::Ty> result;
};
struct SessionCapture {
    std::string name;
    std::shared_ptr<const infer_detail::Ty> shape;
    bool loop = false, readonly = false, const_array = false;
};
struct SessionDefinition {
    std::string name, path;
    std::shared_ptr<const std::string> source;
    std::uint32_t begin = 0, end = 0;
    bool lambda = false, structure = false;
    std::vector<SessionCapture> captures;
    std::vector<std::string> dynamic_fields;  // struct fields subject to legacy caller lookup
};
} // namespace nift::rt

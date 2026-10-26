#pragma once
// Immutable compiler definition retained for a persistent named callable.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace nift::rt {
namespace infer_detail { struct Ty; }
struct SessionCapture {
    std::string name;
    std::shared_ptr<const infer_detail::Ty> shape;
    bool loop = false, readonly = false, const_array = false;
};
struct SessionDefinition {
    std::string name, path;
    std::shared_ptr<const std::string> source;
    std::uint32_t begin = 0, end = 0;
    bool lambda = false;
    std::vector<SessionCapture> captures;
};
} // namespace nift::rt

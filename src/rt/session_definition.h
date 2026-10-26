#pragma once
// Immutable compiler definition retained for a persistent named callable.
#include <cstdint>
#include <memory>
#include <string>
namespace nift::rt {
struct SessionDefinition {
    std::string name, path;
    std::shared_ptr<const std::string> source;
    std::uint32_t begin = 0, end = 0;
};
} // namespace nift::rt

#pragma once

#include <cstddef>
#include <string>

namespace nift {

// A rendering failure. Rendering is not the same operation as building, so
// Embedded Nift reports errors through this type rather than the CLI's
// BuildError.
class RenderError {
public:
    std::string message;
    std::string source;   // logical source name (path, or the text source's logical name)
    std::size_t line = 0;
    std::size_t column = 0;
};

} // namespace nift

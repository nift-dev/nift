#pragma once

#include <string>
#include <vector>
#include "nift/render_error.h"

namespace nift {

class Engine;
struct RenderResultBuilder;   // internal; defined in the engine implementation

// The outcome of an Embedded Nift render: generated text plus the external
// inputs the renderer discovered. Dependency discovery lives here; whether and
// how those dependencies are persisted is the host's decision.
class RenderResult {
public:
    bool ok() const { return ok_; }
    const std::string& output() const { return output_; }
    const RenderError& error() const { return error_; }
    const std::vector<std::string>& dependencies() const { return dependencies_; }
    const std::vector<std::string>& requirements() const { return requirements_; }

private:
    friend class Engine;
    friend struct RenderResultBuilder;
    bool ok_ = false;
    std::string output_;
    RenderError error_;
    std::vector<std::string> dependencies_;
    std::vector<std::string> requirements_;
};

} // namespace nift

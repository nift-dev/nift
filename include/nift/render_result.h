#pragma once

#include <string>
#include <vector>
#include "nift/render_error.h"

namespace nift {

class Engine;
struct RenderResultBuilder;   // internal; defined in the engine implementation

// One paginated page beyond the primary (CP8: complete pagination in the
// public Embed contract). `page` is the 1-based page number (>= 2) and
// `output` is that page's fully rendered content. Pages are ordered ascending.
struct PaginationPage {
    std::size_t page = 0;
    std::string output;
};

// The outcome of an Embedded Nift render: generated text plus the external
// inputs the renderer discovered. Dependency discovery lives here; whether and
// how those dependencies are persisted is the host's decision.
//
// dependencies()/requirements() spell paths relative to the Engine root when
// a root is configured, and as supplied otherwise. Persistence (e.g. the CLI's
// .info.json) is the caller's concern.
//
// Complete pagination: `output()` is page 1 (the primary); `pagination()`
// returns pages 2..N in ascending page order, empty for a non-paginated
// render. Page numbers and rendered content are rendering semantics; output
// filenames/paths are a ProjectState/build concern and are deliberately NOT
// exposed here.
class RenderResult {
public:
    bool ok() const { return ok_; }
    const std::string& output() const { return output_; }
    const RenderError& error() const { return error_; }
    const std::vector<std::string>& dependencies() const { return dependencies_; }
    const std::vector<std::string>& requirements() const { return requirements_; }
    const std::vector<PaginationPage>& pagination() const { return pagination_; }

private:
    friend class Engine;
    friend struct RenderResultBuilder;
    bool ok_ = false;
    std::string output_;
    RenderError error_;
    std::vector<std::string> dependencies_;
    std::vector<std::string> requirements_;
    std::vector<PaginationPage> pagination_;
};

} // namespace nift

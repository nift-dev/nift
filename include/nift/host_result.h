#pragma once

#include <string>

namespace nift {

// The outcome of a host seam (loader / environment provider) lookup.
//
// A host can supply a value (possibly empty), report that the value is absent
// ("missing source" / "environment variable unset"), or report a controlled
// host FAILURE. A failure becomes a rendering error: the RenderResult is
// failed with `error` as the diagnostic, regardless of which engine thread
// invoked the seam (including the pagination worker threads). This is the
// Embed host-resource contract shared by C++, nift-rs and the C ABI.
enum class HostStatus {
    Found,    // a value (possibly empty) is supplied
    NotFound, // no value; the ordinary "missing/unset" case
    Error,    // the host failed; the render reports a controlled error
};

struct HostResult {
    HostStatus status = HostStatus::NotFound;
    std::string value; // valid when status == Found (possibly empty)
    std::string error; // diagnostic when status == Error
};

} // namespace nift

#include "rt/handles/types.h"

namespace nift::rt::handles {

const char* release_reason_name(ReleaseReason reason) {
    switch (reason) {
        case ReleaseReason::Explicit: return "explicit";
        case ReleaseReason::Abandoned: return "abandoned";
        case ReleaseReason::ContextEnd: return "context-end";
        case ReleaseReason::Shutdown: return "shutdown";
    }
    return "unknown";
}

} // namespace nift::rt::handles

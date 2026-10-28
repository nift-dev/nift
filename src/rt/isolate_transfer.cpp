#include "rt/isolate_transfer.h"

namespace nift::rt {

const char* transfer_status_name(TransferStatus status) {
    switch (status) {
        case TransferStatus::Ok: return "ok";
        case TransferStatus::RefusedFuture: return "refused-future";
        case TransferStatus::RefusedHandle: return "refused-handle";
        case TransferStatus::ProtoUnavailable: return "proto-unavailable";
        case TransferStatus::InvalidGraph: return "invalid-graph";
    }
    return "unknown";
}

void TransferGraph::clear() noexcept {
    nodes_.clear();
    tasks_.clear();
    root_ = 0;
    has_root_ = false;
}

} // namespace nift::rt

#include "rt/isolate_transfer.h"
#include "rt/shared_gc.h"

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

TransferGraph::~TransferGraph() { clear(); }
TransferGraph& TransferGraph::operator=(TransferGraph other) noexcept {
    nodes_.swap(other.nodes_); tasks_.swap(other.tasks_); shared_.swap(other.shared_);
    std::swap(root_, other.root_); std::swap(has_root_, other.has_root_);
    return *this;
}

void TransferGraph::clear() noexcept {
    const bool dropped_roots = !tasks_.empty() || !shared_.empty();
    nodes_.clear();
    tasks_.clear();
    shared_.clear();
    root_ = 0;
    has_root_ = false;
    if (dropped_roots) shared_collect();
}

} // namespace nift::rt

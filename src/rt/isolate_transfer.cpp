#include "rt/isolate_transfer.h"
#include "rt/shared_gc.h"
#include <limits>

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

std::size_t TransferGraph::storage_bytes() const noexcept {
    std::size_t bytes = 0;
    const auto add = [&](std::size_t n) {
        const auto max = std::numeric_limits<std::size_t>::max();
        bytes = n > max - bytes ? max : bytes + n;
    };
    add(nodes_.capacity() * sizeof(Node));
    add(shared_.capacity() * sizeof(shared_[0])); add(tasks_.capacity() * sizeof(tasks_[0]));
    for (const auto& n : nodes_) {
        for (const auto* s : {&n.text,&n.spelling,&n.name,&n.message,&n.code,&n.category,&n.source}) add(s->size());
        add(n.bytes.capacity()); add(n.children.capacity() * sizeof(n.children[0]));
        add(n.fields.capacity() * sizeof(n.fields[0]));
        for (const auto& field : n.fields) add(field.first.size());
    }
    return bytes;
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

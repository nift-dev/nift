#include "rt/shared_gc.h"
#include "rt/shared_state_internal.h"
#include "rt/parallel_task.h"
#include <atomic>

namespace nift::rt {
namespace {
std::atomic<bool> possible_cycles{false};
thread_local bool collecting = false;
using shared_detail::Entry;

template<class Visit> void graph_edges(const TransferGraph& graph, Visit visit) {
    for (const auto& p : graph.shared_backings()) if (p) visit(static_cast<Entry*>(p.get())->gc);
    for (const auto& p : graph.task_backings()) if (p) visit(p->gc);
}
template<class Visit> void edges(SharedGcNode& n, Visit visit) {
    if (n.kind == SharedGcNode::Kind::Entry) {
        auto& e = *static_cast<Entry*>(n.object);
        graph_edges(e.value, visit);
        for (const auto& item : e.buffer) graph_edges(item->value, visit);
    } else {
        auto& t = *static_cast<ParallelTaskRecord*>(n.object);
        // Pending tasks are roots. Their graph pins are deliberately not
        // subtracted, so concurrent execution cannot hide an external owner.
        std::lock_guard<std::mutex> lock(t.mutex);
        if (t.state == ParallelTaskState::Pending) return;
        graph_edges(t.input.seed_graph, visit);
        graph_edges(t.input.constants_graph, visit);
        graph_edges(t.result, visit);
        graph_edges(t.error_value, visit);
    }
}
void clear_edges(SharedGcNode& n) {
    if (n.kind == SharedGcNode::Kind::Entry) {
        auto& e = *static_cast<Entry*>(n.object);
        e.value.clear(); e.buffer.clear();
    } else {
        auto& t = *static_cast<ParallelTaskRecord*>(n.object);
        t.input.seed_graph.clear(); t.input.constants_graph.clear();
        t.result.clear(); t.error_value.clear();
    }
}
}
void shared_note_edges() noexcept { possible_cycles.store(true, std::memory_order_relaxed); }
void shared_collect() noexcept {
    if (collecting || !possible_cycles.load(std::memory_order_relaxed)) return;
    collecting = true;
    auto& r = shared_detail::registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    SharedGcNode* all = nullptr;
    const auto pin = [&](auto& weak, SharedGcNode::Kind kind) {
        auto p = weak.lock();
        if (!p) return;
        auto& n = p->gc;
        n.kind = kind; n.object = p.get(); n.incoming = 0; n.marked = false;
        n.work_next = nullptr; n.all_next = all; n.pin = p; all = &n;
    };
    for (auto& pair : r.entries) pin(pair.second, SharedGcNode::Kind::Entry);
    for (auto& pair : r.tasks) pin(pair.second, SharedGcNode::Kind::Task);
    for (auto* n = all; n; n = n->all_next) edges(*n, [](auto& child) { ++child.incoming; });
    SharedGcNode* todo = nullptr;
    const auto mark = [&](SharedGcNode& n) {
        if (n.marked) return;
        n.marked = true; n.work_next = todo; todo = &n;
    };
    for (auto* n = all; n; n = n->all_next)
        if (static_cast<std::size_t>(n->pin.use_count()) > n->incoming + 1) mark(*n);
    while (todo) {
        auto* n = todo; todo = todo->work_next;
        edges(*n, mark);
    }
    for (auto* n = all; n; n = n->all_next) if (!n->marked) clear_edges(*n);
    while (all) {
        auto* n = all; all = all->all_next;
        n->pin.reset();  // may erase its registry entry; all_next was saved
    }
    // No graph can retain an edge to an unregistered node: backing creation
    // and task identity registration precede shared_note_edges(). Under this
    // same registry lock an empty registry therefore starts a fresh epoch.
    // Do not keep every later pure-timer session on the full graph-scan path.
    if (r.entries.empty() && r.tasks.empty())
        possible_cycles.store(false, std::memory_order_relaxed);
    collecting = false;
}
}

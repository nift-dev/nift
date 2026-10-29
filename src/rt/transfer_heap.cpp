#include "rt/transfer_heap.h"
#include "rt/heap.h"
#include <algorithm>
namespace nift::rt {
namespace {
thread_local TransferHeapScope* current = nullptr;
template<class F> void children(const Value& v, F visit) {
    switch (v.kind()) {
        case Kind::Array: for (auto& x : v.heap_as<ArrayObject>()->items) visit(x); break;
        case Kind::Object: for (auto& x : v.heap_as<ObjectObject>()->entries) visit(x.second); break;
        case Kind::Struct: for (auto& x : v.heap_as<StructObject>()->fields) visit(x.second); break;
        case Kind::Function: for (auto& x : v.heap_as<FunctionObject>()->upvalues) visit(x); break;
        case Kind::Cell: visit(v.heap_as<CellObject>()->value); break;
        case Kind::Error: visit(v.heap_as<ErrorObject>()->cause); break;
        default: break;
    }
}
void cut(Value& v) noexcept {
    switch (v.kind()) {
        case Kind::Array: v.heap_as<ArrayObject>()->items.clear(); break;
        case Kind::Object: v.heap_as<ObjectObject>()->entries.clear(); break;
        case Kind::Struct: v.heap_as<StructObject>()->fields.clear(); break;
        case Kind::Function: v.heap_as<FunctionObject>()->upvalues.clear(); break;
        case Kind::Cell: v.heap_as<CellObject>()->value = Value(); break;
        case Kind::Error: v.heap_as<ErrorObject>()->cause = Value(); break;
        default: break;
    }
}
}
TransferHeapScope::TransferHeapScope() : previous_(current) { current = this; }
TransferHeapScope::~TransferHeapScope() {
    // execute_isolate_task declares this before its Program, registers, and
    // return Value. Nothing thread-affine can survive this boundary.
    for (auto& n : nodes_) cut(n.value);
    nodes_.clear();
    current = previous_;
}
void TransferHeapScope::retain_cycle(const std::vector<Value>& nodes) {
    if (!current) {
        thread_local TransferHeapScope root_heap; // embedding/session graph roots
        (void)root_heap;
    }
    current->collect();
    auto& owned = current->nodes_;
    owned.reserve(owned.size() + nodes.size()); // strong guarantee before pinning
    for (const auto& v : nodes) if (v.is_heap()) owned.push_back(Node{v, 0, false});
}
void collect_transfer_heap() noexcept { if (current) current->collect(); }
void TransferHeapScope::collect() noexcept {
    const auto find = [&](const Value& v) -> Node* {
        if (!v.is_heap()) return nullptr;
        for (auto& n : nodes_) if (n.value.heap() == v.heap()) return &n;
        return nullptr;
    };
    for (auto& n : nodes_) { n.incoming = 0; n.marked = false; }
    for (auto& n : nodes_) children(n.value, [&](auto& v) { if (auto* p = find(v)) ++p->incoming; });
    for (auto& n : nodes_) n.marked = n.value.heap()->refcount > n.incoming + 1;
    bool changed;
    do {
        changed = false;
        for (auto& n : nodes_) if (n.marked) children(n.value, [&](auto& v) {
            if (auto* p = find(v)) if (!p->marked) { p->marked = true; changed = true; }
        });
    } while (changed);
    for (auto& n : nodes_) if (!n.marked) cut(n.value);
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [](const Node& n) { return !n.marked; }), nodes_.end());
}
}

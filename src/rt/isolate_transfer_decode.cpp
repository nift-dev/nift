#include "rt/isolate_transfer_decode.h"
#include "rt/shared_state.h"
#include "rt/shared_gc.h"
#include "rt/transfer_heap.h"
#include "rt/aggregate.h"
#include "rt/future.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/isolate_transfer_support.h"
#include "rt/parallel_task.h"
#include "rt/string.h"
#include <utility>

namespace nift::rt {
using namespace transfer_detail;
class TransferGraph::Decoder {
public:
    Decoder(const std::vector<Node>& nodes, const TransferProtoMap& protos,
            const std::vector<std::shared_ptr<ParallelTaskRecord>>& tasks,
            const std::vector<std::shared_ptr<void>>& shared)
        : nodes_(nodes), protos_(protos), tasks_(tasks), shared_(shared),
          decoded_(nodes.size()), state_(nodes.size()) {}
    ~Decoder() {
        if (committed_) return;
        // All nodes remain anchored while partial edges are cut, even at OOM.
        for (auto& v : decoded_) switch (v.kind()) {
            case Kind::Array: v.heap_as<ArrayObject>()->items.clear(); break;
            case Kind::Object: v.heap_as<ObjectObject>()->entries.clear(); break;
            case Kind::Struct: v.heap_as<StructObject>()->fields.clear(); break;
            case Kind::Function: v.heap_as<FunctionObject>()->upvalues.clear(); break;
            case Kind::Cell: v.heap_as<CellObject>()->value = Value(); break;
            case Kind::Error: v.heap_as<ErrorObject>()->cause = Value(); break;
            default: break;
        }
    }
    TransferStatus decode(std::uint32_t root, Value& out, std::string& error) {
        if (root >= nodes_.size()) return fail(TransferStatus::InvalidGraph, "graph node is out of range", error);
        if (!validate(error)) return status_;
        // Allocate every skeleton before linking any edge: aliases and cycles
        // need no recursive C++ calls, and failures have one anchored cleanup.
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            if (!allocate(i, error)) return status_;
        for (std::size_t i = 0; i < nodes_.size(); ++i) link(i);
        if (cyclic_) TransferHeapScope::retain_cycle(decoded_);
        out = decoded_[root]; committed_ = true;
        return TransferStatus::Ok;
    }
private:
    TransferStatus fail(TransferStatus status, const char* message, std::string& error) {
        status_ = status; error = message; return status;
    }
    bool validate(std::string& error) {
        std::vector<std::pair<std::uint32_t, std::size_t>> work;
        for (std::uint32_t start = 0; start < nodes_.size(); ++start) {
            if (state_[start]) continue;
            state_[start] = 1; work.emplace_back(start, 0);
            while (!work.empty()) {
                auto& top = work.back(); const auto& n = nodes_[top.first];
                const auto count = n.children.size() + n.fields.size() + (n.has_child ? 1 : 0);
                if (top.second == count) { state_[top.first] = 2; work.pop_back(); continue; }
                const auto edge = top.second++;
                const auto child = edge < n.children.size() ? n.children[edge]
                    : edge < n.children.size() + n.fields.size() ? n.fields[edge - n.children.size()].second : n.child;
                if (child >= nodes_.size()) {
                    fail(TransferStatus::InvalidGraph, "graph node is out of range", error); return false;
                }
                if (state_[child] == 1) cyclic_ = true;
                if (!state_[child]) { state_[child] = 1; work.emplace_back(child, 0); }
            }
        }
        return true;
    }
    bool allocate(std::size_t id, std::string& error) {
        const auto& n = nodes_[id]; auto& value = decoded_[id];
        switch (n.kind) {
            case WireKind::Null: break;
            case WireKind::Bool: value = Value::boolean(n.boolean); break;
            case WireKind::Int: value = Value::integer(n.integer); break;
            case WireKind::Double: value = Value::number(n.number); break;
            case WireKind::Decimal: value = make_decimal(n.spelling, n.approx); break;
            case WireKind::String: value = make_string(n.text); break;
            case WireKind::Bytes: value = make_bytes(n.bytes); break;
            case WireKind::Array: value = Value::adopt(new ArrayObject()); break;
            case WireKind::Object: value = Value::adopt(new ObjectObject()); break;
            case WireKind::Struct: value = Value::adopt(new StructObject(atom(n.name))); break;
            case WireKind::Cell: {
                auto owner = std::make_unique<CellObject>(); owner->binding_type = n.binding_type;
                value = Value::adopt(owner.release()); break;
            }
            case WireKind::Error: {
                auto owner = std::make_unique<ErrorObject>();
                owner->message = n.message; owner->code = n.code; owner->category = n.category;
                owner->source = n.source; owner->line = n.line; owner->column = n.column;
                value = Value::adopt(owner.release()); break;
            }
            case WireKind::Function: {
                auto owner = std::make_unique<FunctionObject>();
                owner->name = n.name; owner->native_id = n.native_id;
                owner->is_lambda = n.is_lambda; owner->expression_body = n.expression_body;
                owner->selector_call_site = n.selector_call_site;
                if (n.has_proto) {
                    if (n.proto_domain != protos_.domain()) {
                        fail(TransferStatus::ProtoUnavailable, "function belongs to a different program domain", error);
                        return false;
                    }
                    owner->code = protos_.proto_of(n.proto_index);
                    if (!owner->code) {
                        fail(TransferStatus::ProtoUnavailable, "function prototype is not available", error);
                        return false;
                    }
                }
                value = Value::adopt(owner.release()); break;
            }
            case WireKind::SharedHandle:
                if (n.proto_index >= shared_.size()) {
                    fail(TransferStatus::InvalidGraph, "missing shared ownership", error); return false;
                }
                value = shared_handle_from_backing(static_cast<HandleKind>(n.handle_kind), n.handle_id,
                                                  shared_[n.proto_index]);
                break;
            case WireKind::TaskHandle: {
                std::shared_ptr<ParallelTaskRecord> record;
                for (auto& task : tasks_)
                    if (task && parallel_task_identity(task) == n.task_id) { record = task; break; }
                if (!record) {
                    fail(TransferStatus::RefusedFuture,
                         "cannot decode a transferred Future without its completion record", error); return false;
                }
                auto owner = std::make_unique<FutureObject>(); owner->thread = n.task_thread;
                owner->parallel = std::move(record); owner->collect_parallel = shared_collect;
                value = Value::adopt(owner.release()); break;
            }
        }
        return true;
    }
    void link(std::size_t id) {
        const auto& n = nodes_[id]; auto& v = decoded_[id];
        const auto child = [&](std::uint32_t index) -> const Value& {
            const auto& value = decoded_[index]; if (v.is_heap()) note_child(v.heap(), value); return value;
        };
        switch (n.kind) {
            case WireKind::Array:
                for (auto index : n.children) v.heap_as<ArrayObject>()->items.push_back(child(index)); break;
            case WireKind::Object:
                for (auto& field : n.fields)
                    v.heap_as<ObjectObject>()->entries.emplace_back(atom(field.first), child(field.second));
                rebuild_object_index(*v.heap_as<ObjectObject>()); break;
            case WireKind::Struct:
                for (auto& field : n.fields)
                    v.heap_as<StructObject>()->fields.emplace_back(atom(field.first), child(field.second)); break;
            case WireKind::Function:
                for (auto index : n.children) v.heap_as<FunctionObject>()->upvalues.push_back(child(index)); break;
            case WireKind::Cell: if (n.has_child) v.heap_as<CellObject>()->value = child(n.child); break;
            case WireKind::Error: if (n.has_child) v.heap_as<ErrorObject>()->cause = child(n.child); break;
            default: break;
        }
        if (v.is_heap()) heap_retally(v.heap());
    }
    const std::vector<Node>& nodes_;
    const TransferProtoMap& protos_;
    const std::vector<std::shared_ptr<ParallelTaskRecord>>& tasks_;
    const std::vector<std::shared_ptr<void>>& shared_;
    std::vector<Value> decoded_;
    std::vector<std::uint8_t> state_;
    bool committed_ = false, cyclic_ = false;
    TransferStatus status_ = TransferStatus::InvalidGraph;
};
TransferStatus TransferGraph::decode(const TransferProtoMap& protos, Value& root, std::string& error) const {
    error.clear(); root = Value();
    if (!has_root_) { error = "transfer graph has no root"; return TransferStatus::InvalidGraph; }
    Decoder decoder(nodes_, protos, tasks_, shared_);
    return decoder.decode(root_, root, error);
}
} // namespace nift::rt

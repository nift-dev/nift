#include "rt/isolate_transfer_encode.h"

#include "rt/aggregate.h"
#include "rt/future.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/isolate_transfer_support.h"
#include "rt/parallel_task.h"
#include "rt/shared_state.h"
#include "rt/string.h"

#include <unordered_map>
#include <utility>

namespace nift::rt {

class TransferGraph::Encoder {
public:
    Encoder(const TransferProtoMap& protos, std::vector<Node>& nodes,
            std::vector<std::shared_ptr<ParallelTaskRecord>>& tasks, std::vector<std::shared_ptr<void>>& shared)
        : protos_(protos), nodes_(nodes), tasks_(tasks), shared_(shared) {}
    TransferStatus encode(const Value& root, std::uint32_t& root_id, std::string& error) {
        TransferStatus status = add(root, root_id, error);
        while (status == TransferStatus::Ok && !pending_.empty()) {
            const auto item = pending_.back(); pending_.pop_back();
            status = add_children(*item.first, item.second, error);
        }
        return status;
    }
private:
    TransferStatus fail(TransferStatus status, std::string message, std::string& error) {
        error = std::move(message);
        return status;
    }
    TransferStatus add(const Value& value, std::uint32_t& id, std::string& error) {
        if (value.is(Kind::Future)) return add_future(value, id, error);
        if (value.is(Kind::Handle)) {
            if (!is_shared_handle(value))
                return fail(TransferStatus::RefusedHandle,
                            std::string("non-transferable resource: cannot transfer a ") +
                                handle_kind_text(value.handle_kind()) + " handle between isolates",
                            error);
            auto backing = shared_handle_backing(value);
            if (!backing) return fail(TransferStatus::RefusedHandle, "expired shared handle", error);
            Node node;
            node.proto_index = static_cast<std::uint32_t>(shared_.size());
            shared_.push_back(std::move(backing));
            shared_note_edges();
            node.kind = WireKind::SharedHandle;
            node.handle_kind = static_cast<std::uint8_t>(value.handle_kind());
            node.handle_id = value.handle_id();
            node.handle_generation = value.handle_generation();
            id = static_cast<std::uint32_t>(nodes_.size());
            nodes_.push_back(std::move(node));
            return TransferStatus::Ok;
        }
        if (!value.is_heap()) return add_inline(value, id, error);
        const HeapObject* object = value.heap();
        const auto found = seen_.find(object);
        if (found != seen_.end()) { id = found->second; return TransferStatus::Ok; }
        Node node;
        switch (value.kind()) {
            case Kind::Decimal:
                node.kind = WireKind::Decimal;
                node.spelling = std::string(decimal_spelling(value));
                node.approx = decimal_approx(value);
                break;
            case Kind::String:
                node.kind = WireKind::String;
                node.text = std::string(string_view_of(value));
                break;
            case Kind::Bytes:
                node.kind = WireKind::Bytes;
                node.bytes = bytes_of(value);
                break;
            case Kind::Array: node.kind = WireKind::Array; break;
            case Kind::Object: node.kind = WireKind::Object; break;
            case Kind::Struct:
                node.kind = WireKind::Struct;
                node.name = std::string(atom_text(struct_type(value)));
                break;
            case Kind::Error: {
                const auto* error_value = value.heap_as<ErrorObject>();
                node.kind = WireKind::Error;
                node.message = error_value->message;
                node.code = error_value->code;
                node.category = error_value->category;
                node.source = error_value->source;
                node.line = error_value->line;
                node.column = error_value->column;
                break;
            }
            case Kind::Cell: {
                const auto* cell = value.heap_as<CellObject>();
                node.kind = WireKind::Cell;
                node.binding_type = cell->binding_type;
                break;
            }
            case Kind::Function: {
                const auto* function = value.heap_as<FunctionObject>();
                node.kind = WireKind::Function;
                node.proto_domain = protos_.domain();
                node.name = function->name;
                node.native_id = function->native_id;
                node.is_lambda = function->is_lambda;
                node.expression_body = function->expression_body;
                node.selector_call_site = function->selector_call_site;
                if (function->code != nullptr) {
                    if (!protos_.index_of(static_cast<const Proto*>(function->code), node.proto_index))
                        return fail(TransferStatus::ProtoUnavailable,
                                    "function prototype is not in the transfer map", error);
                    node.has_proto = true;
                }
                break;
            }
            default:
                return fail(TransferStatus::InvalidGraph, "unsupported heap value during transfer", error);
        }
        id = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(std::move(node));
        seen_.emplace(object, id);
        pending_.emplace_back(&value, id);
        return TransferStatus::Ok;
    }
    static const char* handle_kind_text(HandleKind kind) {
        switch (kind) {
            case HandleKind::File: return "file";
            case HandleKind::Stream: return "stream";
            case HandleKind::Process: return "process";
            case HandleKind::Command: return "process";
            case HandleKind::Timer: return "timer";
            case HandleKind::Module: return "module";
            case HandleKind::Ffi: return "ffi";
            case HandleKind::Custom: return "custom";
            case HandleKind::Mutex: return "mutex";
            case HandleKind::Atomic: return "atomic";
            case HandleKind::Channel: return "channel";
            case HandleKind::Future: return "future";
            case HandleKind::None: break;
        }
        return "managed";
    }
    TransferStatus add_future(const Value& value, std::uint32_t& id, std::string& error) {
        const auto* future = value.heap_as<FutureObject>();
        if (future == nullptr || !future->parallel)
            return fail(TransferStatus::RefusedFuture,
                        "cannot transfer a non-parallel Future between isolates", error);
        const HeapObject* object = value.heap();
        const auto found = seen_.find(object);
        if (found != seen_.end()) { id = found->second; return TransferStatus::Ok; }
        const std::uint64_t task_id = parallel_task_identity(future->parallel);
        bool known = false;
        for (const auto& task : tasks_)
            if (task.get() == future->parallel.get()) {
                known = true;
                break;
            }
        if (!known) tasks_.push_back(future->parallel);
        shared_note_edges();
        Node node;
        node.kind = WireKind::TaskHandle;
        node.task_id = task_id;
        node.task_thread = future->thread;
        id = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(std::move(node));
        seen_.emplace(object, id);
        return TransferStatus::Ok;
    }
    TransferStatus add_inline(const Value& value, std::uint32_t& id, std::string& error) {
        Node node;
        switch (value.kind()) {
            case Kind::Null: node.kind = WireKind::Null; break;
            case Kind::Bool: node.kind = WireKind::Bool; node.boolean = value.as_bool(); break;
            case Kind::Int: node.kind = WireKind::Int; node.integer = value.as_int(); break;
            case Kind::Double: node.kind = WireKind::Double; node.number = value.as_double(); break;
            case Kind::String: node.kind = WireKind::String; node.text = std::string(value.inline_view()); break;
            default:
                return fail(TransferStatus::InvalidGraph, "unsupported immediate value during transfer", error);
        }
        id = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(std::move(node));
        return TransferStatus::Ok;
    }
    TransferStatus add_children(const Value& value, std::uint32_t id, std::string& error) {
        switch (value.kind()) {
            case Kind::Array:
                for (const Value& child : array_items(value)) {
                    std::uint32_t child_id = 0;
                    TransferStatus status = add(child, child_id, error);
                    if (status != TransferStatus::Ok) return status;
                    nodes_[id].children.push_back(child_id);
                }
                return TransferStatus::Ok;
            case Kind::Object:
                for (const auto& entry : object_entries(value)) {
                    std::uint32_t child_id = 0;
                    TransferStatus status = add(entry.second, child_id, error);
                    if (status != TransferStatus::Ok) return status;
                    nodes_[id].fields.emplace_back(std::string(atom_text(entry.first)), child_id);
                }
                return TransferStatus::Ok;
            case Kind::Struct:
                for (const auto& entry : struct_fields(value)) {
                    std::uint32_t child_id = 0;
                    TransferStatus status = add(entry.second, child_id, error);
                    if (status != TransferStatus::Ok) return status;
                    nodes_[id].fields.emplace_back(std::string(atom_text(entry.first)), child_id);
                }
                return TransferStatus::Ok;
            case Kind::Error: {
                const Value& cause = value.heap_as<ErrorObject>()->cause;
                if (cause.is_null()) return TransferStatus::Ok;
                std::uint32_t child_id = 0;
                TransferStatus status = add(cause, child_id, error);
                if (status != TransferStatus::Ok) return status;
                nodes_[id].has_child = true;
                nodes_[id].child = child_id;
                return TransferStatus::Ok;
            }
            case Kind::Cell: {
                std::uint32_t child_id = 0;
                TransferStatus status = add(value.heap_as<CellObject>()->value, child_id, error);
                if (status != TransferStatus::Ok) return status;
                nodes_[id].has_child = true;
                nodes_[id].child = child_id;
                return TransferStatus::Ok;
            }
            case Kind::Function:
                for (const Value& upvalue : value.heap_as<FunctionObject>()->upvalues) {
                    std::uint32_t child_id = 0;
                    TransferStatus status = add(upvalue, child_id, error);
                    if (status != TransferStatus::Ok) return status;
                    nodes_[id].children.push_back(child_id);
                }
                return TransferStatus::Ok;
            default: return TransferStatus::Ok;
        }
    }
    const TransferProtoMap& protos_;
    std::vector<Node>& nodes_;
    std::vector<std::shared_ptr<ParallelTaskRecord>>& tasks_;
    std::vector<std::shared_ptr<void>>& shared_;
    std::unordered_map<const HeapObject*, std::uint32_t> seen_;
    std::vector<std::pair<const Value*, std::uint32_t>> pending_;
};

TransferStatus TransferGraph::encode(const Value& root, const TransferProtoMap& protos, std::string& error) {
    clear();
    error.clear();
    Encoder encoder(protos, nodes_, tasks_, shared_);
    std::uint32_t root_id = 0;
    const TransferStatus status = encoder.encode(root, root_id, error);
    if (status != TransferStatus::Ok) { clear(); return status; }
    root_ = root_id;
    has_root_ = true;
    return status;
}

} // namespace nift::rt

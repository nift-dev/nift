#include "rt/isolate_transfer_decode.h"

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

using namespace transfer_detail;

class TransferGraph::Decoder {
public:
    Decoder(const std::vector<Node>& nodes, const TransferProtoMap& protos,
            const std::vector<std::shared_ptr<ParallelTaskRecord>>& tasks)
        : nodes_(nodes), protos_(protos), tasks_(tasks), decoded_(nodes.size()), state_(nodes.size(), 0) {}
    TransferStatus decode(std::uint32_t root, Value& out, std::string& error) {
        Value value;
        if (!decode_node(root, value, error)) return status_;
        out = std::move(value);
        return TransferStatus::Ok;
    }
private:
    bool fail(TransferStatus status, std::string message, std::string& error) {
        status_ = status;
        error = std::move(message);
        return false;
    }
    bool decode_node(std::uint32_t id, Value& out, std::string& error) {
        if (id >= nodes_.size()) return fail(TransferStatus::InvalidGraph, "graph node is out of range", error);
        if (state_[id] != 0) { out = decoded_[id]; return true; }
        const Node& node = nodes_[id];
        switch (node.kind) {
            case WireKind::Null: return done(id, Value(), out);
            case WireKind::Bool: return done(id, Value::boolean(node.boolean), out);
            case WireKind::Int: return done(id, Value::integer(node.integer), out);
            case WireKind::Double: return done(id, Value::number(node.number), out);
            case WireKind::Decimal: return done(id, make_decimal(node.spelling, node.approx), out);
            case WireKind::String: return done(id, make_string(node.text), out);
            case WireKind::Bytes: return done(id, make_bytes(node.bytes), out);
            case WireKind::Array: return decode_array(id, node, out, error);
            case WireKind::Object: return decode_object(id, node, out, error);
            case WireKind::Struct: return decode_struct(id, node, out, error);
            case WireKind::Error: return decode_error(id, node, out, error);
            case WireKind::Cell: return decode_cell(id, node, out, error);
            case WireKind::Function: return decode_function(id, node, out, error);
            case WireKind::SharedHandle:
                return done(id,
                            Value::handle(static_cast<HandleKind>(node.handle_kind), node.handle_id,
                                          node.handle_generation),
                            out);
            case WireKind::TaskHandle: return decode_task(id, node, out, error);
        }
        return fail(TransferStatus::InvalidGraph, "unknown graph node kind", error);
    }
    bool done(std::uint32_t id, Value value, Value& out) {
        decoded_[id] = value;
        state_[id] = 2;
        out = std::move(value);
        return true;
    }
    bool decode_task(std::uint32_t id, const Node& node, Value& out, std::string& error) {
        std::shared_ptr<ParallelTaskRecord> record;
        for (const auto& task : tasks_)
            if (task && parallel_task_identity(task) == node.task_id) {
                record = task;
                break;
            }
        if (!record)
            return fail(TransferStatus::RefusedFuture,
                        "cannot decode a transferred Future without its completion record", error);
        auto* future = new FutureObject();
        future->thread = node.task_thread;
        future->parallel = std::move(record);
        Value value = Value::adopt(future);
        heap_retally(future);
        return done(id, std::move(value), out);
    }
    bool decode_array(std::uint32_t id, const Node& node, Value& out, std::string& error) {
        auto* object = new ArrayObject();
        Value value = Value::adopt(object);
        decoded_[id] = value;
        state_[id] = 1;
        for (std::uint32_t child_id : node.children) {
            Value child;
            if (!decode_node(child_id, child, error)) return false;
            note_child(object, child);
            object->items.push_back(std::move(child));
        }
        heap_retally(object);
        state_[id] = 2;
        out = std::move(value);
        return true;
    }
    bool decode_object(std::uint32_t id, const Node& node, Value& out, std::string& error) {
        auto* object = new ObjectObject();
        Value value = Value::adopt(object);
        decoded_[id] = value;
        state_[id] = 1;
        for (const auto& field : node.fields) {
            Value child;
            if (!decode_node(field.second, child, error)) return false;
            note_child(object, child);
            object->entries.emplace_back(atom(field.first), std::move(child));
        }
        rebuild_object_index(*object);
        heap_retally(object);
        state_[id] = 2;
        out = std::move(value);
        return true;
    }
    bool decode_struct(std::uint32_t id, const Node& node, Value& out, std::string& error) {
        auto* object = new StructObject(atom(node.name));
        Value value = Value::adopt(object);
        decoded_[id] = value;
        state_[id] = 1;
        for (const auto& field : node.fields) {
            Value child;
            if (!decode_node(field.second, child, error)) return false;
            note_child(object, child);
            object->fields.emplace_back(atom(field.first), std::move(child));
        }
        heap_retally(object);
        state_[id] = 2;
        out = std::move(value);
        return true;
    }
    bool decode_error(std::uint32_t id, const Node& node, Value& out, std::string& error) {
        auto* object = new ErrorObject();
        object->message = node.message;
        object->code = node.code;
        object->category = node.category;
        object->source = node.source;
        object->line = static_cast<std::size_t>(node.line);
        object->column = static_cast<std::size_t>(node.column);
        Value value = Value::adopt(object);
        decoded_[id] = value;
        state_[id] = 1;
        if (node.has_child) {
            Value cause;
            if (!decode_node(node.child, cause, error)) return false;
            note_child(object, cause);
            object->cause = std::move(cause);
        }
        heap_retally(object);
        state_[id] = 2;
        out = std::move(value);
        return true;
    }
    bool decode_cell(std::uint32_t id, const Node& node, Value& out, std::string& error) {
        auto* cell = new CellObject();
        cell->binding_type = node.binding_type;
        Value value = Value::adopt(cell);
        decoded_[id] = value;
        state_[id] = 1;
        if (node.has_child) {
            Value child;
            if (!decode_node(node.child, child, error)) return false;
            note_child(cell, child);
            cell->value = std::move(child);
        }
        heap_retally(cell);
        state_[id] = 2;
        out = std::move(value);
        return true;
    }
    bool decode_function(std::uint32_t id, const Node& node, Value& out, std::string& error) {
        auto* function = new FunctionObject();
        function->name = node.name;
        function->native_id = node.native_id;
        function->is_lambda = node.is_lambda;
        function->expression_body = node.expression_body;
        function->selector_call_site = node.selector_call_site;
        if (node.has_proto) {
            const Proto* proto = protos_.proto_of(node.proto_index);
            if (proto == nullptr)
                return fail(TransferStatus::ProtoUnavailable, "function prototype is not available", error);
            function->code = proto;
        }
        Value value = Value::adopt(function);
        decoded_[id] = value;
        state_[id] = 1;
        for (std::uint32_t child_id : node.children) {
            Value child;
            if (!decode_node(child_id, child, error)) return false;
            note_child(function, child);
            function->upvalues.push_back(std::move(child));
        }
        heap_retally(function);
        state_[id] = 2;
        out = std::move(value);
        return true;
    }
    const std::vector<Node>& nodes_;
    const TransferProtoMap& protos_;
    const std::vector<std::shared_ptr<ParallelTaskRecord>>& tasks_;
    std::vector<Value> decoded_;
    std::vector<std::uint8_t> state_;
    TransferStatus status_ = TransferStatus::InvalidGraph;
};

TransferStatus TransferGraph::decode(const TransferProtoMap& protos, Value& root, std::string& error) const {
    error.clear();
    root = Value();
    if (!has_root_) {
        error = "transfer graph has no root";
        return TransferStatus::InvalidGraph;
    }
    Decoder decoder(nodes_, protos, tasks_);
    Value value;
    const TransferStatus status = decoder.decode(root_, value, error);
    if (status == TransferStatus::Ok) root = std::move(value);
    return status;
}

} // namespace nift::rt

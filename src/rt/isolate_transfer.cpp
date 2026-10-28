#include "rt/isolate_transfer.h"

#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/string.h"

#include <unordered_map>
#include <utility>

namespace nift::rt {

namespace {

std::size_t object_slot(Atom key, std::size_t mask) {
    return (key.id * 2654435761u) & mask;
}
void rebuild_object_index(ObjectObject& object) {
    if (object.entries.size() < ObjectObject::kIndexThreshold) { object.index.clear(); return; }
    std::size_t capacity = 16;
    while (capacity < object.entries.size() * 2) capacity <<= 1;
    object.index.assign(capacity, 0);
    const std::size_t mask = capacity - 1;
    for (std::size_t i = 0; i < object.entries.size(); ++i) {
        std::size_t slot = object_slot(object.entries[i].first, mask);
        while (object.index[slot] != 0) slot = (slot + 1) & mask;
        object.index[slot] = static_cast<std::uint32_t>(i + 1);
    }
}
void note_child(HeapObject* parent, const Value& child) {
    if (child.kind() >= Kind::Array) mark_may_hold_identity(parent);
}
} // namespace

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

class TransferGraph::Encoder {
public:
    Encoder(const TransferProtoMap& protos, std::vector<Node>& nodes) : protos_(protos), nodes_(nodes) {}
    TransferStatus encode(const Value& root, std::uint32_t& root_id, std::string& error) {
        return add(root, root_id, error);
    }
private:
    TransferStatus fail(TransferStatus status, std::string message, std::string& error) {
        error = std::move(message);
        return status;
    }
    TransferStatus add(const Value& value, std::uint32_t& id, std::string& error) {
        if (value.is(Kind::Future))
            return fail(TransferStatus::RefusedFuture, "cannot transfer a Future between isolates", error);
        if (value.is(Kind::Handle))
            return fail(TransferStatus::RefusedHandle, "cannot transfer a Handle between isolates", error);
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
        return add_children(value, id, error);
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
    std::unordered_map<const HeapObject*, std::uint32_t> seen_;
};

class TransferGraph::Decoder {
public:
    Decoder(const std::vector<Node>& nodes, const TransferProtoMap& protos)
        : nodes_(nodes), protos_(protos), decoded_(nodes.size()), state_(nodes.size(), 0) {}
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
        }
        return fail(TransferStatus::InvalidGraph, "unknown graph node kind", error);
    }
    bool done(std::uint32_t id, Value value, Value& out) {
        decoded_[id] = value;
        state_[id] = 2;
        out = std::move(value);
        return true;
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
    std::vector<Value> decoded_;
    std::vector<std::uint8_t> state_;
    TransferStatus status_ = TransferStatus::InvalidGraph;
};

TransferStatus TransferGraph::encode(const Value& root, const TransferProtoMap& protos, std::string& error) {
    clear();
    error.clear();
    Encoder encoder(protos, nodes_);
    std::uint32_t root_id = 0;
    const TransferStatus status = encoder.encode(root, root_id, error);
    if (status != TransferStatus::Ok) { clear(); return status; }
    root_ = root_id;
    has_root_ = true;
    return status;
}

TransferStatus TransferGraph::decode(const TransferProtoMap& protos, Value& root, std::string& error) const {
    error.clear();
    root = Value();
    if (!has_root_) {
        error = "transfer graph has no root";
        return TransferStatus::InvalidGraph;
    }
    Decoder decoder(nodes_, protos);
    Value value;
    const TransferStatus status = decoder.decode(root_, value, error);
    if (status == TransferStatus::Ok) root = std::move(value);
    return status;
}

void TransferGraph::clear() noexcept {
    nodes_.clear();
    root_ = 0;
    has_root_ = false;
}

} // namespace nift::rt

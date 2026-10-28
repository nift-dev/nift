// CP23 unit tests for the thread-neutral isolate transfer graph.
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/compare.h"
#include "rt/error.h"
#include "rt/future.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/isolate_transfer.h"
#include "rt/string.h"
#include "rt_test.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using namespace nift::rt;

namespace {

Value I(std::int64_t i) { return Value::integer(i); }
Value S(const char* s) { return make_string(s); }

struct ProtoMap : TransferProtoMap {
    std::vector<const Proto*> by_index;
    std::unordered_map<const Proto*, std::uint32_t> by_pointer;

    std::uint32_t add(const Proto* proto) {
        const std::uint32_t index = static_cast<std::uint32_t>(by_index.size());
        by_index.push_back(proto);
        by_pointer.emplace(proto, index);
        return index;
    }
    bool index_of(const Proto* proto, std::uint32_t& index) const override {
        const auto found = by_pointer.find(proto);
        if (found == by_pointer.end()) return false;
        index = found->second;
        return true;
    }
    const Proto* proto_of(std::uint32_t index) const override {
        return index < by_index.size() ? by_index[index] : nullptr;
    }
};

bool roundtrip(const Value& source, const ProtoMap& protos, TransferGraph& graph, Value& decoded,
               std::string& error) {
    if (graph.encode(source, protos, error) != TransferStatus::Ok) return false;
    return graph.decode(protos, decoded, error) == TransferStatus::Ok;
}

void scalars() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    CHECK(roundtrip(Value(), protos, graph, decoded, error) && decoded.is_null());
    CHECK(roundtrip(Value::boolean(true), protos, graph, decoded, error) && decoded.as_bool());
    CHECK(roundtrip(Value::boolean(false), protos, graph, decoded, error) && !decoded.as_bool());
    CHECK(roundtrip(I(-42), protos, graph, decoded, error) && decoded.as_int() == -42);
    CHECK(roundtrip(Value::number(3.25), protos, graph, decoded, error) && decoded.as_double() == 3.25);
    Value decimal = make_decimal("12345678901234567890.5", 1.2345678901234567e19);
    CHECK(roundtrip(decimal, protos, graph, decoded, error));
    CHECK(decoded.is(Kind::Decimal) && decimal_spelling(decoded) == "12345678901234567890.5");
    CHECK(graph.node_count() == 1 && !graph.empty());
}

void strings_and_bytes() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    Value inline_text = S("short text");
    CHECK(roundtrip(inline_text, protos, graph, decoded, error));
    CHECK(decoded.inline_string() && string_view_of(decoded) == "short text");

    Value heap_text = S("this text is longer than fourteen bytes");
    CHECK(roundtrip(heap_text, protos, graph, decoded, error));
    CHECK(!decoded.inline_string() && string_view_of(decoded).size() == string_view_of(heap_text).size());
    CHECK(equal(heap_text, decoded));

    const std::string embedded("a\0b\0c", 5);
    CHECK(roundtrip(make_string(embedded), protos, graph, decoded, error));
    CHECK(string_view_of(decoded).size() == 5 && string_view_of(decoded) == embedded);

    Value bytes = make_bytes({0, 1, 127, 255});
    CHECK(roundtrip(bytes, protos, graph, decoded, error));
    CHECK(decoded.is(Kind::Bytes) && bytes_of(decoded) == bytes_of(bytes));

    Value shared = S("a heap string shared by two array slots");
    Value array = make_array();
    array_push(array, shared);
    array_push(array, shared);
    CHECK(roundtrip(array, protos, graph, decoded, error));
    CHECK(graph.node_count() == 2);
    CHECK(array_items(decoded)[0].heap() == array_items(decoded)[1].heap());
}

void arrays_objects_and_aliases() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    Value shared = make_array();
    array_push(shared, I(1));
    array_push(shared, I(2));
    Value object = make_object();
    object_set(object, atom("second"), shared);
    object_set(object, atom("first"), shared);
    object_set(object, atom("text"), S("value"));
    Value root = make_array();
    array_push(root, object);
    array_push(root, shared);

    CHECK(roundtrip(root, protos, graph, decoded, error));
    CHECK(graph.node_count() == 6);
    CHECK(array_size(decoded) == 2);
    const Value& decoded_object = array_items(decoded)[0];
    const Value* second = object_get(decoded_object, atom("second"));
    const Value* first = object_get(decoded_object, atom("first"));
    CHECK(second != nullptr && first != nullptr);
    CHECK(second->heap() == first->heap());
    CHECK(second->heap() == array_items(decoded)[1].heap());
    CHECK(second->heap() != decoded_object.heap());
    CHECK(object_get(decoded_object, atom("text")) != nullptr);
    CHECK(equal(*object_get(decoded_object, atom("text")), S("value")));
}

void cycles() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    Value self = make_array();
    array_push(self, I(7));
    self.heap_as<ArrayObject>()->items.push_back(self);
    mark_may_hold_identity(self.heap());
    heap_retally(self.heap());
    CHECK(roundtrip(self, protos, graph, decoded, error));
    CHECK(array_size(decoded) == 2);
    CHECK(array_items(decoded)[1].heap() == decoded.heap());

    auto* decoded_object = decoded.heap_as<ArrayObject>();
    decoded_object->items.clear();
    heap_retally(decoded_object);

    Value outer = make_array();
    Value inner = make_array();
    array_push(outer, inner);
    inner.heap_as<ArrayObject>()->items.push_back(outer);
    mark_may_hold_identity(inner.heap());
    heap_retally(inner.heap());
    CHECK(roundtrip(outer, protos, graph, decoded, error));
    const Value& back = array_items(array_items(decoded)[0])[0];
    CHECK(back.heap() == decoded.heap());
    auto* break_outer = decoded.heap_as<ArrayObject>();
    auto* break_inner = break_outer->items[0].heap_as<ArrayObject>();
    break_inner->items.clear();
    heap_retally(break_inner);
    break_outer->items.clear();
    heap_retally(break_outer);
}

void structs_and_errors() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    Value point = make_struct(atom("Point"), 2);
    struct_add_field(point, atom("x"), I(10));
    struct_add_field(point, atom("y"), S("twenty"));
    CHECK(roundtrip(point, protos, graph, decoded, error));
    CHECK(decoded.is(Kind::Struct) && atom_text(struct_type(decoded)) == "Point");
    CHECK(struct_fields(decoded).size() == 2);
    CHECK(atom_text(struct_fields(decoded)[0].first) == "x");
    CHECK(struct_fields(decoded)[0].second.as_int() == 10);
    CHECK(atom_text(struct_fields(decoded)[1].first) == "y");
    CHECK(string_view_of(struct_fields(decoded)[1].second) == "twenty");

    Value cause;
    CHECK(make_error("inner", "user.inner", Value(), "cause.nift", 2, 3, cause, error));
    Value outer;
    CHECK(make_error("outer", "user.outer", cause, "outer.nift", 8, 9, outer, error));
    CHECK(roundtrip(outer, protos, graph, decoded, error));
    const auto* decoded_error = decoded.heap_as<ErrorObject>();
    CHECK(decoded_error->message == "outer" && decoded_error->code == "user.outer");
    CHECK(decoded_error->category == "user" && decoded_error->source == "outer.nift");
    CHECK(decoded_error->line == 8 && decoded_error->column == 9);
    CHECK(decoded_error->cause.is(Kind::Error));
    CHECK(decoded_error->cause.heap_as<ErrorObject>()->message == "inner");
}

void cells_and_functions() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    Value cell = make_cell(S("captured"), 5);
    CHECK(roundtrip(cell, protos, graph, decoded, error));
    CHECK(decoded.is(Kind::Cell) && cell_binding_type(*cell_of(decoded)) == 5);
    CHECK(string_view_of(cell_load(decoded)) == "captured");

    Proto unused;
    Proto target;
    target.name = "work";
    const std::uint32_t target_index = protos.add(&target);
    protos.add(&unused);

    auto* function = new FunctionObject();
    function->name = "work";
    function->code = &target;
    function->native_id = 77;
    function->is_lambda = true;
    function->expression_body = true;
    function->selector_call_site = true;
    function->upvalues.push_back(make_cell(I(9), 2));
    heap_retally(function);
    Value source = Value::adopt(function);

    CHECK(roundtrip(source, protos, graph, decoded, error));
    const auto* decoded_function = decoded.heap_as<FunctionObject>();
    CHECK(decoded_function->name == "work" && decoded_function->native_id == 77);
    CHECK(decoded_function->code == &target);
    CHECK(decoded_function->is_lambda && decoded_function->expression_body);
    CHECK(decoded_function->selector_call_site);
    CHECK(decoded_function->upvalues.size() == 1);
    CHECK(cell_load(decoded_function->upvalues[0]).as_int() == 9);
    CHECK(target_index == 0);

    ProtoMap empty;
    Value not_decoded;
    CHECK(graph.decode(empty, not_decoded, error) == TransferStatus::ProtoUnavailable);
    CHECK(not_decoded.is_null() && !error.empty());

    auto* native = new FunctionObject();
    native->name = "native function";
    native->native_id = 3;
    heap_retally(native);
    CHECK(roundtrip(Value::adopt(native), protos, graph, decoded, error));
    CHECK(decoded.heap_as<FunctionObject>()->code == nullptr);
    CHECK(decoded.heap_as<FunctionObject>()->native_id == 3);
}

void refusals() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    Value future = Value::adopt(new FutureObject());
    CHECK(graph.encode(future, protos, error) == TransferStatus::RefusedFuture);
    CHECK(!error.empty() && graph.empty());
    CHECK(error.find("Future") != std::string::npos);

    Value handle = Value::handle(HandleKind::File, 7, 2);
    CHECK(graph.encode(handle, protos, error) == TransferStatus::RefusedHandle);
    CHECK(!error.empty() && graph.empty());
    CHECK(error.find("Handle") != std::string::npos);

    auto* box = new HandleBoxObject();
    box->id = 11;
    box->generation = 1;
    box->handle_kind = HandleKind::Stream;
    Value boxed = Value::adopt_handle_box(box);
    CHECK(graph.encode(boxed, protos, error) == TransferStatus::RefusedHandle);
    CHECK(!error.empty() && graph.empty());

    Value nested = make_array();
    array_push(nested, I(1));
    array_push(nested, Value::adopt(new FutureObject()));
    CHECK(graph.encode(nested, protos, error) == TransferStatus::RefusedFuture);
    CHECK(!error.empty() && graph.empty());
    CHECK(graph.decode(protos, decoded, error) == TransferStatus::InvalidGraph);
}

void equality_and_length() {
    ProtoMap protos;
    TransferGraph graph;
    std::string error;
    Value decoded;

    const char* title_text = "a transfer graph keeps text and length";
    Value root = make_object();
    object_set(root, atom("title"), S(title_text));
    Value list = make_array();
    array_push(list, I(1));
    array_push(list, Value::number(2.5));
    array_push(list, S("third"));
    object_set(root, atom("list"), list);

    CHECK(roundtrip(root, protos, graph, decoded, error));
    CHECK(graph.node_count() == 6);
    CHECK(equal(root, decoded));
    const Value* title = object_get(decoded, atom("title"));
    CHECK(title != nullptr && string_view_of(*title).size() == std::string(title_text).size());
    CHECK(array_size(*object_get(decoded, atom("list"))) == 3);
}

} // namespace

int main() {
    scalars();
    strings_and_bytes();
    arrays_objects_and_aliases();
    cycles();
    structs_and_errors();
    cells_and_functions();
    refusals();
    equality_and_length();
    return rt_test::finish("isolate-transfer-unit");
}

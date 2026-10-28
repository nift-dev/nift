// CP23 milestone 2: isolate task entry/result API. A synthetic wrapper runs a
// cloned Program on a fresh Vm; arguments, globals and the result cross by
// TransferGraph, and every constant is rebuilt in the isolate heap.
#include "rt/codegen.h"
#include "rt/future.h"
#include "rt/isolate_task.h"
#include "rt/string.h"
#include "rt_test.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace nift::rt;

namespace {

constexpr const char* kGlobalText = "global text longer than fourteen bytes";
constexpr const char* kLongConstant = "constant text longer than fourteen bytes";

Value I(std::int64_t value) { return Value::integer(value); }
Value S(const char* text) { return make_string(text); }

struct ProtoMap : TransferProtoMap {
    std::vector<const Proto*> by_index;
    std::unordered_map<const Proto*, std::uint32_t> by_pointer;

    void add_all(const Program& program) {
        for (const auto& proto : program.protos) {
            by_pointer.emplace(proto.get(), static_cast<std::uint32_t>(by_index.size()));
            by_index.push_back(proto.get());
        }
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

struct Built {
    std::unique_ptr<Program> program;
    std::uint32_t entry = 0;
};

// main has three globals; inner adds; entry returns
// [arg0, arg1, G0, G1, G2, long constant, inner(arg0, arg1)].
Built task_program() {
    Built built;
    auto program = std::make_unique<Program>();

    FunctionBuilder main("");
    std::uint16_t m0 = 0, m1 = 0, m2 = 0;
    main.alloc(m0);
    main.alloc(m1);
    main.alloc(m2);
    main.emit(OpCode::Halt);
    program->protos.push_back(main.finish());

    FunctionBuilder inner("inner");
    std::uint16_t ia = 0, ib = 0, ir = 0;
    inner.alloc(ia);
    inner.alloc(ib);
    inner.alloc(ir);
    inner.set_params(2);
    inner.emit(OpCode::Add, ir, ia, ib);
    inner.emit(OpCode::Return, ir);
    program->protos.push_back(inner.finish());

    FunctionBuilder entry("entry");
    std::uint16_t a = 0, b = 0, ar0 = 0, ar1 = 0;
    std::uint16_t gscalar = 0, gtext = 0, garray = 0, longk = 0;
    std::uint16_t call0 = 0, call1 = 0, result = 0;
    entry.alloc(a);
    entry.alloc(b);
    entry.alloc(ar0);
    entry.alloc(ar1);
    entry.alloc(gscalar);
    entry.alloc(gtext);
    entry.alloc(garray);
    entry.alloc(longk);
    entry.alloc(call0);
    entry.alloc(call1);
    entry.alloc(result);
    entry.set_params(2);
    entry.emit(OpCode::Move, ar0, a);
    entry.emit(OpCode::Move, ar1, b);
    entry.emit(OpCode::GetGlobal, gscalar, 0);
    entry.emit(OpCode::GetGlobal, gtext, 1);
    entry.emit(OpCode::GetGlobal, garray, 2);
    std::uint16_t constant = 0;
    entry.constant(S(kLongConstant), constant);
    entry.emit(OpCode::LoadK, longk, static_cast<std::uint16_t>(constant & ~kConstantBit));
    entry.emit(OpCode::Move, call0, a);
    entry.emit(OpCode::Move, call1, b);
    entry.emit(OpCode::Call, call0, 1, 2);
    entry.emit(OpCode::NewArray, result, ar0, 7);
    entry.emit(OpCode::TopReturn, result);
    program->protos.push_back(entry.finish());

    built.entry = static_cast<std::uint32_t>(program->protos.size() - 1);
    built.program = std::move(program);
    return built;
}

Value global_array() {
    Value array = make_array();
    array_push(array, I(1));
    array_push(array, I(2));
    array_push(array, I(3));
    return array;
}

IsolateTaskInput task_input(const ProtoMap& map, std::uint32_t entry) {
    IsolateTaskInput input;
    input.entry_proto = entry;
    input.argc = 2;
    input.args = {I(10), I(32)};
    input.globals = {I(7), S(kGlobalText), global_array()};
    input.global_ready = {1, 1, 1};
    input.proto_map = &map;
    return input;
}

void check_entry_and_result() {
    Built built = task_program();
    ProtoMap map;
    map.add_all(*built.program);
    const IsolateTaskInput input = task_input(map, built.entry);
    IsolateTaskOutcome outcome = execute_isolate_task(*built.program, input);
    if (outcome.status != IsolateTaskStatus::Ok)
        std::fprintf(stderr, "entry: status=%s error=[%s] stop=%d message=[%s]\n",
                     isolate_task_status_name(outcome.status), outcome.error.c_str(),
                     static_cast<int>(outcome.result.reason), outcome.result.message.c_str());
    CHECK(outcome.status == IsolateTaskStatus::Ok);
    CHECK(outcome.error.empty());
    CHECK(outcome.result.reason == StopReason::None);
    CHECK(outcome.result.returned);
    CHECK(outcome.has_return_value);
    CHECK(!outcome.return_value.empty());
    CHECK(outcome.output.empty());

    built.program.reset();  // parent constants are gone before the graph is decoded
    ProtoMap empty;
    Value result;
    std::string error;
    CHECK(outcome.return_value.decode(empty, result, error) == TransferStatus::Ok);
    CHECK(result.is(Kind::Array));
    CHECK(array_size(result) == 7);
    const std::vector<Value>& items = array_items(result);
    CHECK(items[0].as_int() == 10);
    CHECK(items[1].as_int() == 32);
    CHECK(items[2].as_int() == 7);
    CHECK(string_view_of(items[3]) == kGlobalText);
    CHECK(items[4].is(Kind::Array) && array_size(items[4]) == 3);
    CHECK(array_items(items[4])[2].as_int() == 3);
    CHECK(string_view_of(items[5]) == kLongConstant);
    CHECK(items[6].as_int() == 42);
}

// entry: R0 = G[0] (checked), return R0.
std::unique_ptr<Program> checked_program(std::uint32_t& entry) {
    auto program = std::make_unique<Program>();
    FunctionBuilder main("");
    std::uint16_t global = 0;
    main.alloc(global);
    main.emit(OpCode::Halt);
    program->protos.push_back(main.finish());

    FunctionBuilder fn("checked");
    std::uint16_t value = 0;
    fn.alloc(value);
    fn.emit(OpCode::GetGlobalChecked, value, 0);
    fn.emit(OpCode::Return, value);
    program->protos.push_back(fn.finish());
    entry = static_cast<std::uint32_t>(program->protos.size() - 1);
    return program;
}

void check_readiness() {
    std::uint32_t entry = 0;
    std::unique_ptr<Program> program = checked_program(entry);
    ProtoMap map;
    map.add_all(*program);
    IsolateTaskInput input;
    input.entry_proto = entry;
    input.globals = {I(99)};
    input.proto_map = &map;

    input.global_ready = {0};
    IsolateTaskOutcome refused = execute_isolate_task(*program, input);
    CHECK(refused.status == IsolateTaskStatus::ExecutionError);
    CHECK(refused.result.reason == StopReason::Error);

    input.global_ready = {1};
    IsolateTaskOutcome ready = execute_isolate_task(*program, input);
    CHECK(ready.status == IsolateTaskStatus::Ok);
    CHECK(ready.has_return_value);
    ProtoMap empty;
    Value result;
    std::string error;
    CHECK(ready.return_value.decode(empty, result, error) == TransferStatus::Ok);
    CHECK(result.as_int() == 99);
}

void check_refusals() {
    Built built = task_program();
    ProtoMap map;
    map.add_all(*built.program);
    IsolateTaskInput input = task_input(map, built.entry);

    input.args[0] = Value::adopt(new FutureObject());
    IsolateTaskOutcome future = execute_isolate_task(*built.program, input);
    CHECK(future.status == IsolateTaskStatus::RefusedFuture);
    CHECK(future.error.find("Future") != std::string::npos);
    CHECK(!future.has_return_value && future.return_value.empty());

    input = task_input(map, built.entry);
    input.globals[0] = Value::handle(HandleKind::File, 7, 2);
    IsolateTaskOutcome handle = execute_isolate_task(*built.program, input);
    CHECK(handle.status == IsolateTaskStatus::RefusedHandle);
    CHECK(handle.error.find("Handle") != std::string::npos);
    CHECK(!handle.has_return_value && handle.return_value.empty());

    input = task_input(map, built.entry);
    input.argc = 1;
    IsolateTaskOutcome mismatch = execute_isolate_task(*built.program, input);
    CHECK(mismatch.status == IsolateTaskStatus::InvalidInput);

    input = task_input(map, 999);
    IsolateTaskOutcome range = execute_isolate_task(*built.program, input);
    CHECK(range.status == IsolateTaskStatus::EntryOutOfRange);
}

void check_unsupported_constant() {
    auto program = std::make_unique<Program>();
    FunctionBuilder main("");
    std::uint16_t m0 = 0;
    main.alloc(m0);
    main.emit(OpCode::Halt);
    program->protos.push_back(main.finish());

    FunctionBuilder fn("future-constant");
    std::uint16_t r = 0;
    fn.alloc(r);
    std::uint16_t constant = 0;
    fn.constant(Value::adopt(new FutureObject()), constant);
    fn.emit(OpCode::LoadK, r, static_cast<std::uint16_t>(constant & ~kConstantBit));
    fn.emit(OpCode::Return, r);
    program->protos.push_back(fn.finish());

    ProtoMap map;
    map.add_all(*program);
    IsolateTaskInput input;
    input.entry_proto = 1;
    input.proto_map = &map;
    IsolateTaskOutcome outcome = execute_isolate_task(*program, input);
    CHECK(outcome.status == IsolateTaskStatus::RefusedFuture);
    CHECK(outcome.error.find("Future") != std::string::npos);
    CHECK(!outcome.has_return_value);
}

} // namespace

int main() {
    check_entry_and_result();
    check_readiness();
    check_refusals();
    check_unsupported_constant();
    return rt_test::finish("cp23-isolate-task-unit");
}

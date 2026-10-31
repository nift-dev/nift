#pragma once
// Weekend collections (D-063): the map(), set() and stack() constructors, their
// read-only methods, and same(). Mutators (set/add/remove/push/pop/clear) are
// lowered by the compiler onto the bare binding (KeyedMutate, vm_keyed.cpp),
// like array mutation. Semantics and error texts are legacy's
// (ParserExpression.cpp, "v4.3 collection constructors"); copies are values
// (D-063), legacy shares them.
#include "rt/natives/builtin.h"

#include <string>

namespace nift::rt::natives {

Status fn_map(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_set(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_stack(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status coll_get(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status coll_contains(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status coll_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status coll_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status coll_top(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status coll_front(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_same(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

// Legacy's key rule for map keys and set members: bool, number or string.
bool collection_key_ok(const Value& key) noexcept;

} // namespace nift::rt::natives

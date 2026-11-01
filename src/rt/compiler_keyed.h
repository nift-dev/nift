#pragma once
// D-063 map/set/stack method tables of the compiler's keyed lowering
// (compiler_keyed.cpp): which method names are mutators, fallible reads and
// their legacy arities, for a receiver of exactly one collection kind.
#include "rt/infer.h"
#include "rt/natives/builtin.h"

#include <string>
#include <string_view>

namespace nift::rt::detail {

// The KeyedMethod of mutator `name` on a receiver of exactly `kinds`, or -1.
int keyed_mutator(KindSet kinds, std::string_view name);
// The KeyedMethod of a fallible read compiled as KeyedMutate, or -1.
int keyed_read(KindSet kinds, std::string_view name);
// Legacy's arity of a map/set/stack method and its arity message, or -1.
int keyed_arity_of(KindSet kinds, std::string_view name, std::string& message);
natives::Receiver keyed_receiver(KindSet kinds);

} // namespace nift::rt::detail

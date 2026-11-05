#pragma once
// B3/B4 presentation dispatch for struct receivers (compiler_presentation.cpp)
// and the per-compile struct descriptor it tests against (compiler_structs.cpp).
#include "rt/compiler_expr.h"
#include "rt/struct_type.h"

#include <memory>
#include <string>
#include <vector>

namespace nift::rt::detail {
// B3: one descriptor per declaration per compile; in a session one per schema
// version (the cache is the UnitCompiler's).
std::shared_ptr<const StructType> struct_descriptor(const UnitPlan& plan, SessionCompileContext* session,
                                                    std::vector<std::shared_ptr<const StructType>>& cache, int s,
                                                    const std::string& display_name);
} // namespace nift::rt::detail

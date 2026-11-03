#pragma once
// The mutation protocol's collection entry points (map, set, stack: rt/mutation.h)
// and the refusal builders and stage helpers they share with mutation.cpp.
// Internal to the protocol's two translation units.
#include "rt/mutation.h"

#include "rt/aggregate_collection.h"

#include <string>
#include <string_view>

namespace nift::rt::mutation::internal {

// The refusals, built out of line (B1-perf): a validated entry point's
// success path then needs no frame for their text.
NIFT_MUTATION_COLD Outcome failed(Failure failure, std::string text);
NIFT_MUTATION_COLD Outcome failed_text(Failure failure, const char* text);
NIFT_MUTATION_COLD Outcome budget_failure();
NIFT_MUTATION_COLD Outcome collection_admit_failure(const Value& receiver, CollectionKind sub, const char* method,
                                                    std::string_view receiver_text);

// 2.1 then 2.2: the receiver kind (and collection sub-kind), then immutability.
NIFT_MUTATION_INLINE bool admits_collection(const Value& receiver, CollectionKind sub) noexcept {
    return receiver.is(Kind::Collection) && collection_kind(receiver) == sub && !refuses_mutation(receiver);
}

// 2.4 for one stored operand (rt/mutation.h closes, with the Env's graph).
NIFT_MUTATION_INLINE bool closes(const Value& receiver, const Value& stored, const Env& env) {
    return mutation::closes(receiver, stored, env.graph);
}

} // namespace nift::rt::mutation::internal

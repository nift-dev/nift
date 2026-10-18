#include "rt/natives/builtin.h"

#include "rt/natives/array_methods.h"
#include "rt/natives/object_methods.h"
#include "rt/natives/string_methods.h"

// rt-lint: cold-file names are resolved here at compile time only, never on the VM hot path

namespace nift::rt::natives {

namespace {

constexpr NativeFn kFunctions[] = {
    str_length, str_empty, str_split, str_trim, str_trim_start, str_trim_end, str_to_upper, str_to_lower,
    str_index_of, str_last_index_of, str_contains, str_starts_with, str_ends_with, str_replace, str_substr,
    str_to_string, str_to_int, str_to_double,
    arr_size, arr_length, arr_empty, arr_first, arr_last, arr_index_of, arr_contains, arr_join, arr_slice,
    arr_unique, arr_flatten, arr_sum, arr_min, arr_max, arr_take, arr_drop, arr_chunk, arr_from_entries,
    fn_min, fn_max, fn_range,
    obj_size, obj_empty, obj_keys, obj_values, obj_entries, obj_has, obj_get, obj_merge, obj_merge_deep,
    obj_pick, obj_omit,
};

constexpr BuiltinSpec kSpecs[] = {
#define NIFT_RT_BUILTIN(id, name, receiver, min, max, result, rule, may_fail, guard) \
    {BuiltinId::id, name, Receiver::receiver, min, max, result, ResultRule::rule, may_fail, guard, nullptr},
#include "rt/natives/builtin_list.inc"
#undef NIFT_RT_BUILTIN
};

static_assert(sizeof kFunctions / sizeof kFunctions[0] == static_cast<std::size_t>(BuiltinId::Count),
              "one native function per built-in, in builtin_list.inc order");
static_assert(sizeof kSpecs / sizeof kSpecs[0] == static_cast<std::size_t>(BuiltinId::Count), "spec table size");

struct Table {
    BuiltinSpec specs[static_cast<std::size_t>(BuiltinId::Count)];
    Table() {
        for (std::size_t i = 0; i < static_cast<std::size_t>(BuiltinId::Count); ++i) {
            specs[i] = kSpecs[i];
            specs[i].fn = kFunctions[i];
        }
    }
};

const Table& table() {
    static const Table instance;
    return instance;
}

} // namespace

const BuiltinSpec& builtin_spec(BuiltinId id) { return table().specs[static_cast<std::size_t>(id)]; }

std::size_t builtin_count() { return static_cast<std::size_t>(BuiltinId::Count); }

const BuiltinSpec* find_builtin(Receiver receiver, std::string_view name) {
    for (const BuiltinSpec& spec : table().specs)
        if (spec.receiver == receiver && spec.name == name) return &spec;
    return nullptr;
}

Status call_builtin(const BuiltinSpec& spec, const Context& context, const Value* args, std::uint32_t argc,
                    Value& out, std::string& error) {
    return spec.fn(context, args, argc, out, error);
}

} // namespace nift::rt::natives

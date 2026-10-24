#include "rt/natives/builtin.h"

#include "rt/natives/array_methods.h"
#include "rt/natives/bytes_methods.h"
#include "rt/natives/host_ops.h"
#include "rt/natives/json_methods.h"
#include "rt/natives/number_methods.h"
#include "rt/natives/format_methods.h"
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
    scalar_to_string, scalar_to_string, scalar_to_string, fmt_stringify, fmt_prettify, fmt_highlight,
    fn_html_escape, fn_attr_escape, fn_url_encode, fn_type,
    fn_is_null, fn_is_bool, fn_is_number, fn_is_int, fn_is_float, fn_is_string, fn_is_bytes, fn_is_array,
    fn_is_object, fn_is_struct, fn_is_function, fn_is_collection, fn_is_enum,
    num_to_int, num_abs, num_floor, num_ceil, num_round,
    fn_bytes, str_encode, bytes_length, bytes_size, bytes_empty, bytes_slice, bytes_decode, fn_sha256,
    fn_validate, fn_copy, fn_copy,
    host_open, host_open_bytes, host_cat, host_sha256_file,
    host_exists, host_is_file, host_is_dir, host_stat,
    host_ls, host_pwd, host_cd, host_touch, host_mkdir, host_mkdir, host_remove, host_remove,
    host_cp, host_mv, host_mv, host_which,
    host_getenv, host_env, host_setenv, host_unsetenv, host_os, host_arch, host_platform,
    host_hardware_concurrency, host_epoch, host_sleep, host_secure_random_bytes,
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

std::string_view recoverable_code(BuiltinId id) {
    switch (id) {
        case BuiltinId::FnValidate: return kSchemaRejectedCode;
        case BuiltinId::HostOpen: case BuiltinId::HostOpenBytes: case BuiltinId::HostCat:
        case BuiltinId::HostSha256File: return "io.open_failed";
        case BuiltinId::HostExists: case BuiltinId::HostIsFile: case BuiltinId::HostIsDir:
        case BuiltinId::HostStat: return "io.metadata_failed";
        case BuiltinId::HostLs: return "io.directory_read_failed";
        case BuiltinId::HostCd: return "io.change_directory_failed";
        case BuiltinId::HostTouch: case BuiltinId::HostMkdir: case BuiltinId::HostMakeDir:
            return "io.create_failed";
        case BuiltinId::HostRemove: case BuiltinId::HostRm: return "io.remove_failed";
        case BuiltinId::HostCp: return "io.copy_failed";
        case BuiltinId::HostMv: case BuiltinId::HostMove: return "io.move_failed";
        default: return std::string_view();
    }
}

Status call_builtin(const BuiltinSpec& spec, const Context& context, const Value* args, std::uint32_t argc,
                    Value& out, std::string& error) {
    return spec.fn(context, args, argc, out, error);
}

bool is_recoverable_code(std::string_view code) {
    if (code.empty()) return false;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(BuiltinId::Count); ++i)
        if (recoverable_code(static_cast<BuiltinId>(i)) == code) return true;
    return false;
}

} // namespace nift::rt::natives

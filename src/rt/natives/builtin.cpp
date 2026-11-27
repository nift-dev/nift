#include "rt/natives/builtin.h"

#include "rt/aggregate_collection.h"
#include "rt/handle.h"

#include "rt/natives/array_methods.h"
#include "rt/natives/bytes_methods.h"
#include "rt/natives/collection_methods.h"
#include "rt/natives/host_file_handles.h"
#include "rt/natives/host_ops.h"
#include "rt/natives/host_process.h"
#include "rt/natives/host_stream_handles.h"
#include "rt/natives/host_stream_extract.h"
#include "rt/natives/json_methods.h"
#include "rt/natives/minify_builtin.h"
#include "rt/natives/number_methods.h"
#include "rt/natives/format_methods.h"
#include "rt/natives/object_methods.h"
#include "rt/natives/shared_methods.h"
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
    fn_validate, fn_copy, fn_deepcopy,
    host_open, host_open_bytes, host_cat, host_sha256_file,
    host_exists, host_is_file, host_is_dir, host_stat,
    host_ls, host_pwd, host_cd, host_touch, host_mkdir, host_mkdir, host_remove, host_remove,
    host_cp, host_mv, host_mv, host_which,
    host_getenv, host_env, host_setenv, host_unsetenv, host_os, host_arch, host_platform,
    host_hardware_concurrency, host_epoch, host_sleep, host_secure_random_bytes,
    handle_file, handle_ifstream, handle_ofstream, handle_close,
    file_open, file_close, file_read, file_read_all, file_read_line, file_write, file_write_line, file_flush,
    file_save, file_revert, file_path, file_exists, file_modified, file_tell, file_seek, file_eof, file_copy,
    file_move, file_remove,
    stream_close, stream_read, stream_read_all, stream_read_line, stream_write, stream_write_line, stream_write_bytes,
    stream_flush, stream_eof,
    shared_mutex, shared_atomic_int, shared_atomic_bool, shared_channel, shared_send, shared_recv, shared_cancel,
    mutex_lock, mutex_try_lock, mutex_unlock, mutex_locked, mutex_get, mutex_set,
    atomic_load, atomic_store, atomic_exchange, atomic_fetch_add, atomic_fetch_sub, atomic_compare_exchange,
    fn_map, fn_set, fn_stack, coll_get, coll_contains, coll_size, coll_empty, coll_contains, coll_size, coll_empty,
    coll_top, coll_front, coll_contains, coll_size, coll_empty, fn_same,
    stream_read_bytes, stream_read_all_bytes, stream_extract_string, fn_minify, file_write_val,
    file_append, file_prepend, file_insert, file_insert_before, file_insert_after, file_replace, file_replace_once,
    fn_sorted_map, fn_sorted_set,
    process_check, process_argument, process_spread, process_run, process_run_stdout, host_inject_json,
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

constexpr std::uint16_t kNoSibling = 0xffff;

constexpr std::uint32_t bit(Kind k) { return 1u << static_cast<unsigned>(k); }
std::uint32_t accept_of(Receiver r) {
    switch (r) {
        case Receiver::None: case Receiver::Any: return 0;
        case Receiver::Null: return bit(Kind::Null);
        case Receiver::Bool: return bit(Kind::Bool);
        case Receiver::Number: return bit(Kind::Int) | bit(Kind::Double) | bit(Kind::Decimal);
        case Receiver::String: return bit(Kind::String);
        case Receiver::Array: return bit(Kind::Array);
        case Receiver::Object: return bit(Kind::Object);
        case Receiver::Bytes: return bit(Kind::Bytes);
        case Receiver::Map: case Receiver::Set: case Receiver::Stack: return bit(Kind::Collection) | kAcceptSubkind;
        default: return bit(Kind::Handle) | kAcceptSubkind;  // File, Stream, Mutex, Atomic, Channel
    }
}

struct Table {
    BuiltinSpec specs[static_cast<std::size_t>(BuiltinId::Count)];
    Table() {
        constexpr std::size_t n = static_cast<std::size_t>(BuiltinId::Count);
        for (std::size_t i = 0; i < n; ++i) {
            specs[i] = kSpecs[i];
            specs[i].fn = kFunctions[i];
        }
    }
};

const Table& table() {
    static const Table instance;
    return instance;
}

// B4: per built-in, per receiver family, the native of the same name; built on
// first use (a dispatch mismatch only), never on the ordinary call path.
// Entries exist only where a Dynamic site can redirect: between the plain
// families (null, bool, number, string, array, object), and between map, set
// and stack for size and empty. There the same name is the same legacy method
// on another family (legacy selects the implementation by the receiver's
// kind), so a redirection is that operation, never an unrelated one; the
// entries are listed and justified in b4-dispatch.md (the redirection table).
// No entry crosses the two groups (a string's contains is not a map's), and
// handle and bytes families have none: a mismatch there is a defect.
int redirect_group(Receiver r) {
    switch (r) {
        case Receiver::Null: case Receiver::Bool: case Receiver::Number: case Receiver::String:
        case Receiver::Array: case Receiver::Object: return 1;
        case Receiver::Map: case Receiver::Set: case Receiver::Stack: return 2;
        default: return 0;
    }
}
bool redirectable(const BuiltinSpec& from, Receiver to) {
    const int group = redirect_group(from.receiver);
    if (group == 0 || group != redirect_group(to)) return false;
    return group == 1 || from.name == "size" || from.name == "empty";
}

struct Siblings {
    std::uint16_t of[static_cast<std::size_t>(BuiltinId::Count)][kReceiverCount];
    explicit Siblings(const BuiltinSpec* specs) {
        constexpr std::size_t n = static_cast<std::size_t>(BuiltinId::Count);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t r = 0; r < kReceiverCount; ++r) {
                of[i][r] = kNoSibling;
                if (!redirectable(specs[i], static_cast<Receiver>(r))) continue;
                for (std::size_t j = 0; j < n; ++j)
                    if (static_cast<std::size_t>(specs[j].receiver) == r && specs[j].name == specs[i].name) {
                        of[i][r] = static_cast<std::uint16_t>(j);
                        break;
                    }
#if defined(NIFT_B4_MUTANT) && NIFT_B4_MUTANT == 1
                if (r == static_cast<std::size_t>(Receiver::Array)) of[i][r] = kNoSibling;  // A1: omits arrays
#endif
            }
    }
};

const Siblings& siblings() {
    static const Siblings instance(table().specs);
    return instance;
}

} // namespace

const BuiltinSpec& builtin_spec(BuiltinId id) { return table().specs[static_cast<std::size_t>(id)]; }

std::size_t builtin_count() { return static_cast<std::size_t>(BuiltinId::Count); }

Receiver receiver_of(const Value& value) noexcept {
    switch (value.kind()) {
        case Kind::Null: return Receiver::Null;
        case Kind::Bool: return Receiver::Bool;
        case Kind::Int: case Kind::Double: case Kind::Decimal: return Receiver::Number;
        case Kind::String: return Receiver::String;
        case Kind::Bytes: return Receiver::Bytes;
        case Kind::Array: return Receiver::Array;
        case Kind::Object: return Receiver::Object;
        case Kind::Collection:
            switch (collection_kind(value)) {
                case CollectionKind::Map: return Receiver::Map;
                case CollectionKind::Set: return Receiver::Set;
                default: return Receiver::Stack;
            }
        case Kind::Handle:
            switch (value.handle_kind()) {
                case HandleKind::File: return Receiver::File;
                case HandleKind::Stream: return Receiver::Stream;
                case HandleKind::Mutex: return Receiver::Mutex;
                case HandleKind::Atomic: return Receiver::Atomic;
                case HandleKind::Channel: return Receiver::Channel;
                default: return Receiver::None;
            }
        default: return Receiver::None;
    }
}

std::uint32_t receiver_accept(const BuiltinSpec& spec) noexcept { return accept_of(spec.receiver); }

const BuiltinSpec* dispatch_sibling(const BuiltinSpec& spec, Receiver family) noexcept {
    const std::uint16_t j = siblings().of[static_cast<std::size_t>(spec.id)][static_cast<std::size_t>(family)];
    return j == kNoSibling ? nullptr : &table().specs[j];
}

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
        case BuiltinId::FileOpen: case BuiltinId::FileSave: return "io.open_failed";
        case BuiltinId::FileCopy: return "io.copy_failed";
        case BuiltinId::FileMove: return "io.move_failed";
        case BuiltinId::FileRemove: return "io.remove_failed";
        case BuiltinId::HandleIfstream: case BuiltinId::HandleOfstream: return "stream.open_failed";
        case BuiltinId::StreamRead: case BuiltinId::StreamReadAll: case BuiltinId::StreamReadLine:
        case BuiltinId::StreamReadBytes: case BuiltinId::StreamReadAllBytes: case BuiltinId::StreamExtractString:
            return "stream.read_failed";
        case BuiltinId::StreamWrite: case BuiltinId::StreamWriteLine: return "stream.write_failed";
        case BuiltinId::StreamFlush: return "stream.flush_failed";
        case BuiltinId::StreamClose: return "stream.close_failed";
        case BuiltinId::SharedRecv: case BuiltinId::SharedSend: return "runtime.channel_closed";
        case BuiltinId::SharedCancel: return "runtime.cancelled";
        case BuiltinId::SharedMutex: case BuiltinId::SharedAtomicInt: case BuiltinId::SharedAtomicBool:
        case BuiltinId::SharedChannel: case BuiltinId::MutexLock: case BuiltinId::MutexTryLock:
        case BuiltinId::MutexUnlock: case BuiltinId::MutexGet: case BuiltinId::MutexSet:
        case BuiltinId::AtomicLoad: case BuiltinId::AtomicStore: case BuiltinId::AtomicExchange:
        case BuiltinId::AtomicFetchAdd: case BuiltinId::AtomicFetchSub:
        case BuiltinId::AtomicCompareExchange: return "runtime.invalid_argument";
        default: return std::string_view();
    }
}

Status call_builtin(const BuiltinSpec& spec, const Context& context, const Value* args, std::uint32_t argc,
                    Value& out, std::string& error) {
    return spec.fn(context, args, argc, out, error);
}

bool is_recoverable_code(std::string_view code) {
    if (code.empty()) return false;
    if (code == "runtime.cancelled" || code == "runtime.channel_closed" ||
        code == "runtime.atomic_overflow" || code == "runtime.invalid_argument")
        return true;
    // D-NEXT-refsem #4: a refused reference cycle (the receiver is unchanged).
    if (code == "runtime.reference_cycle") return true;
    // B2 (owner E): a mutation of an array while its comparator sort runs
    // (refused before anything changes; the sort continues).
    if (code == "runtime.sort_receiver_mutated") return true;
    // CP17 handle statuses classed Recoverable (D-035 runtime.handle_closed).
    if (handle_code_recoverable(code)) return true;
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(BuiltinId::Count); ++i)
        if (recoverable_code(static_cast<BuiltinId>(i)) == code) return true;
    return false;
}

} // namespace nift::rt::natives

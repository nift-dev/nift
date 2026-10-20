#include "rt/host/host_op.h"

#include <cstddef>

namespace nift::rt::host {

namespace {
using TP = TracePoint;
using R = Recording;
// Order must match HostOp. Checked by host_op_table_consistent() in the tests.
constexpr HostOpInfo kOps[] = {
    {HostOp::Stat, "stat", TP::PostAlways, "stat", R::Presence},
    {HostOp::List, "list", TP::Pre, "list", R::Listing},
    {HostOp::Glob, "glob", TP::Pre, "glob", R::Listing},
    {HostOp::Read, "read", TP::PostOk, "read", R::Read},
    {HostOp::Open, "open", TP::Pre, "open", R::OpenMode},
    {HostOp::Save, "save", TP::Pre, "save", R::Written},
    {HostOp::Touch, "touch", TP::Pre, "touch", R::Written},
    {HostOp::Remove, "remove", TP::Pre, "remove", R::Written},
    {HostOp::Mkdir, "mkdir", TP::Pre, "mkdir", R::Written},
    {HostOp::CopyTo, "copy-to", TP::Pre, "copy-to", R::Written},
    {HostOp::MoveTo, "move-to", TP::Pre, "move-to", R::Written},
    {HostOp::MoveFrom, "move-from", TP::Pre, "move-from", R::Written},
    {HostOp::ReadSource, "read-source", TP::PostAlways, "read-source", R::Read},
    {HostOp::ReadJson, "read-json", TP::PostAlways, "read-json", R::Read},
    {HostOp::Getenv, "getenv", TP::PostOk, "getenv", R::Environment},
    {HostOp::GetenvTemplate, "getenv", TP::PostAlways, "getenv", R::Environment},
    {HostOp::EnvAll, "env-all", TP::Pre, "env-all", R::EnvironmentSnapshot},
    {HostOp::Spawn, "spawn", TP::Pre, "spawn", R::None},
    // D-028 (host-deps): open_bytes() and sha256_file() are traced `read`
    // and recorded as dependencies, hashed from the streamed bytes.
    {HostOp::ReadBytes, "read", TP::PostOk, "read-bytes", R::StreamedRead},
    {HostOp::StreamOpen, nullptr, TP::None, "stream-open", R::StreamOpen},  // D-028: input streams at successful open
    {HostOp::StreamRead, nullptr, TP::None, "stream-read", R::None},
    {HostOp::StreamWrite, nullptr, TP::None, "stream-write", R::None},
    {HostOp::StreamClose, nullptr, TP::None, "stream-close", R::None},
    {HostOp::Inject, nullptr, TP::None, "inject", R::Read},
    {HostOp::FileCopy, nullptr, TP::None, "file-copy", R::Written},
    {HostOp::FileMove, nullptr, TP::None, "file-move", R::Written},
    {HostOp::FileRemove, nullptr, TP::None, "file-remove", R::Written},
    {HostOp::Chdir, nullptr, TP::None, "cd", R::None},
    {HostOp::Setenv, nullptr, TP::None, "setenv", R::None},
    {HostOp::Unsetenv, nullptr, TP::None, "unsetenv", R::None},
    {HostOp::Which, nullptr, TP::None, "which", R::None},
    {HostOp::Platform, nullptr, TP::None, "platform", R::Platform},
    {HostOp::Clock, nullptr, TP::None, "clock", R::None},
    {HostOp::Sleep, nullptr, TP::None, "sleep", R::None},
    {HostOp::Random, nullptr, TP::None, "random", R::None},
    {HostOp::ProcessWait, nullptr, TP::None, "wait", R::None},
    {HostOp::ProcessCancel, nullptr, TP::None, "cancel", R::None},
};
static_assert(sizeof(kOps) / sizeof(kOps[0]) == static_cast<std::size_t>(HostOp::Count_), "op table out of sync");
} // namespace

const HostOpInfo& op_info(HostOp op) {
    const auto index = static_cast<std::size_t>(op);
    return kOps[index < static_cast<std::size_t>(HostOp::Count_) ? index : 0];
}

const char* op_v2_name(HostOp op) { return op_info(op).v2_name; }

bool op_is_write(HostOp op) {
    switch (op) {
        case HostOp::Save: case HostOp::Touch: case HostOp::Remove: case HostOp::Mkdir:
        case HostOp::CopyTo: case HostOp::MoveTo: case HostOp::MoveFrom:
        case HostOp::FileCopy: case HostOp::FileMove: case HostOp::FileRemove:
        case HostOp::StreamWrite:
            return true;
        default:
            return false;
    }
}

} // namespace nift::rt::host

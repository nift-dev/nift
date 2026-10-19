#pragma once
// CP16 Host layer: the operation table (design §2.4, §2.5; trace v2 per O2).
//
// One row per Host op: its trace v1 name and emission point (exactly legacy,
// pinned by tests/runtime2/cp16 fixtures), its trace v2 name (every op,
// emitted after the attempt), and the dependency-recording rule for page
// renders. HostCall consults only this table, so a native cannot trace or
// record at the wrong time.
#include <cstdint>

namespace nift::rt::host {

enum class HostOp : std::uint8_t {
    // traced in v1
    Stat, List, Glob, Read, Open, Save, Touch, Remove, Mkdir, CopyTo, MoveTo, MoveFrom,
    ReadSource, ReadJson, Getenv, GetenvTemplate, EnvAll, Spawn,
    // not traced in v1 (trace-format.md "Not traced"; design finding 5)
    ReadBytes, StreamOpen, StreamRead, StreamWrite, StreamClose, Inject, FileCopy, FileMove, FileRemove,
    Chdir, Setenv, Unsetenv, Which, Platform, Clock, Sleep, Random, ProcessWait, ProcessCancel,
    Count_
};

// When the v1 line is written relative to the attempt.
enum class TracePoint : std::uint8_t {
    None,        // not traced in v1
    Pre,         // before the attempt, even when it then fails (writes, list/glob, open, spawn, env-all)
    PostOk,      // after a successful attempt only (read, stat, getenv)
    PostAlways,  // after the attempt, success or failure (@getenv: res "error")
};

// What a successful op records into page observations (D-002). Standalone
// scripts record nothing (null Observer).
enum class Recording : std::uint8_t {
    None,
    Read,          // dependency + content hash
    StreamedRead,  // dependency + streamed hash (sha256_file; file-backed streams, D-028)
    Presence,      // reqs/kinds, or absent; plus metadata_read when the payload asks (stat() on a file)
    Listing,       // listing fingerprint (dir:/glob: key)
    Environment,   // environment[name]
    EnvironmentSnapshot,
    Platform,
    Written,       // drops the path from observations
    OpenMode,      // file().open(mode): r -> Read; rw -> hash only (legacy, code reading); w/a -> None
};

struct HostOpInfo {
    HostOp op;
    const char* v1_name;  // nullptr when not traced in v1
    TracePoint v1_point;
    const char* v2_name;  // every op has one
    Recording recording;
};

const HostOpInfo& op_info(HostOp op);
const char* op_v2_name(HostOp op);
// True when the op writes (its v2 failure line must say so; Q-H10).
bool op_is_write(HostOp op);

} // namespace nift::rt::host

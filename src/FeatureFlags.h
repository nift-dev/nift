#pragma once
// Runtime 2.0 CP05: diagnostic NIFT_P4_* switches are read once per process.
// They only enable tracing or turn off an equivalent fast path, are never set
// by Nift itself, and were previously looked up with getenv() on every user
// function call (~9% of a flat-call benchmark). Switches that Nift sets at run
// time (NIFT_NO_PROCESS, NIFT_FS_ROOT) are deliberately not cached here.
#include <cstdlib>

namespace nift::flags {
inline bool env_set(const char* name) { return std::getenv(name) != nullptr; }
inline bool p4_disable() { static const bool value = env_set("NIFT_P4_DISABLE"); return value; }
inline bool p4_trace()   { static const bool value = env_set("NIFT_P4_TRACE");   return value; }
inline bool p4_shape()   { static const bool value = env_set("NIFT_P4_SHAPE");   return value; }
inline bool p4_bridge()  { static const bool value = env_set("NIFT_P4_BRIDGE");  return value; }
} // namespace nift::flags

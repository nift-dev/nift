#pragma once
// The same file name in another directory is not allowed.
namespace nift::rt { inline void reset(Value& v) { Storage::array_mut(v).clear(); } }

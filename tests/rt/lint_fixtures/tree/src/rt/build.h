#pragma once
// An allowed storage file (by path): the storage commit passes.
namespace nift::rt { inline void reset(Value& v) { Storage::array_mut(v).clear(); } }

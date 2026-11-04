#pragma once
// The top-level tests/ tree defines the accessor (not scanned).
namespace nift::rt { struct StorageTestAccess { static void clear(Value& v) { Storage::array_mut(v).clear(); } }; }

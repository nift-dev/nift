#pragma once
// helper
#include "rt/aggregate.h"
namespace nift::rt { struct StorageTestAccess {
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}

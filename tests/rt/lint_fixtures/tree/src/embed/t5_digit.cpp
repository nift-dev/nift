#include "rt/aggregate.h"
namespace nift::rt { constexpr int k = 1'0; struct StorageTestAccess {
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}

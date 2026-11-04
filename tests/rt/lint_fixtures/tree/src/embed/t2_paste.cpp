#include "rt/aggregate.h"
#define CAT(a, b) a##b
namespace nift::rt { struct CAT(StorageTest, Access) {
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}
void h2(nift::rt::Value& v) { nift::rt::CAT(StorageTest, Access)::clear(v); }

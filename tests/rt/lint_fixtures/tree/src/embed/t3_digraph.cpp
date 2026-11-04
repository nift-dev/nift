#include "rt/aggregate.h"
#define CAT(a, b) a%:%:b
namespace nift::rt { struct CAT(StorageTest, Access) {
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}

#include "rt/aggregate.h"
namespace nift::rt { struct StorageTest\
Access {
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}

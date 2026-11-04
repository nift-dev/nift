#include "rt/aggregate.h"
namespace nift::rt { const char* r = R"(")"; struct StorageTestAccess {
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}

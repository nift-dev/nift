#include "rt/aggregate.h"
namespace nift::rt { struct StorageTestAccess {  // rt-lint: test-access-decl
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}
void h1(nift::rt::Value& v) { nift::rt::StorageTestAccess::clear(v); }  // rt-lint: test-access-decl

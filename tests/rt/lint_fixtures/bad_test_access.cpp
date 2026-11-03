// Lint fixture (re-check R2, the reviewer's rc/forge/f23): a production file
// that defines the test accessor gets every storage commit. The test-access
// scan must flag lines 5 and 9.
#include "rt/aggregate.h"
namespace nift::rt { struct StorageTestAccess {
    static void clear(Value& v) { Storage::array_mut(v).clear(); }
};
}
void hijack2(nift::rt::Value& v) { nift::rt::StorageTestAccess::clear(v); }

// B2 compile-fail test (re-check R1): SortKey is defined completely in
// rt/mutation.h, so a translation unit cannot define its own public one.
// Must NOT compile.
#include "rt/mutation.h"
namespace nift::rt::mutation {
class SortKey {
public:
    SortKey() {}
};
}
void hijack(nift::rt::Value& v) { nift::rt::mutation::Protocol::sort_items(nift::rt::mutation::SortKey(), v).clear(); }

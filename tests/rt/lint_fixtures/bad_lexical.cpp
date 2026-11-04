#include "bad_lexical_inc.inc"
#define CAT(a, b) a %:%: b
%:include "rt//sort_controller.h"
// The re-checker's rc/lintb/rt/vm_bypass2.cpp, rc/m-digit and the raw-string forms (b2-refrep.md 19.3):
// the boundary rules fire on exactly the lines rt-lint.py's self-test lists (b4's pasted name at its #define).
namespace nift::rt {
void b1(Value& published, const Value& x) { int n = 1'0; array_push(published, x); int m = 2'0; (void)n; (void)m; }
void b2(Value& published, const Value& x) { const char* s = R"(")"; array_push(published, x); (void)s; }
void b3(Value& published, const Value& x) {
    array_\
push(published, x);
}
void b4(Value& published, const Value& x) { CAT(array_, push)(published, x); }
void b5(Value& published, const Value& x) { array_push(published, make_string("rt-lint: storage-ok fresh")); (void)x; }
void b6(Value& published, const Value& x) {
    const char* s = R"(")" "/*";
    array_push(published, x);
    object_set(published, Atom{}, x);
    array_build_storage(published).clear();
    const char* t = "*/"; (void)s; (void)t;
}
void b7(Value& published, const Value& x) { inc_push(published, x); }
void b8(Value& published, const Value& x) {
    // rt-lint: storage-ok fresh, one annotation, then a statement split over two lines
    array_push(published,
               x); array_build_set(published, 0, x);
}
bool m_digit(Value& array, const Value& value) {
    if (constexpr int digits = 1'0; digits) { rt::array_push(array, value); return true; }
    return false;
}
void b9(Value& published, const Value& x) {
    const char* r = u8R"x(
)" array_push(published, x); /*
)x"; array_push(published, x);
    const char* q = R"(// rt-lint: storage-ok fresh)"; array_push(published, x); (void)r; (void)q;
}
void b10(Value& published, const Value& x) {
    const char c = '\''; array_push(published, x); const auto f = 0x1'Fp+2'0; (void)c; (void)f;
    Storage::\
array_mut(published).clear();
    Value fresh = make_array(1); array_push(fresh, x);  // rt-lint: storage-ok fresh, this line only
    array_push(published, x);
}
}  // namespace nift::rt

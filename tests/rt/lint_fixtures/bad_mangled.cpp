// Re-review N2 (rr/e1-asm.diff, rr/e3-weak.diff, rrlint/src/rt/v4_asm.cpp): a
// declaration bound to a mangled nift::rt name reaches the symbol past access
// control and names no checked token. Lines 6 to 10 must fire (not 11).
#include <vector>
std::vector<nift::rt::Value>& rr_items(nift::rt::Value&)
    asm("_ZN4nift2rt12Construction13array_storageERNS0_5ValueE");
std::vector<nift::rt::Value>& rr_mut(nift::rt::Value&) __asm__("_ZN4nift2rt7Storage9array_mutERNS0_5ValueE") __attribute__((weak));
#pragma weak _ZN4nift2rt7Storage9array_mutERNS0_5ValueE
extern "C" void _ZN4nift2rt12Construction10array_pushERNS0_5ValueEOS1_(void*, void*);
void f(void* a) { __asm("call _ZN4nift2rt7Storage9array_setERNS0_5ValueEmS2_"); (void)a; }
const char* note = "not a symbol: nift::rt::Storage";

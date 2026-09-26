#include <nift/nift.h>
#include <nift/c_abi.h>
static_assert(NIFT_C_ABI_VERSION_MAJOR == 1, "C ABI major must be frozen");
int main() { nift::Engine engine; nift::Context ctx; (void)engine; (void)ctx; return 0; }

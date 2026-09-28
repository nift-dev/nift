#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#if defined(_WIN32)
# define API __declspec(dllexport)
#else
# define API __attribute__((visibility("default")))
#endif
API int64_t nift_ffi_add_i64(int64_t a, int64_t b) { return a + b; }
API uint64_t nift_ffi_xor_u64(uint64_t a, uint64_t b) { return a ^ b; }
API double nift_ffi_add_f64(double a, double b) { return a + b; }
API int32_t nift_ffi_strlen(const char* s) { return s ? (int32_t)strlen(s) : -1; }
API const char* nift_ffi_greeting(void) { return "hello from ffi"; }
API void* nift_ffi_identity_ptr(void* p) { return p; }
typedef struct { int32_t x; int32_t y; } nift_ffi_pair;
API int32_t nift_ffi_pair_sum(nift_ffi_pair p) { return p.x + p.y; }
API int32_t nift_ffi_pair_sum_ptr(const nift_ffi_pair* p) { return p ? p->x + p->y : -1; }
typedef struct {
    int8_t i8; uint8_t u8; int16_t i16; uint16_t u16;
    int32_t i32; uint32_t u32; int64_t i64; uint64_t u64; bool boolean;
} nift_ffi_integer_bounds;
API bool nift_ffi_check_integer_bounds_ptr(const nift_ffi_integer_bounds* p) {
    return p && p->i8 == INT8_MIN && p->u8 == UINT8_MAX &&
           p->i16 == INT16_MIN && p->u16 == UINT16_MAX &&
           p->i32 == INT32_MIN && p->u32 == UINT32_MAX &&
           p->i64 == INT64_MIN && p->u64 == UINT64_MAX && p->boolean;
}
API void nift_ffi_buffer_xor(unsigned char* p, size_t n, unsigned char key) { for(size_t i=0;i<n;++i) p[i]^=key; }
typedef int64_t (*nift_ffi_cb_i64)(int64_t, void*);
API int64_t nift_ffi_call_cb(nift_ffi_cb_i64 cb, void* user, int64_t x) { return cb ? cb(x,user) : -1; }
typedef int64_t (*nift_ffi_cb_simple)(int64_t);
API int64_t nift_ffi_call_cb_simple(nift_ffi_cb_simple cb, int64_t x) { return cb ? cb(x) : -1; }

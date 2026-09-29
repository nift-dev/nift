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

API bool nift_ffi_not_bool(bool x) { return !x; }
API int8_t nift_ffi_i8(int8_t x) { return x; }
API uint8_t nift_ffi_u8(uint8_t x) { return x; }
API int16_t nift_ffi_i16(int16_t x) { return x; }
API uint16_t nift_ffi_u16(uint16_t x) { return x; }
API int32_t nift_ffi_i32(int32_t x) { return x; }
API uint32_t nift_ffi_u32(uint32_t x) { return x; }
API int64_t nift_ffi_i64(int64_t x) { return x; }
API uint64_t nift_ffi_u64(uint64_t x) { return x; }
API float nift_ffi_add_f32(float a, float b) { return a + b; }
API const char* nift_ffi_null_cstr(void) { return NULL; }
API void* nift_ffi_null_ptr(void) { return NULL; }
static int32_t nift_ffi_void_value;
API void nift_ffi_set_void(int32_t x) { nift_ffi_void_value = x; }
API int32_t nift_ffi_get_void(void) { return nift_ffi_void_value; }

API int64_t nift_ffi_arity0(void) { return 10; }
API int64_t nift_ffi_arity1(int8_t a) { return a; }
API int64_t nift_ffi_arity2(int8_t a, uint16_t b) { return a + b; }
API int64_t nift_ffi_arity3(int8_t a, uint16_t b, int32_t c) { return a + b + c; }
API int64_t nift_ffi_arity4(int8_t a, uint16_t b, int32_t c, uint64_t d) { return a + b + c + (int64_t)d; }
API int64_t nift_ffi_arity5(int8_t a, uint16_t b, int32_t c, uint64_t d, void* p) { return a + b + c + (int64_t)d + (p != NULL); }
API int64_t nift_ffi_arity6(int8_t a, uint16_t b, int32_t c, uint64_t d, void* p, const char* s) { return a + b + c + (int64_t)d + (p != NULL) + (s ? (int64_t)strlen(s) : 0); }

API float nift_ffi_f32_0(void) { return 0.5f; }
API float nift_ffi_f32_1(float a) { return a; }
API float nift_ffi_f32_2(float a, float b) { return a + b; }
API float nift_ffi_f32_3(float a, float b, float c) { return a + b + c; }
API float nift_ffi_f32_4(float a, float b, float c, float d) { return a + b + c + d; }
API double nift_ffi_f64_0(void) { return 0.25; }
API double nift_ffi_f64_1(double a) { return a; }
API double nift_ffi_f64_2(double a, double b) { return a + b; }
API double nift_ffi_f64_3(double a, double b, double c) { return a + b + c; }
API double nift_ffi_f64_4(double a, double b, double c, double d) { return a + b + c + d; }

typedef struct { int8_t a; uint32_t b; int64_t i; double d; bool flag; } nift_ffi_layout;
API uint64_t nift_ffi_layout_size(void) { return (uint64_t)sizeof(nift_ffi_layout); }
API bool nift_ffi_check_layout_ptr(const nift_ffi_layout* p) {
    return p && p->a == -7 && p->b == UINT32_MAX && p->i == INT64_MIN &&
           p->d == 3.5 && p->flag;
}
API uint64_t nift_ffi_layout_offset(unsigned i) {
    static const size_t offsets[] = {
        offsetof(nift_ffi_layout, a), offsetof(nift_ffi_layout, b),
        offsetof(nift_ffi_layout, i), offsetof(nift_ffi_layout, d),
        offsetof(nift_ffi_layout, flag)
    };
    return i < sizeof(offsets) / sizeof(offsets[0]) ? (uint64_t)offsets[i] : UINT64_MAX;
}

typedef struct {
    bool boolean;
    int8_t i8; uint8_t u8; int16_t i16; uint16_t u16;
    int32_t i32; uint32_t u32; int64_t i64; uint64_t u64;
    float f32; double f64;
    void* pointer; const char* cstr;
} nift_ffi_all_scalars;
API uint64_t nift_ffi_all_scalars_size(void) { return (uint64_t)sizeof(nift_ffi_all_scalars); }
typedef struct {
    bool boolean;
    int8_t i8; uint8_t u8; int16_t i16; uint16_t u16;
    int32_t i32; uint32_t u32; int64_t i64; uint64_t u64;
    float f32; double f64;
} nift_ffi_construct_scalars;
API bool nift_ffi_check_all_scalars_ptr(const nift_ffi_construct_scalars* p) {
    return p && p->boolean && p->i8 == INT8_MIN && p->u8 == UINT8_MAX &&
           p->i16 == INT16_MIN && p->u16 == UINT16_MAX &&
           p->i32 == INT32_MIN && p->u32 == UINT32_MAX &&
           p->i64 == INT64_MIN && p->u64 == UINT64_MAX &&
           p->f32 == 1.25f && p->f64 == 2.5;
}

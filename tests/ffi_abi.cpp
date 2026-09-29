#include "FfiAbi.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

struct LayoutOracle {
    bool boolean;
    std::int8_t i8;
    std::uint8_t u8;
    std::int16_t i16;
    std::uint16_t u16;
    std::int32_t i32;
    std::uint32_t u32;
    std::int64_t i64;
    std::uint64_t u64;
    float f32;
    double f64;
    void* pointer;
    const char* cstr;
};

int main() {
    NiftFfiValue value{};
    assert(nift_ffi_integer_return_storage(value, NiftFfiIntegerType::Bool) ==
           &value.unsigned_word);
    assert(nift_ffi_integer_return_storage(value, NiftFfiIntegerType::U8) ==
           &value.unsigned_word);
    assert(nift_ffi_integer_return_storage(value, NiftFfiIntegerType::I8) ==
           &value.signed_word);

    value.unsigned_word = std::numeric_limits<ffi_arg>::max();
    assert(nift_ffi_read_unsigned_return(value, NiftFfiIntegerType::U8) ==
           std::numeric_limits<std::uint8_t>::max());
    value.signed_word = -128;
    assert(nift_ffi_read_signed_return(value, NiftFfiIntegerType::I8) == -128);

    void* u16_storage = nift_ffi_integer_return_storage(value, NiftFfiIntegerType::U16);
    if constexpr (sizeof(std::uint16_t) < sizeof(ffi_arg)) {
        assert(u16_storage == &value.unsigned_word);
        value.unsigned_word = 65535;
    } else {
        assert(u16_storage == &value.u16);
        value.u16 = 65535;
    }
    assert(nift_ffi_read_unsigned_return(value, NiftFfiIntegerType::U16) == 65535);

    void* i16_storage = nift_ffi_integer_return_storage(value, NiftFfiIntegerType::I16);
    if constexpr (sizeof(std::int16_t) < sizeof(ffi_sarg)) {
        assert(i16_storage == &value.signed_word);
        value.signed_word = -32768;
    } else {
        assert(i16_storage == &value.i16);
        value.i16 = -32768;
    }
    assert(nift_ffi_read_signed_return(value, NiftFfiIntegerType::I16) == -32768);

    void* u32_storage = nift_ffi_integer_return_storage(value, NiftFfiIntegerType::U32);
    if constexpr (sizeof(std::uint32_t) < sizeof(ffi_arg)) {
        assert(u32_storage == &value.unsigned_word);
        value.unsigned_word = UINT32_MAX;
    } else {
        assert(u32_storage == &value.u32);
        value.u32 = UINT32_MAX;
    }
    assert(nift_ffi_read_unsigned_return(value, NiftFfiIntegerType::U32) == UINT32_MAX);

    void* i32_storage = nift_ffi_integer_return_storage(value, NiftFfiIntegerType::I32);
    if constexpr (sizeof(std::int32_t) < sizeof(ffi_sarg)) {
        assert(i32_storage == &value.signed_word);
        value.signed_word = INT32_MIN;
    } else {
        assert(i32_storage == &value.i32);
        value.i32 = INT32_MIN;
    }
    assert(nift_ffi_read_signed_return(value, NiftFfiIntegerType::I32) == INT32_MIN);

    std::vector<ffi_type*> fields = {
        &ffi_type_uint8, &ffi_type_sint8, &ffi_type_uint8, &ffi_type_sint16,
        &ffi_type_uint16, &ffi_type_sint32, &ffi_type_uint32, &ffi_type_sint64,
        &ffi_type_uint64, &ffi_type_float, &ffi_type_double, &ffi_type_pointer,
        &ffi_type_pointer,
    };
    std::vector<std::size_t> offsets;
    std::size_t size = 0;
    assert(nift_ffi_struct_layout(fields, offsets, size) == FFI_OK);
    const std::size_t expected[] = {
        offsetof(LayoutOracle, boolean), offsetof(LayoutOracle, i8),
        offsetof(LayoutOracle, u8), offsetof(LayoutOracle, i16),
        offsetof(LayoutOracle, u16), offsetof(LayoutOracle, i32),
        offsetof(LayoutOracle, u32), offsetof(LayoutOracle, i64),
        offsetof(LayoutOracle, u64), offsetof(LayoutOracle, f32),
        offsetof(LayoutOracle, f64), offsetof(LayoutOracle, pointer),
        offsetof(LayoutOracle, cstr),
    };
    assert(offsets == std::vector<std::size_t>(std::begin(expected), std::end(expected)));
    assert(size == sizeof(LayoutOracle));
}

#pragma once

#include <ffi.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using NiftFfiFunction = void (*)(void);
using NiftFfiCallbackI64 = std::int64_t (*)(std::int64_t);

union alignas(std::max_align_t) NiftFfiValue {
    ffi_arg unsigned_word;
    ffi_sarg signed_word;
    std::uint8_t u8;
    std::int8_t i8;
    std::uint16_t u16;
    std::int16_t i16;
    std::uint32_t u32;
    std::int32_t i32;
    std::uint64_t u64;
    std::int64_t i64;
    float f32;
    double f64;
    void* pointer;
    NiftFfiCallbackI64 callback_i64;
};

enum class NiftFfiIntegerType {
    Bool,
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
};

inline ffi_type* nift_ffi_type(const std::string& type) {
    if (type == "void") return &ffi_type_void;
    if (type == "bool" || type == "u8") return &ffi_type_uint8;
    if (type == "i8") return &ffi_type_sint8;
    if (type == "u16") return &ffi_type_uint16;
    if (type == "i16") return &ffi_type_sint16;
    if (type == "u32") return &ffi_type_uint32;
    if (type == "i32") return &ffi_type_sint32;
    if (type == "u64") return &ffi_type_uint64;
    if (type == "i64") return &ffi_type_sint64;
    if (type == "f32") return &ffi_type_float;
    if (type == "f64") return &ffi_type_double;
    return &ffi_type_pointer;
}

inline NiftFfiIntegerType nift_ffi_integer_type(const std::string& type) {
    if (type == "bool") return NiftFfiIntegerType::Bool;
    if (type == "i8") return NiftFfiIntegerType::I8;
    if (type == "u8") return NiftFfiIntegerType::U8;
    if (type == "i16") return NiftFfiIntegerType::I16;
    if (type == "u16") return NiftFfiIntegerType::U16;
    if (type == "i32") return NiftFfiIntegerType::I32;
    if (type == "u32") return NiftFfiIntegerType::U32;
    if (type == "i64") return NiftFfiIntegerType::I64;
    return NiftFfiIntegerType::U64;
}

template <typename T>
inline void* nift_ffi_unsigned_return_storage(NiftFfiValue& value, T& exact) {
    if constexpr (sizeof(T) < sizeof(ffi_arg)) return &value.unsigned_word;
    return &exact;
}

template <typename T>
inline void* nift_ffi_signed_return_storage(NiftFfiValue& value, T& exact) {
    if constexpr (sizeof(T) < sizeof(ffi_sarg)) return &value.signed_word;
    return &exact;
}

inline void* nift_ffi_integer_return_storage(NiftFfiValue& value,
                                             NiftFfiIntegerType type) {
    switch (type) {
        case NiftFfiIntegerType::Bool:
        case NiftFfiIntegerType::U8:
            return &value.unsigned_word;
        case NiftFfiIntegerType::I8:
            return &value.signed_word;
        case NiftFfiIntegerType::U16:
            return nift_ffi_unsigned_return_storage(value, value.u16);
        case NiftFfiIntegerType::I16:
            return nift_ffi_signed_return_storage(value, value.i16);
        case NiftFfiIntegerType::U32:
            return nift_ffi_unsigned_return_storage(value, value.u32);
        case NiftFfiIntegerType::I32:
            return nift_ffi_signed_return_storage(value, value.i32);
        case NiftFfiIntegerType::U64:
            return nift_ffi_unsigned_return_storage(value, value.u64);
        case NiftFfiIntegerType::I64:
            return nift_ffi_signed_return_storage(value, value.i64);
    }
    return nullptr;
}

template <typename T>
inline std::uint64_t nift_ffi_read_unsigned_return(const NiftFfiValue& value,
                                                   const T& exact) {
    if constexpr (sizeof(T) < sizeof(ffi_arg)) return static_cast<T>(value.unsigned_word);
    return exact;
}

template <typename T>
inline std::int64_t nift_ffi_read_signed_return(const NiftFfiValue& value,
                                                const T& exact) {
    if constexpr (sizeof(T) < sizeof(ffi_sarg)) return static_cast<T>(value.signed_word);
    return exact;
}

inline std::uint64_t nift_ffi_read_unsigned_return(const NiftFfiValue& value,
                                                   NiftFfiIntegerType type) {
    switch (type) {
        case NiftFfiIntegerType::Bool:
        case NiftFfiIntegerType::U8:
            return static_cast<std::uint8_t>(value.unsigned_word);
        case NiftFfiIntegerType::U16:
            return nift_ffi_read_unsigned_return(value, value.u16);
        case NiftFfiIntegerType::U32:
            return nift_ffi_read_unsigned_return(value, value.u32);
        case NiftFfiIntegerType::U64:
            return nift_ffi_read_unsigned_return(value, value.u64);
        default:
            return 0;
    }
}

inline std::int64_t nift_ffi_read_signed_return(const NiftFfiValue& value,
                                                NiftFfiIntegerType type) {
    switch (type) {
        case NiftFfiIntegerType::I8:
            return static_cast<std::int8_t>(value.signed_word);
        case NiftFfiIntegerType::I16:
            return nift_ffi_read_signed_return(value, value.i16);
        case NiftFfiIntegerType::I32:
            return nift_ffi_read_signed_return(value, value.i32);
        case NiftFfiIntegerType::I64:
            return nift_ffi_read_signed_return(value, value.i64);
        default:
            return 0;
    }
}

inline ffi_status nift_ffi_struct_layout(const std::vector<ffi_type*>& fields,
                                         std::vector<std::size_t>& offsets,
                                         std::size_t& size) {
    std::vector<ffi_type*> elements(fields);
    elements.push_back(nullptr);
    ffi_type struct_type{};
    struct_type.type = FFI_TYPE_STRUCT;
    struct_type.elements = elements.data();
    offsets.resize(fields.size());
    const ffi_status status = ffi_get_struct_offsets(
        FFI_DEFAULT_ABI, &struct_type, offsets.empty() ? nullptr : offsets.data());
    size = struct_type.size;
    return status;
}

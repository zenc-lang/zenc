// SPDX-License-Identifier: MIT
#ifndef ZEN_CONSTANTS_H
#ifndef ZC_ALLOW_INTERNAL
#error "constants.h is internal to Zen C. Include the appropriate public header instead."
#endif

#define ZEN_CONSTANTS_H

/**
 * @file constants.h
 * @brief Type checking helpers, buffer size constants.
 */

#include "compat/c23_compat.h"
#include <string.h>

// Buffer sizes — enum for compile-time constant expression (array sizes, etc.)
// Use ZEN_CONSTEXPR for non-array-size constants.
enum
{
    MAX_TYPE_NAME_LEN = 1024,
    MAX_VAR_NAME_LEN = 256,
    MAX_FUNC_NAME_LEN = 1024,
    MAX_SHORT_MSG_LEN = 256,
    MAX_ERROR_MSG_LEN = 2048,
    MAX_MANGLED_NAME_LEN = 2048,
    MAX_PATH_LEN = 4096
};

// Type checking helpers

static inline bool str_is_int_type(const char *t)
{
    return t &&
           (strcmp(t, "int") == 0 || strcmp(t, "uint") == 0 || strcmp(t, "i8") == 0 ||
            strcmp(t, "I8") == 0 || strcmp(t, "u8") == 0 || strcmp(t, "U8") == 0 ||
            strcmp(t, "i16") == 0 || strcmp(t, "I16") == 0 || strcmp(t, "u16") == 0 ||
            strcmp(t, "U16") == 0 || strcmp(t, "i32") == 0 || strcmp(t, "I32") == 0 ||
            strcmp(t, "u32") == 0 || strcmp(t, "U32") == 0 || strcmp(t, "i64") == 0 ||
            strcmp(t, "I64") == 0 || strcmp(t, "u64") == 0 || strcmp(t, "U64") == 0 ||
            strcmp(t, "i128") == 0 || strcmp(t, "I128") == 0 || strcmp(t, "u128") == 0 ||
            strcmp(t, "U128") == 0 || strcmp(t, "byte") == 0 || strcmp(t, "rune") == 0 ||
            strcmp(t, "long") == 0 || strcmp(t, "short") == 0 || strcmp(t, "ulong") == 0 ||
            strcmp(t, "ushort") == 0 || strcmp(t, "int8_t") == 0 || strcmp(t, "uint8_t") == 0 ||
            strcmp(t, "int16_t") == 0 || strcmp(t, "uint16_t") == 0 || strcmp(t, "int32_t") == 0 ||
            strcmp(t, "uint32_t") == 0 || strcmp(t, "int64_t") == 0 || strcmp(t, "uint64_t") == 0);
}

static inline bool str_is_bool_type(const char *t)
{
    return t && strcmp(t, "bool") == 0;
}

static inline bool str_is_char_type(const char *t)
{
    return t && strcmp(t, "char") == 0;
}

static inline bool str_is_void_type(const char *t)
{
    return t && strcmp(t, "void") == 0;
}

static inline bool str_is_float_type(const char *t)
{
    return t && (strcmp(t, "float") == 0 || strcmp(t, "f32") == 0 || strcmp(t, "F32") == 0);
}

static inline bool str_is_double_type(const char *t)
{
    return t && (strcmp(t, "double") == 0 || strcmp(t, "f64") == 0 || strcmp(t, "F64") == 0);
}

static inline bool str_is_usize_type(const char *t)
{
    return t && (strcmp(t, "usize") == 0 || strcmp(t, "size_t") == 0);
}

static inline bool str_is_isize_type(const char *t)
{
    return t &&
           (strcmp(t, "isize") == 0 || strcmp(t, "ptrdiff_t") == 0 || strcmp(t, "ssize_t") == 0);
}

static inline bool str_is_basic_type(const char *t)
{
    return t && (str_is_int_type(t) || str_is_bool_type(t) || str_is_char_type(t) ||
                 str_is_void_type(t) || str_is_float_type(t) || str_is_double_type(t) ||
                 str_is_usize_type(t) || str_is_isize_type(t) || strcmp(t, "__auto_type") == 0);
}

static inline bool str_is_struct_prefix(const char *t)
{
    return t && strncmp(t, "struct ", 7) == 0;
}

static inline const char *str_strip_struct_prefix(const char *t)
{
    return str_is_struct_prefix(t) ? t + 7 : t;
}

static inline bool str_is_option_type(const char *t)
{
    return t && strncmp(t, "Option__", 8) == 0;
}

static inline bool str_is_result_type(const char *t)
{
    return t && strncmp(t, "Result__", 8) == 0;
}

static inline bool str_is_vec_type(const char *t)
{
    return t && strncmp(t, "Vec__", 5) == 0;
}

static inline bool str_is_slice_type(const char *t)
{
    return t && strncmp(t, "Slice__", 7) == 0;
}

#endif // ZEN_CONSTANTS_H


#ifndef ZEN_CONSTANTS_H
#define ZEN_CONSTANTS_H

// Buffer sizes
#define MAX_TYPE_NAME_LEN 256    ///< Max length for type name strings.
#define MAX_FUNC_NAME_LEN 512    ///< Max length for function names.
#define MAX_ERROR_MSG_LEN 1024   ///< Max length for error messages.
#define MAX_MANGLED_NAME_LEN 512 ///< Max length for mangled names (generics).
#define MAX_PATH_LEN 4096        ///< Max length for file paths.

// Type checking helpers

/**
 * @brief Checks if type is any integer type (signed or unsigned, any width).
 * Covers: int, uint, i8-i128, u8-u128, I8-I128, U8-U128, byte,
 *         int8_t-int64_t, uint8_t-uint64_t, long, short, ulong, ushort, rune.
 */
#define IS_INT_TYPE(t)                                                                             \
    ((t) &&                                                                                        \
     (strcmp((t), "int") == 0 || strcmp((t), "uint") == 0 || strcmp((t), "i8") == 0 ||             \
      strcmp((t), "I8") == 0 || strcmp((t), "u8") == 0 || strcmp((t), "U8") == 0 ||                \
      strcmp((t), "i16") == 0 || strcmp((t), "I16") == 0 || strcmp((t), "u16") == 0 ||             \
      strcmp((t), "U16") == 0 || strcmp((t), "i32") == 0 || strcmp((t), "I32") == 0 ||             \
      strcmp((t), "u32") == 0 || strcmp((t), "U32") == 0 || strcmp((t), "i64") == 0 ||             \
      strcmp((t), "I64") == 0 || strcmp((t), "u64") == 0 || strcmp((t), "U64") == 0 ||             \
      strcmp((t), "i128") == 0 || strcmp((t), "I128") == 0 || strcmp((t), "u128") == 0 ||          \
      strcmp((t), "U128") == 0 || strcmp((t), "byte") == 0 || strcmp((t), "rune") == 0 ||          \
      strcmp((t), "long") == 0 || strcmp((t), "short") == 0 || strcmp((t), "ulong") == 0 ||        \
      strcmp((t), "ushort") == 0 || strcmp((t), "int8_t") == 0 || strcmp((t), "uint8_t") == 0 ||   \
      strcmp((t), "int16_t") == 0 || strcmp((t), "uint16_t") == 0 ||                               \
      strcmp((t), "int32_t") == 0 || strcmp((t), "uint32_t") == 0 ||                               \
      strcmp((t), "int64_t") == 0 || strcmp((t), "uint64_t") == 0))

#define IS_BOOL_TYPE(t) ((t) && strcmp((t), "bool") == 0) ///< Checks if type is "bool".
#define IS_CHAR_TYPE(t) ((t) && strcmp((t), "char") == 0) ///< Checks if type is "char".
#define IS_VOID_TYPE(t) ((t) && strcmp((t), "void") == 0) ///< Checks if type is "void".

/**
 * @brief Checks if type is "float" or "f32"/"F32".
 */
#define IS_FLOAT_TYPE(t)                                                                           \
    ((t) && (strcmp((t), "float") == 0 || strcmp((t), "f32") == 0 || strcmp((t), "F32") == 0))

/**
 * @brief Checks if type is "double" or "f64"/"F64".
 */
#define IS_DOUBLE_TYPE(t)                                                                          \
    ((t) && (strcmp((t), "double") == 0 || strcmp((t), "f64") == 0 || strcmp((t), "F64") == 0))

/**
 * @brief Checks if type is "usize" or "size_t".
 */
#define IS_USIZE_TYPE(t) ((t) && (strcmp((t), "usize") == 0 || strcmp((t), "size_t") == 0))

/**
 * @brief Checks if type is "isize" or "ptrdiff_t" or "ssize_t".
 */
#define IS_ISIZE_TYPE(t)                                                                           \
    ((t) &&                                                                                        \
     (strcmp((t), "isize") == 0 || strcmp((t), "ptrdiff_t") == 0 || strcmp((t), "ssize_t") == 0))

/**
 * @brief Checks if type is a string type ("string", "char*", "const char*").
 */
#define IS_STRING_TYPE(t)                                                                          \
    ((t) &&                                                                                        \
     (strcmp((t), "string") == 0 || strcmp((t), "char*") == 0 || strcmp((t), "const char*") == 0))

// Composite type checks
/**
 * @brief Checks if type is a basic primitive type.
 */
#define IS_BASIC_TYPE(t)                                                                           \
    ((t) && (IS_INT_TYPE(t) || IS_BOOL_TYPE(t) || IS_CHAR_TYPE(t) || IS_VOID_TYPE(t) ||            \
             IS_FLOAT_TYPE(t) || IS_DOUBLE_TYPE(t) || IS_USIZE_TYPE(t) || IS_ISIZE_TYPE(t) ||      \
             strcmp((t), "__auto_type") == 0))

/**
 * @brief Checks if type is numeric (int, float, double, usize, isize).
 */
#define IS_NUMERIC_TYPE(t)                                                                         \
    ((t) && (IS_INT_TYPE(t) || IS_FLOAT_TYPE(t) || IS_DOUBLE_TYPE(t) || IS_USIZE_TYPE(t) ||        \
             IS_ISIZE_TYPE(t)))

// Pointer type check
#define IS_PTR_TYPE(t) ((t) && strchr((t), '*') != NULL) ///< Checks if type string contains '*'.

// Struct prefix check
#define IS_STRUCT_PREFIX(t)                                                                        \
    ((t) && strncmp((t), "struct ", 7) == 0) ///< Checks if type starts with "struct ".
#define STRIP_STRUCT_PREFIX(t)                                                                     \
    (IS_STRUCT_PREFIX(t) ? ((t) + 7) : (t)) ///< Returns ptr to name after "struct " prefix.

// Generic type checks
#define IS_OPTION_TYPE(t)                                                                          \
    ((t) && strncmp((t), "Option__", 8) == 0) ///< Checks if type is Option<T>.
#define IS_RESULT_TYPE(t)                                                                          \
    ((t) && strncmp((t), "Result__", 8) == 0)                     ///< Checks if type is Result<T>.
#define IS_VEC_TYPE(t) ((t) && strncmp((t), "Vec__", 5) == 0)     ///< Checks if type is Vec<T>.
#define IS_SLICE_TYPE(t) ((t) && strncmp((t), "Slice__", 7) == 0) ///< Checks if type is Slice<T>.

#endif // ZEN_CONSTANTS_H

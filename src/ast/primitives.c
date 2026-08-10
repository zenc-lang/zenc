// SPDX-License-Identifier: MIT

#include "primitives.h"
#include <string.h>

static const ZenPrimitive primitives[] = {{"U0", TYPE_VOID, "void"},
                                          {"u0", TYPE_VOID, "void"},
                                          {"void", TYPE_VOID, "void"},
                                          {"bool", TYPE_BOOL, "bool"},
                                          {"char", TYPE_CHAR, "char"},
                                          {"I8", TYPE_I8, "int8_t"},
                                          {"i8", TYPE_I8, "int8_t"},
                                          {"int8", TYPE_I8, "int8_t"},
                                          {"U8", TYPE_U8, "uint8_t"},
                                          {"u8", TYPE_U8, "uint8_t"},
                                          {"uint8", TYPE_U8, "uint8_t"},
                                          {"byte", TYPE_BYTE, "uint8_t"},
                                          {"I16", TYPE_I16, "int16_t"},
                                          {"i16", TYPE_I16, "int16_t"},
                                          {"int16", TYPE_I16, "int16_t"},
                                          {"short", TYPE_I16, "int16_t"},
                                          {"U16", TYPE_U16, "uint16_t"},
                                          {"u16", TYPE_U16, "uint16_t"},
                                          {"uint16", TYPE_U16, "uint16_t"},
                                          {"ushort", TYPE_U16, "uint16_t"},
                                          {"I32", TYPE_I32, "int32_t"},
                                          {"i32", TYPE_I32, "int32_t"},
                                          {"int32", TYPE_I32, "int32_t"},
                                          {"int", TYPE_I32, "int32_t"},
                                          {"U32", TYPE_U32, "uint32_t"},
                                          {"u32", TYPE_U32, "uint32_t"},
                                          {"uint32", TYPE_U32, "uint32_t"},
                                          {"uint", TYPE_U32, "uint32_t"},
                                          {"rune", TYPE_RUNE, "int32_t"},
                                          {"I64", TYPE_I64, "int64_t"},
                                          {"i64", TYPE_I64, "int64_t"},
                                          {"int64", TYPE_I64, "int64_t"},
                                          {"long", TYPE_I64, "int64_t"},
                                          {"U64", TYPE_U64, "uint64_t"},
                                          {"u64", TYPE_U64, "uint64_t"},
                                          {"uint64", TYPE_U64, "uint64_t"},
                                          {"ulong", TYPE_U64, "uint64_t"},
                                          {"unsigned", TYPE_U32, "uint32_t"},
                                          {"signed", TYPE_I32, "int32_t"},
                                          {"I128", TYPE_I128, "__int128"},
                                          {"U128", TYPE_U128, "unsigned __int128"},
                                          {"F32", TYPE_F32, "float"},
                                          {"f32", TYPE_F32, "float"},
                                          {"float", TYPE_F32, "float"},
                                          {"F64", TYPE_F64, "double"},
                                          {"f64", TYPE_F64, "double"},
                                          {"double", TYPE_F64, "double"},
                                          {"f128", TYPE_F64, "__float128"},
                                          {"usize", TYPE_USIZE, "size_t"},
                                          {"isize", TYPE_ISIZE, "ptrdiff_t"},
                                          {"ssize_t", TYPE_ISIZE, "ssize_t"},
                                          {"c_int", TYPE_C_INT, "int"},
                                          {"c_uint", TYPE_C_UINT, "unsigned int"},
                                          {"c_long", TYPE_C_LONG, "long"},
                                          {"c_ulong", TYPE_C_ULONG, "unsigned long"},
                                          {"c_longlong", TYPE_C_LONGLONG, "long long"},
                                          {"c_ulonglong", TYPE_C_ULONGLONG, "unsigned long long"},
                                          {"c_short", TYPE_C_SHORT, "short"},
                                          {"c_ushort", TYPE_C_USHORT, "unsigned short"},
                                          {"c_char", TYPE_C_CHAR, "char"},
                                          {"c_uchar", TYPE_C_UCHAR, "unsigned char"},
                                          {"string", TYPE_STRING, "char*"}};

const ZenPrimitive *get_zen_primitives(int *count)
{
    if (count)
    {
        *count = sizeof(primitives) / sizeof(primitives[0]);
    }
    return primitives;
}

const ZenPrimitive *find_primitive_by_name(const char *name)
{
    int count = sizeof(primitives) / sizeof(primitives[0]);
    for (int i = 0; i < count; i++)
    {
        if (strcmp(primitives[i].zen_name, name) == 0)
        {
            return &primitives[i];
        }
    }
    return NULL;
}

const ZenPrimitive *find_primitive_by_c_name(const char *c_name)
{
    int count = sizeof(primitives) / sizeof(primitives[0]);
    for (int i = 0; i < count; i++)
    {
        if (strcmp(primitives[i].c_name, c_name) == 0)
        {
            return &primitives[i];
        }
    }
    return NULL;
}

const char *get_primitive_c_name(const char *name)
{
    const ZenPrimitive *p = find_primitive_by_name(name);
    return p ? p->c_name : name;
}

TypeKind find_primitive_kind(const char *name)
{
    const ZenPrimitive *p = find_primitive_by_name(name);
    if (p)
    {
        return p->kind;
    }
    p = find_primitive_by_c_name(name);
    if (p)
    {
        return p->kind;
    }
    return TYPE_UNKNOWN;
}

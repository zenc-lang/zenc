// SPDX-License-Identifier: MIT

/**
 * @file primitives.h
 * @brief Primitive type definitions and lookup tables.
 */

#ifndef ZC_ALLOW_INTERNAL
#error "ast/primitives.h is internal to Zen C. Include the appropriate public header instead."
#endif

#ifndef PRIMITIVES_H
#define PRIMITIVES_H

#include "ast.h"

typedef struct
{
    const char *zen_name;
    TypeKind kind;
    const char *c_name;
} ZenPrimitive;

/**
 * @brief Returns the table of Zen primitives.
 */
const ZenPrimitive *get_zen_primitives(int *count);

/**
 * @brief Finds a primitive by its Zen name.
 */
const ZenPrimitive *find_primitive_by_name(const char *name);

/**
 * @brief Finds a primitive by its C name.
 */
const ZenPrimitive *find_primitive_by_c_name(const char *c_name);

/**
 * @brief Normalizes a Zen type name to its C equivalent.
 */
const char *get_primitive_c_name(const char *name);

/**
 * @brief Returns the TypeKind for a given primitive name (Zen or C).
 */
TypeKind find_primitive_kind(const char *name);

#endif

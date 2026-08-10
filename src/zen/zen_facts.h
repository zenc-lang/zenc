// SPDX-License-Identifier: MIT

#ifndef ZC_ALLOW_INTERNAL
#error "zen/zen_facts.h is internal to Zen C. Include the appropriate public header instead."
#endif

#ifndef ZEN_FACTS_H
#define ZEN_FACTS_H

#include "../zprep.h"
#include "../compiler_config.h"
struct Token;

/**
 * @brief Triggers for the (removed) Zen facts easter-egg system.
 *
 * Kept so the parser's `hook_zen_trigger` field retains its type; the hook is
 * never installed anymore (the feature was dropped), so these are unused.
 */
typedef enum
{
    TRIGGER_GOTO,           ///< Usage of `goto`.
    TRIGGER_POINTER_ARITH,  ///< Pointer arithmetic usage.
    TRIGGER_BITWISE,        ///< Bitwise operations.
    TRIGGER_RECURSION,      ///< Recursive calls (currently manual trigger).
    TRIGGER_TERNARY,        ///< Ternary operator usage.
    TRIGGER_ASM,            ///< Inline assembly.
    TRIGGER_WHILE_TRUE,     ///< `while(true)` loops.
    TRIGGER_MACRO,          ///< Macro definitions.
    TRIGGER_VOID_PTR,       ///< `void*` usage.
    TRIGGER_MAIN,           ///< Compilation of `main` function.
    TRIGGER_FORMAT_STRING,  ///< F-string usage.
    TRIGGER_STRUCT_PADDING, ///< Implicit padding detection.
    TRIGGER_GLOBAL          ///< Global variables.
} ZenTrigger;

#endif

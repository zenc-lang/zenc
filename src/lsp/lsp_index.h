// SPDX-License-Identifier: MIT

#ifndef ZC_ALLOW_INTERNAL
#error "lsp/lsp_index.h is internal to Zen C. Include the appropriate public header instead."
#endif

#ifndef LSP_INDEX_H
#define LSP_INDEX_H

#include "parser.h"

/**
 * @brief Type of an indexed AST range.
 */
typedef enum
{
    RANGE_DEFINITION, ///< Defines a symbol.
    RANGE_REFERENCE   ///< References a symbol.
} RangeType;

/**
 * @brief A range in the source code mapping to semantic info.
 */
typedef struct LSPRange
{
    int start_line;   ///< Start line (1-based).
    int start_col;    ///< Start column (1-based).
    int end_line;     ///< End line.
    int end_col;      ///< End column (approximated).
    RangeType kind;   ///< Type of range (def or ref).
    int def_line;     ///< Line of definition (if reference).
    int def_col;      ///< Column of definition (if reference).
    char *hover_text; ///< Tooltip text / signature.
    ASTNode *node;    ///< Associated AST node.
    struct LSPRange *next;
} LSPRange;

/**
 * @brief Index of a single file.
 */
typedef struct LSPIndex
{
    LSPRange *head; ///< First range in the file.
    LSPRange *tail; ///< Last range in the file.
} LSPIndex;

// API.
LSPIndex *lsp_index_new(void);
void lsp_index_free(LSPIndex *idx);
void lsp_index_add_def(LSPIndex *idx, Token t, const char *hover, ASTNode *node);
void lsp_index_add_def_range(LSPIndex *idx, Token start, Token end, const char *hover,
                             ASTNode *node);
void lsp_index_add_ref(LSPIndex *idx, Token t, Token def_t, ASTNode *node);
LSPRange *lsp_find_at(LSPIndex *idx, int line, int col);

// Walker.
void lsp_build_index(LSPIndex *idx, ASTNode *root);

#endif

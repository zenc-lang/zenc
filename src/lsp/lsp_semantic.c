// SPDX-License-Identifier: MIT
#include "../constants.h"
#include "../ast/primitives.h"
#include "lsp_project.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Legend indices
#define TOKEN_TYPE_VARIABLE 0
#define TOKEN_TYPE_FUNCTION 1
#define TOKEN_TYPE_STRUCT 2
#define TOKEN_TYPE_KEYWORD 3
#define TOKEN_TYPE_STRING 4
#define TOKEN_TYPE_NUMBER 5
#define TOKEN_TYPE_COMMENT 6
#define TOKEN_TYPE_TYPE 7
#define TOKEN_TYPE_ENUM 8
#define TOKEN_TYPE_MEMBER 9
#define TOKEN_TYPE_OPERATOR 10
#define TOKEN_TYPE_PARAMETER 11
#define TOKEN_TYPE_MACRO 12
#define TOKEN_TYPE_TYPE_PARAMETER 13

typedef struct
{
    int line;
    int col;
    int length;
    int token_type;
    int token_modifiers;
} SemanticToken;

typedef struct
{
    SemanticToken *tokens;
    int count;
    int capacity;
} TokenBuilder;

static void builder_init(TokenBuilder *b)
{
    b->count = 0;
    b->capacity = 4096;
    b->tokens = malloc(sizeof(SemanticToken) * (size_t)(b->capacity));
}

static void builder_push(TokenBuilder *b, int line, int col, int length, int type, int modifiers)
{
    if (line < 0 || col < 0)
    {
        return;
    }
    if (b->count >= b->capacity)
    {
        b->capacity *= 2;
        SemanticToken *new_tokens =
            realloc(b->tokens, sizeof(SemanticToken) * (size_t)(b->capacity));
        if (!new_tokens)
        {
            return;
        }
        b->tokens = new_tokens;
    }
    SemanticToken *t = &b->tokens[b->count++];
    t->line = line;
    t->col = col;
    t->length = length;
    t->token_type = type;
    t->token_modifiers = modifiers;
}

static void builder_free(TokenBuilder *b)
{
    (void)b;
    zfree(b->tokens);
}

static int compare_tokens(const void *a, const void *b)
{
    const SemanticToken *ta = (const SemanticToken *)a;
    const SemanticToken *tb = (const SemanticToken *)b;
    if (ta->line != tb->line)
    {
        return ta->line - tb->line;
    }
    return ta->col - tb->col;
}

// Keyword check: reuse parser's is_reserved_keyword() + primitives table.
static int is_keyword(const char *w, size_t len)
{
    // Parser's reserved keyword check (covers pseudo_keywords like fn, let,
    // struct, return, if, else, while, break, continue, loop, for, do, etc.)
    Token t = {.type = TOK_IDENT, .start = w, .len = len};
    if (is_reserved_keyword(t))
    {
        return 1;
    }

    // Token-typed keywords that the parser maps to special tokens
    // (these are not checked by is_reserved_keyword with TOK_IDENT):
    //   test, assert, sizeof, def, defer, autofree, use, trait, impl
    //   and, or, not, comptime, union, asm, volatile, async, await, alias, opaque
    // Plus a few extra Zen keywords that don't fall into the above categories:
    //   import, module, extern, inline, noreturn, public, private, typeof,
    //   switch, match, external, true, false, null, in, union
    // Plus C interop: extern, inline, noreturn
    static const char *more[] = {"and",     "asm",       "alias",   "assert",   "async",
                                 "await",   "autofree",  "comptime","def",      "defer",
                                 "expect",  "extern",    "false",   "import",   "inline",
                                 "match",   "module",    "noreturn","not",      "null",
                                 "opaque",  "or",        "private", "public",   "sizeof",
                                 "switch",  "test",      "true",    "typeof",   "union",
                                 "use",     "volatile",  "impl",    "trait",    "in",
                                 "external","continue",
                                 NULL};
    for (size_t i = 0; more[i]; i++)
    {
        size_t klen = strlen(more[i]);
        if (len == klen && strncmp(w, more[i], len) == 0)
        {
            return 1;
        }
    }

    // Primitive type names — authoritative list from primitives.c
    int type_count = 0;
    const ZenPrimitive *types = get_zen_primitives(&type_count);
    for (int i = 0; i < type_count; i++)
    {
        size_t klen = strlen(types[i].zen_name);
        if (len == klen && strncmp(w, types[i].zen_name, len) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Lex source for keyword tokens and push them into the builder.
// This captures keywords that are not represented as separate AST nodes.
static void emit_keywords(TokenBuilder *b, const char *source)
{
    if (!source)
    {
        return;
    }
    const char *p = source;
    while (*p)
    {
        // Skip non-word characters
        if (!isalnum((unsigned char)*p) && *p != '_')
        {
            p++;
            continue;
        }
        const char *start = p;
        while (isalnum((unsigned char)*p) || *p == '_')
        {
            p++;
        }
        size_t len = (size_t)(p - start);

        if (is_keyword(start, len))
        {
            // Compute line/col by scanning from start of source
            int line = 0, col = 0;
            const char *scan = source;
            while (scan < start)
            {
                if (*scan == '\n')
                {
                    line++;
                    col = 0;
                }
                else
                {
                    col++;
                }
                scan++;
            }
            builder_push(b, line, col, (int)len, TOKEN_TYPE_KEYWORD, 0);
        }
    }
}

// AST Traversal
static void traverse_node(TokenBuilder *b, ASTNode *node, int depth)
{
    if (!node || depth > 32)
    {
        return;
    }

    switch (node->type)
    {
    case NODE_FUNCTION:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_FUNCTION, 1);
        }
        // Parameters
        for (int i = 0; i < node->func.arg_count; i++)
        {
            Attribute *attr = node->func.attributes;
            while (attr)
            {
                attr = attr->next;
            }
        }
        traverse_node(b, node->func.body, depth + 1);
        break;

    case NODE_VAR_DECL:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_VARIABLE, 0);
        }
        traverse_node(b, node->var_decl.init_expr, depth + 1);
        break;

    case NODE_BLOCK:
    {
        ASTNode *stmt = node->block.statements;
        while (stmt)
        {
            traverse_node(b, stmt, depth + 1);
            stmt = stmt->next;
        }
        break;
    }

    case NODE_RETURN:
        traverse_node(b, node->ret.value, depth + 1);
        break;

    case NODE_EXPR_BINARY:
        traverse_node(b, node->binary.left, depth + 1);
        traverse_node(b, node->binary.right, depth + 1);
        break;

    case NODE_EXPR_CALL:
        traverse_node(b, node->call.callee, depth + 1);
        {
            ASTNode *arg = node->call.args;
            while (arg)
            {
                traverse_node(b, arg, depth + 1);
                arg = arg->next;
            }
        }
        break;

    case NODE_CONST:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_VARIABLE, 2);
        }
        traverse_node(b, node->var_decl.init_expr, depth + 1);
        break;

    case NODE_TYPE_ALIAS:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_TYPE, 0);
        }
        break;

    case NODE_EXPR_VAR:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_VARIABLE, 0);
        }
        break;

    case NODE_STRUCT:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_STRUCT, 0);
        }
        traverse_node(b, node->strct.fields, depth + 1);
        break;

    case NODE_FIELD:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_MEMBER, 0);
        }
        break;

    case NODE_EXPR_MEMBER:
        traverse_node(b, node->member.target, depth + 1);
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_MEMBER, 0);
        }
        break;

    case NODE_EXPR_LITERAL:
        if (node->token.type != TOK_EOF)
        {
            if (node->literal.type_kind == LITERAL_STRING)
            {
                builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                             TOKEN_TYPE_STRING, 0);
            }
            else if (node->literal.type_kind == LITERAL_INT ||
                     node->literal.type_kind == LITERAL_FLOAT)
            {
                builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                             TOKEN_TYPE_NUMBER, 0);
            }
            else if (node->literal.type_kind == LITERAL_CHAR)
            {
                builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                             TOKEN_TYPE_STRING, 0);
            }
        }
        break;

    case NODE_TRAIT:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_STRUCT, 0);
        }
        traverse_node(b, node->trait.methods, depth + 1);
        break;

    case NODE_IMPL:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_STRUCT, 0);
        }
        traverse_node(b, node->impl.methods, depth + 1);
        break;

    case NODE_IMPL_TRAIT:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_STRUCT, 0);
        }
        traverse_node(b, node->impl_trait.methods, depth + 1);
        break;

    case NODE_ENUM:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_ENUM, 0);
        }
        traverse_node(b, node->enm.variants, depth + 1);
        break;

    case NODE_ENUM_VARIANT:
        if (node->token.type != TOK_EOF)
        {
            builder_push(b, node->token.line - 1, node->token.col - 1, (int)(node->token.len),
                         TOKEN_TYPE_ENUM, 0);
        }
        if (node->variant.payload)
        {
            // If we had a way to traverse types with tokens...
        }
        break;

    case NODE_DESTRUCT_VAR:
        traverse_node(b, node->destruct.init_expr, depth + 1);
        traverse_node(b, node->destruct.else_block, depth + 1);
        break;

    case NODE_MATCH_CASE:
        traverse_node(b, node->match_case.guard, depth + 1);
        traverse_node(b, node->match_case.body, depth + 1);
        break;

    case NODE_LAMBDA:
        traverse_node(b, node->lambda.body, depth + 1);
        break;

    case NODE_FOR_RANGE:
        traverse_node(b, node->for_range.start, depth + 1);
        traverse_node(b, node->for_range.end, depth + 1);
        traverse_node(b, node->for_range.body, depth + 1);
        break;

    default:
        if (node->type == NODE_ROOT)
        {
            traverse_node(b, node->root.children, depth + 1);
        }
        else if (node->next)
        {
            // Fallback to next
        }
        break;
    }
}

char *lsp_semantic_tokens_full(const char *uri)
{
    ProjectFile *pf = lsp_project_get_file(uri);
    if (!pf || !pf->ast)
    {
        return xstrdup("{\"data\":[]}");
    }

    TokenBuilder b;
    builder_init(&b);

    // Emit keyword tokens from source (keywords not captured by AST traversal)
    emit_keywords(&b, pf->source);

    ASTNode *root = pf->ast;
    while (root)
    {
        traverse_node(&b, root, 0);
        root = root->next;
    }

    qsort(b.tokens, (size_t)(b.count), sizeof(SemanticToken), compare_tokens);

    cJSON *root_json = cJSON_CreateObject();
    cJSON *data = cJSON_CreateArray();

    int prev_line = 0;
    int prev_col = 0;

    for (int i = 0; i < b.count; i++)
    {
        SemanticToken t = b.tokens[i];

        if (i > 0 && t.line == b.tokens[i - 1].line && t.col == b.tokens[i - 1].col)
        {
            continue;
        }

        int delta_line = t.line - prev_line;
        int delta_col = (delta_line == 0) ? (t.col - prev_col) : t.col;

        cJSON_AddItemToArray(data, cJSON_CreateNumber(delta_line));
        cJSON_AddItemToArray(data, cJSON_CreateNumber(delta_col));
        cJSON_AddItemToArray(data, cJSON_CreateNumber(t.length));
        cJSON_AddItemToArray(data, cJSON_CreateNumber(t.token_type));
        cJSON_AddItemToArray(data, cJSON_CreateNumber(t.token_modifiers));

        prev_line = t.line;
        prev_col = t.col;
    }

    cJSON_AddItemToObject(root_json, "data", data);

    char *json_str = cJSON_PrintUnformatted(root_json);
    cJSON_Delete(root_json);
    builder_free(&b);

    return json_str;
}

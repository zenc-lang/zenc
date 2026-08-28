// SPDX-License-Identifier: MIT
#include "plugins/plugin_manager.h"
#include "parser.h"
#include "utils/format_expr.h"
#include "utils/colors.h"
#include "utils/utils.h"
#include "constants.h"
#include "ast/primitives.h"
#include <ctype.h>
#include "analysis/const_fold.h"
#include <stdlib.h>
#include <string.h>

// Local parser context — avoids g_parser_ctx dependency.
// Set via token_set_parser_ctx() during compilation setup.
ParserContext *token_parser_ctx = NULL;

void token_set_parser_ctx(ParserContext *ctx)
{
    token_parser_ctx = ctx;
}

Token z_parse_expect(Lexer *l, ZenTokenType type, const char *msg)
{
    Token t = lexer_next(l);
    if (t.kind != type)
    {
        zpanic_at(t, "Expected %s, but got '%.*s'", msg, (int)(t.len), t.start);
        // Fault-tolerant (LSP) mode: resume as if the expected token was found.
        return (Token){type, t.start, 0, t.line, t.col, t.file};
    }
    return t;
}

int is_primitive_type_name(const char *name)
{
    return find_primitive_kind(name) != TYPE_UNKNOWN;
}

TypeKind get_primitive_type_kind(const char *name)
{
    return find_primitive_kind(name);
}

char *ast_to_string_recursive(ASTNode *node, int depth);

char *ast_to_string(ASTNode *node)
{
    return ast_to_string_recursive(node, 0);
}

char *ast_to_string_recursive(ASTNode *node, int depth)
{
    const int MAX_DEPTH = 32;
    if (!node || depth > MAX_DEPTH)
    {
        return xstrdup(depth > MAX_DEPTH ? "..." : "");
    }

    size_t buf_size = MAX_PATH_LEN;
    char *buf = xmalloc((size_t)(buf_size));
    buf[0] = 0;

    switch (node->kind)
    {
    case NODE_EXPR_LITERAL:
        if (node->literal.kind == LITERAL_INT)
        {
            snprintf(buf, buf_size, "%llu", node->literal.int_val);
        }
        else if (node->literal.kind == LITERAL_FLOAT)
        {
            snprintf(buf, buf_size, "%f", node->literal.float_val);
        }
        else if (node->literal.kind == LITERAL_STRING)
        {
            size_t s_len = strlen(node->literal.string_val);
            size_t required = s_len + 16;
            if (required > buf_size)
            {
                char *new_buf = xrealloc(buf, required);
                buf = new_buf;
                buf_size = required;
            }
            snprintf(buf, buf_size, "\"%s\"", node->literal.string_val);
        }
        else if (node->literal.kind == LITERAL_CHAR)
        {
            if (node->literal.int_val == '\'')
            {
                snprintf(buf, buf_size, "'\\''");
            }
            else if (node->literal.int_val == '\n')
            {
                snprintf(buf, buf_size, "'\\n'");
            }
            else if (node->literal.int_val == '\\')
            {
                snprintf(buf, buf_size, "'\\\\'");
            }
            else if (node->literal.int_val == '\0')
            {
                snprintf(buf, buf_size, "'\\0'");
            }
            else
            {
                snprintf(buf, buf_size, "'%c'", (char)node->literal.int_val);
            }
        }
        break;
    case NODE_EXPR_VAR:
        snprintf(buf, buf_size, "%s", node->var_ref.name ? node->var_ref.name : "");
        break;
    case NODE_EXPR_BINARY:
    {
        char *l = ast_to_string_recursive(node->binary.left, depth + 1);
        char *r = ast_to_string_recursive(node->binary.right, depth + 1);
        snprintf(buf, buf_size, "(%s %s %s)", l, node->binary.op ? node->binary.op : "?", r);
        zfree(l);
        zfree(r);
        break;
    }
    case NODE_EXPR_UNARY:
    {
        char *o = ast_to_string_recursive(node->unary.operand, depth + 1);
        snprintf(buf, buf_size, "(%s%s)", node->unary.op ? node->unary.op : "?", o);
        zfree(o);
        break;
    }
    case NODE_EXPR_CAST:
    {
        char *e = ast_to_string_recursive(node->cast.expr, depth + 1);
        snprintf(buf, buf_size, "((%s)%s)", node->cast.target_type ? node->cast.target_type : "?",
                 e);
        zfree(e);
        break;
    }
    case NODE_EXPR_CALL:
    {
        char *callee = ast_to_string_recursive(node->call.callee, depth + 1);
        snprintf(buf, buf_size, "%s(", callee);
        zfree(callee);

        ASTNode *arg = node->call.args;
        int first = 1;
        while (arg)
        {
            if (!first)
            {
                if (strlen(buf) + 4 < buf_size)
                {
                    strcat(buf, ", ");
                }
            }
            char *a = ast_to_string_recursive(arg, depth + 1);
            if (strlen(buf) + strlen(a) + 4 < buf_size)
            {
                strcat(buf, a);
            }
            zfree(a);
            first = 0;
            arg = arg->next;
        }
        if (strlen(buf) + 2 < buf_size)
        {
            strcat(buf, ")");
        }
        break;
    }
    case NODE_EXPR_STRUCT_INIT:
    {
        char *name = node->struct_init.struct_name;
        snprintf(buf, buf_size, "%s{", name ? name : "?");

        ASTNode *field = node->struct_init.fields;
        int first = 1;
        while (field)
        {
            if (!first)
            {
                if (strlen(buf) + 4 < buf_size)
                {
                    strcat(buf, ", ");
                }
            }
            if (field->kind == NODE_VAR_DECL)
            {
                if (strlen(buf) + (field->var_decl.name ? strlen(field->var_decl.name) : 0) + 4 <
                    buf_size)
                {
                    strcat(buf, field->var_decl.name ? field->var_decl.name : "?");
                    strcat(buf, ": ");
                }
                char *val = ast_to_string_recursive(field->var_decl.init_expr, depth + 1);
                if (strlen(buf) + strlen(val) + 2 < buf_size)
                {
                    strcat(buf, val);
                }
                zfree(val);
            }
            first = 0;
            field = field->next;
        }
        if (strlen(buf) + 2 < buf_size)
        {
            strcat(buf, "}");
        }
        break;
    }
    case NODE_EXPR_MEMBER:
    {
        char *t = ast_to_string_recursive(node->member.target, depth + 1);
        snprintf(buf, buf_size, "%s.%s", t, node->member.field ? node->member.field : "?");
        zfree(t);
        break;
    }
    case NODE_EXPR_INDEX:
    {
        char *arr = ast_to_string_recursive(node->index.array, depth + 1);
        char *idx = ast_to_string_recursive(node->index.index, depth + 1);
        snprintf(buf, buf_size, "%s[%s]", arr, idx);
        zfree(arr);
        zfree(idx);
        break;
    }
    default:
        snprintf(buf, buf_size, "<expr>");
        break;
    }
    return buf;
}

int is_token(Token t, const char *s)
{
    size_t len = strlen(s);
    return ((size_t)t.len == len && strncmp(t.start, s, (size_t)(len)) == 0);
}

char *token_strdup(Token t)
{
    char *s = xmalloc(t.len + 1);
    strncpy(s, t.start, t.len);
    s[t.len] = 0;
    return s;
}

char *token_get_string_content(Token t)
{
    int is_multi = 0;
    int is_fstring = (t.kind == TOK_FSTRING);
    int is_raw = (t.kind == TOK_RAW_STRING);
    int start_offset = 1;
    int end_offset = 1;

    if (is_fstring)
    {
        is_multi = (t.len >= 7 && t.start[1] == '"' && t.start[2] == '"' && t.start[3] == '"');
        start_offset = is_multi ? 4 : 2;
        end_offset = is_multi ? 3 : 1;
    }
    else if (is_raw)
    {
        is_multi = (t.len >= 7 && t.start[1] == '"' && t.start[2] == '"' && t.start[3] == '"');
        start_offset = is_multi ? 4 : 2;
        end_offset = is_multi ? 3 : 1;
    }
    else
    {
        is_multi = (t.len >= 6 && t.start[0] == '"' && t.start[1] == '"' && t.start[2] == '"');
        start_offset = is_multi ? 3 : 1;
        end_offset = is_multi ? 3 : 1;
    }

    int content_len = (int)(t.len) - start_offset - end_offset;
    if (content_len < 0)
    {
        content_len = 0;
    }

    char *content = xmalloc((size_t)(content_len + 1));
    strncpy(content, t.start + start_offset, (size_t)(content_len));
    content[content_len] = '\0';
    return content;
}

void skip_comments(Lexer *l)
{
    int prev_emit = l->emit_comments;
    l->emit_comments = 1;
    while (lexer_peek(l).kind == TOK_COMMENT)
    {
        Token tk = lexer_next(l);
        if (token_parser_ctx->config->keep_comments)
        {
            if (token_parser_ctx->last_doc_comment)
            {
                size_t old_len = strlen(token_parser_ctx->last_doc_comment);
                char *new_c = xmalloc(old_len + tk.len + 2);
                sprintf(new_c, "%s\n%.*s", token_parser_ctx->last_doc_comment, (int)(tk.len),
                        tk.start); /* safe */
                zfree(token_parser_ctx->last_doc_comment);
                token_parser_ctx->last_doc_comment = new_c;
            }
            else
            {
                token_parser_ctx->last_doc_comment = xmalloc(tk.len + 1);
                strncpy(token_parser_ctx->last_doc_comment, tk.start, tk.len);
                token_parser_ctx->last_doc_comment[tk.len] = 0;
            }
        }
    }
    l->emit_comments = prev_emit;
}

static const char *C_RESERVED_WORDS[] = {
    "double",        "float",    "signed",   "unsigned",   "short",     "long",
    "auto",          "register", "switch",   "case",       "default",   "do",
    "goto",          "typedef",  "static",   "extern",     "volatile",  "inline",
    "restrict",      "sizeof",   "const",    "_Alignas",   "_Alignof",  "_Atomic",
    "_Bool",         "_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
    "_Thread_local", NULL};

int is_c_reserved_word(const char *name)
{
    for (int i = 0; C_RESERVED_WORDS[i] != NULL; i++)
    {
        if (strcmp(name, C_RESERVED_WORDS[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

void warn_c_reserved_word(Token t, const char *name)
{
    zwarn_at(t, "Identifier '%s' conflicts with C reserved word", name);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET
                               "This will cause compilation errors in the generated C code\n");
}

char *consume_until_semicolon(Lexer *l)
{
    const char *s = l->src + l->pos;
    int d = 0;
    while (1)
    {
        Token t = lexer_peek(l);
        if (t.kind == TOK_EOF)
        {
            break;
        }
        if (t.kind == TOK_LBRACE || t.kind == TOK_LPAREN || t.kind == TOK_LBRACKET)
        {
            d++;
        }
        if (t.kind == TOK_RBRACE || t.kind == TOK_RPAREN || t.kind == TOK_RBRACKET)
        {
            d--;
        }

        if (d == 0 && t.kind == TOK_SEMICOLON)
        {
            ptrdiff_t len = t.start - s;
            char *r = xmalloc((size_t)(len + 1));
            strncpy(r, s, (size_t)(len));
            r[len] = 0;
            lexer_next(l);
            return r;
        }
        lexer_next(l);
    }
    return xstrdup("");
}

const char *normalize_type_name(const char *name)
{
    if (!name)
    {
        return NULL;
    }

    return get_primitive_c_name(name);
}

int is_reserved_keyword(Token t)
{
    switch (t.kind)
    {
    case TOK_TEST:
    case TOK_ASSERT:
    case TOK_SIZEOF:
    case TOK_DEF:
    case TOK_DEFER:
    case TOK_AUTOFREE:
    case TOK_USE:
    case TOK_TRAIT:
    case TOK_IMPL:
    case TOK_AND:
    case TOK_OR:
    case TOK_FOR:
    case TOK_COMPTIME:
    case TOK_UNION:
    case TOK_ASM:
    case TOK_VOLATILE:
    case TOK_ASYNC:
    case TOK_AWAIT:
    case TOK_ALIAS:
    case TOK_OPAQUE:
        return 1;
    default:
        break;
    }

    if (t.kind == TOK_IDENT)
    {
        static const char *pseudo_keywords[] = {
            "let",      "static", "const",  "return", "if",    "else",   "while", "break",
            "continue", "loop",   "repeat", "unless", "guard", "launch", "do",    "goto",
            "plugin",   "fn",     "struct", "enum",   "self",  NULL};

        for (int i = 0; pseudo_keywords[i] != NULL; i++)
        {
            if (t.len == strlen(pseudo_keywords[i]) &&
                strncmp(t.start, pseudo_keywords[i], t.len) == 0)
            {
                return 1;
            }
        }
    }

    return 0;
}

void check_identifier(Token t)
{
    if (is_reserved_keyword(t))
    {
        char buf[MAX_SHORT_MSG_LEN];
        char name[64];
        int len = (int)(t.len < 63 ? t.len : 63);
        strncpy(name, t.start, (size_t)(len));
        name[len] = 0;
        snprintf(buf, sizeof(buf), "Cannot use reserved keyword '%s' as an identifier", name);
        zpanic_at(t, "%s", buf);
    }
}

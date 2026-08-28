// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "expr_internal.h"
#include "ast/ast.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTNode *parse_lambda(ParserContext *ctx, Lexer *l)
{
    Token lambda_token = lexer_next(l);

    int default_capture_mode = 0; // 0=Value, 1=Reference
    char **explicit_captures = xmalloc(sizeof(char *) * 32);
    int *explicit_capture_modes = xmalloc(sizeof(int) * 32);
    int explicit_capture_count = 0;

    if (lexer_peek(l).kind == TOK_LBRACKET)
    {
        lexer_next(l);

        while (lexer_peek(l).kind != TOK_RBRACKET && lexer_peek(l).kind != TOK_EOF)
        {
            if (lexer_peek(l).kind == TOK_OP && lexer_peek(l).len == 1 &&
                lexer_peek(l).start[0] == '&')
            {
                lexer_next(l);
                if (lexer_peek(l).kind == TOK_IDENT)
                {
                    explicit_captures[explicit_capture_count] = token_strdup(lexer_peek(l));
                    explicit_capture_modes[explicit_capture_count] = 1; // By-Reference
                    explicit_capture_count++;
                    lexer_next(l);
                }
                else
                {
                    default_capture_mode = 1;
                }
            }
            else if (lexer_peek(l).kind == TOK_OP && lexer_peek(l).len == 1 &&
                     lexer_peek(l).start[0] == '=')
            {
                default_capture_mode = 0;
                lexer_next(l);
            }
            else if (lexer_peek(l).kind == TOK_IDENT)
            {
                explicit_captures[explicit_capture_count] = token_strdup(lexer_peek(l));
                explicit_capture_modes[explicit_capture_count] = 0; // By-Value
                explicit_capture_count++;
                lexer_next(l);
            }
            else
            {
                zpanic_at(lexer_peek(l), "Invalid capture list item");
            }

            if (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
            }
            else
            {
                break;
            }
        }

        if (lexer_peek(l).kind != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ']' after capture list");
        }
        lexer_next(l); // eat ]
    }

    if (lexer_peek(l).kind != TOK_LPAREN)
    {
        zpanic_at(lexer_peek(l), "Expected '(' after 'fn' in lambda");
    }

    lexer_next(l);

    Type *t = type_new(TYPE_FUNCTION);
    t->args = xmalloc(sizeof(Type *) * 16);
    char **param_names = xmalloc(sizeof(char *) * 16);
    char **param_types = xmalloc(sizeof(char *) * 16);
    int count = 0;

    while (lexer_peek(l).kind != TOK_RPAREN)
    {
        if (count >= 16)
        {
            zpanic_at(lexer_peek(l), "Too many function parameters (max 16)");
            break;
        }

        if (count > 0)
        {
            if (lexer_peek(l).kind != TOK_COMMA)
            {
                zpanic_at(lexer_peek(l), "Expected ',' between parameters");
                break;
            }

            lexer_next(l);
        }

        Token name_tok = lexer_next(l);
        if (name_tok.kind != TOK_IDENT)
        {
            zpanic_at(name_tok, "Expected parameter name");
            break;
        }

        param_names[count] = token_strdup(name_tok);

        if (lexer_peek(l).kind != TOK_COLON)
        {
            zpanic_at(lexer_peek(l), "Expected ':' after parameter name");
            break;
        }

        lexer_next(l);

        Type *typef = parse_type_formal(ctx, l);
        if (!typef)
        {
            return NULL;
        }
        t->args[t->count] = typef;
        param_types[count] = type_to_string(typef);
        count++;
        t->count = count;
    }
    lexer_next(l);

    char *return_type = xstrdup("void");
    if (lexer_peek(l).kind == TOK_ARROW)
    {
        lexer_next(l);

        t->inner = parse_type_formal(ctx, l);
        if (!t->inner)
        {
            return NULL;
        }
        return_type = type_to_string(t->inner);
    }

    enter_scope(ctx);

    for (int i = 0; i < count; i++)
    {
        add_symbol(ctx, param_names[i], param_types[i], t->args[i], 0);
    }

    ASTNode *body = NULL;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        zpanic_at(lexer_peek(l), "Expected '{' for lambda body");
    }

    ASTNode *lambda = ast_create(NODE_LAMBDA);
    lambda->token = lambda_token;
    lambda->line = lambda_token.line;
    lambda->lambda.param_names = param_names;
    lambda->lambda.param_types = param_types;
    lambda->lambda.return_type = return_type;
    lambda->lambda.body = body;
    lambda->lambda.body = body;
    lambda->lambda.count = count;
    lambda->lambda.default_capture_mode = default_capture_mode;
    lambda->lambda.explicit_captures = explicit_captures;
    lambda->lambda.explicit_capture_modes = explicit_capture_modes;
    lambda->lambda.explicit_capture_count = explicit_capture_count;
    lambda->lambda.capture_modes = NULL; // Will be allocated in analysis
    lambda->lambda.lambda_id = ctx->lambda_counter++;
    lambda->lambda.is_expression = 0;
    lambda->type_info = t;
    lambda->resolved_type = type_to_string(t);
    if (ctx->known_generics_count == 0)
    {
        register_lambda(ctx, lambda);
    }
    analyze_lambda_captures(ctx, lambda);

    exit_scope(ctx);

    return lambda;
}

char *escape_c_string(const char *input)
{
    char *out = xmalloc(strlen(input) * 2 + 1);
    char *p = out;
    while (*input)
    {
        if (*input == '\\' && (input[1] == 'u' || input[1] == 'U') && input[2] == '{')
        {
            input += 3;
            uint32_t val = 0;
            while (*input && *input != '}')
            {
                val = (val << 4);
                if (*input >= '0' && *input <= '9')
                {
                    val += (uint32_t)(*input - '0');
                }
                else if (*input >= 'a' && *input <= 'f')
                {
                    val += (uint32_t)(*input - 'a' + 10);
                }
                else if (*input >= 'A' && *input <= 'F')
                {
                    val += (uint32_t)(*input - 'A' + 10);
                }
                input++;
            }
            if (*input == '}')
            {
                input++;
            }

            if (val < 128)
            {
                char c = (char)val;
                if (c == '\n')
                {
                    *p++ = '\\';
                    *p++ = 'n';
                }
                else if (c == '\r')
                {
                    *p++ = '\\';
                    *p++ = 'r';
                }
                else if (c == '\t')
                {
                    *p++ = '\\';
                    *p++ = 't';
                }
                else if (c == '"')
                {
                    *p++ = '\\';
                    *p++ = '"';
                }
                else if (c == '\\')
                {
                    *p++ = '\\';
                    *p++ = '\\';
                }
                else
                {
                    *p++ = c;
                }
            }
            else if (val < 0x800)
            {
                *p++ = (char)(0xC0 | (val >> 6));
                *p++ = (char)(0x80 | (val & 0x3F));
            }
            else if (val < 0x10000)
            {
                *p++ = (char)(0xE0 | (val >> 12));
                *p++ = (char)(0x80 | ((val >> 6) & 0x3F));
                *p++ = (char)(0x80 | (val & 0x3F));
            }
            else
            {
                *p++ = (char)(0xF0 | (val >> 18));
                *p++ = (char)(0x80 | ((val >> 12) & 0x3F));
                *p++ = (char)(0x80 | ((val >> 6) & 0x3F));
                *p++ = (char)(0x80 | (val & 0x3F));
            }
            continue;
        }

        if (*input == '\\')
        {
            *p++ = *input++;
            if (*input)
            {
                *p++ = *input++;
            }
        }
        else if (*input == '\n')
        {
            *p++ = '\\';
            *p++ = 'n';
            input++;
        }
        else if (*input == '\r')
        {
            *p++ = '\\';
            *p++ = 'r';
            input++;
        }
        else if (*input == '\t')
        {
            *p++ = '\\';
            *p++ = 't';
            input++;
        }
        else if (*input == '"')
        {
            *p++ = '\\';
            *p++ = '"';
            input++;
        }
        else
        {
            *p++ = *input++;
        }
    }
    *p = '\0';
    return out;
}

// Check if the suffix is a valid integer suffix
static int is_valid_int_suffix(const char *s)
{
    if (!s || !*s)
    {
        return 1;
    }
    // Standard and Zen C suffixes
    const char *valid[] = {"u",   "U",   "l",   "L",   "ul",    "UL",  "uL",  "Ul",  "lu",
                           "LU",  "lU",  "Lu",  "ll",  "LL",    "ull", "ULL", "uLL", "Ull",
                           "llu", "LLu", "lLu", "llU", "u8",    "u16", "u32", "u64", "usize",
                           "i8",  "i16", "i32", "i64", "isize", NULL};
    for (int i = 0; valid[i]; i++)
    {
        if (strcmp(s, valid[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int is_valid_float_suffix(const char *s)
{
    if (!s || !*s)
    {
        return 1;
    }
    const char *valid[] = {"f", "F", "d", "D", NULL};
    for (int i = 0; valid[i]; i++)
    {
        if (strcmp(s, valid[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Parse integer literal (decimal, hex, binary, octal)
ASTNode *parse_int_literal(ParserContext *ctx, Token t)
{
    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.kind = LITERAL_INT;
    node->type_info = type_new(TYPE_INT);
    char *s = token_strdup(t);
    int write_idx = 0;
    for (int read_idx = 0; s[read_idx]; read_idx++)
    {
        if (s[read_idx] != '_')
        {
            s[write_idx++] = s[read_idx];
        }
    }
    s[write_idx] = '\0';

    unsigned long long val;
    char *endptr = NULL;

    // Cap integer literal length to prevent overflow hangs (max safe decimal: 19 digits for
    // ULLONG_MAX)
    if (write_idx > 20)
    {
        val = 1048576;
        endptr = s + write_idx;
    }
    else if (t.len > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
    {
        val = strtoull(s + 2, &endptr, 2);
    }
    else if (t.len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        val = strtoull(s + 2, &endptr, 16);
    }
    else if (t.len > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
    {
        val = strtoull(s + 2, &endptr, 8);
    }
    else
    {
        val = strtoull(s, &endptr, 10);
    }

    // Validate and apply suffix
    if (endptr && *endptr)
    {
        if (!is_valid_int_suffix(endptr))
        {
            char err[MAX_SHORT_MSG_LEN];
            snprintf(err, sizeof(err), "Invalid integer literal suffix: '%s'", endptr);
            zpanic_at(t, "%s", err);
        }

        if (ctx->config->misra_mode && strchr(endptr, 'l'))
        {
            zerror_at(t, "MISRA Rule 7.3");
        }

        // Apply type from suffix
        if (strcasecmp(endptr, "u") == 0)
        {
            node->type_info->kind = TYPE_UINT;
        }
        else if (strcasecmp(endptr, "l") == 0)
        {
            node->type_info->kind = TYPE_C_LONG;
        }
        else if (strcasecmp(endptr, "ll") == 0)
        {
            node->type_info->kind = TYPE_C_LONGLONG;
        }
        else if (strcasecmp(endptr, "ul") == 0 || strcasecmp(endptr, "lu") == 0)
        {
            node->type_info->kind = TYPE_C_ULONG;
        }
        else if (strcasecmp(endptr, "ull") == 0 || strcasecmp(endptr, "llu") == 0)
        {
            node->type_info->kind = TYPE_C_ULONGLONG;
        }
        else if (strcmp(endptr, "u8") == 0)
        {
            node->type_info->kind = TYPE_U8;
        }
        else if (strcmp(endptr, "u16") == 0)
        {
            node->type_info->kind = TYPE_U16;
        }
        else if (strcmp(endptr, "u32") == 0)
        {
            node->type_info->kind = TYPE_U32;
        }
        else if (strcmp(endptr, "u64") == 0)
        {
            node->type_info->kind = TYPE_U64;
        }
        else if (strcmp(endptr, "usize") == 0)
        {
            node->type_info->kind = TYPE_USIZE;
        }
        else if (strcmp(endptr, "i8") == 0)
        {
            node->type_info->kind = TYPE_I8;
        }
        else if (strcmp(endptr, "i16") == 0)
        {
            node->type_info->kind = TYPE_I16;
        }
        else if (strcmp(endptr, "i32") == 0)
        {
            node->type_info->kind = TYPE_I32;
        }
        else if (strcmp(endptr, "i64") == 0)
        {
            node->type_info->kind = TYPE_I64;
        }
        else if (strcmp(endptr, "isize") == 0)
        {
            node->type_info->kind = TYPE_ISIZE;
        }

        // Rule 7.2: If it's hex/bin/octal and semantically unsigned (high bit set), it needs 'u'
        // This part is for when we ALREADY have a suffix (like 'L'), but it lacks 'U'.
        if (ctx->config->misra_mode && !(strchr(endptr, 'u') || strchr(endptr, 'U')))
        {
            int is_non_decimal = (t.len > 2 && s[0] == '0' &&
                                  (s[1] == 'x' || s[1] == 'X' || s[1] == 'b' || s[1] == 'B' ||
                                   s[1] == 'o' || s[1] == 'O'));
            if (is_non_decimal && val > 2147483647ULL)
            {
                zerror_at(t, "MISRA Rule 7.2");
            }
        }
    }
    else
    {
        // No suffix: default to int or i64 if too large
        if (val > 2147483647ULL)
        {
            node->type_info->kind = TYPE_I64;
        }

        if (ctx->config->misra_mode)
        {
            int is_non_decimal = (t.len > 2 && s[0] == '0' &&
                                  (s[1] == 'x' || s[1] == 'X' || s[1] == 'b' || s[1] == 'B' ||
                                   s[1] == 'o' || s[1] == 'O'));
            if (is_non_decimal && val > 2147483647ULL)
            {
                // Hex/Octal constants that are "implicitly" unsigned in C.
                zerror_at(t, "MISRA Rule 7.2");
            }
        }
    }

    node->literal.int_val = val;
    zfree(s);
    return node;
}

// Parse float literal
ASTNode *parse_float_literal(Token t)
{
    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.kind = LITERAL_FLOAT;
    char *s = token_strdup(t);
    int write_idx = 0;
    for (int read_idx = 0; s[read_idx]; read_idx++)
    {
        if (s[read_idx] != '_')
        {
            s[write_idx++] = s[read_idx];
        }
    }
    s[write_idx] = '\0';

    node->literal.float_val = atof(s);
    node->type_info = type_new(TYPE_F64);

    // Check for suffix
    char *endptr = NULL;
    strtod(s, &endptr);
    if (endptr && *endptr)
    {
        if (!is_valid_float_suffix(endptr))
        {
            char err[MAX_SHORT_MSG_LEN];
            snprintf(err, sizeof(err), "Invalid float literal suffix: '%s'", endptr);
            zpanic_at(t, "%s", err);
        }
        if (strcmp(endptr, "f") == 0 || strcmp(endptr, "F") == 0)
        {
            node->type_info->kind = TYPE_F32;
        }
    }

    zfree(s);
    return node;
}

// Parse string literal
ASTNode *parse_string_literal(ParserContext *ctx, Token t)
{
    char *content = token_get_string_content(t);
    int str_len = (int)strlen(content);

    // Check for implicit interpolation
    int has_interpolation = 0;
    for (int i = 0; i < str_len; i++)
    {
        if (content[i] == '{')
        {
            // Ignore if part of \u{ or \U{
            if (i >= 2 && (content[i - 1] == 'u' || content[i - 1] == 'U') &&
                content[i - 2] == '\\')
            {
                continue;
            }
            has_interpolation = 1;
            break;
        }
    }

    if (has_interpolation)
    {
        // ... (interpolation logic)
        char *code = process_printf_sugar(ctx, t, content, 0, "stdout", NULL, NULL, 0, 0, 1);

        ASTNode *node = ast_create(NODE_RAW_STMT);
        node->token = t;
        node->raw_stmt.content = code;
        node->type_info = type_new(TYPE_STRING);
        node->resolved_type = xstrdup("string");

        // Rule 4.1 check also for interpolated strings (though rarer)
        if (ctx->config->misra_mode)
        {
            for (int i = 0; i < str_len; i++)
            {
                if (content[i] == '\\' && i + 1 < str_len)
                {

                    // Hex
                    if (content[i + 1] == 'x')
                    {
                        if (i + 3 >= str_len)
                        {
                            warn_misra_violation(t, "MISRA Rule 4.1: Octal/hexadecimal escape "
                                                    "sequences shall not be used");
                        }
                        i += 2;
                        continue;
                    }

                    // Octal (starts with \0)
                    if (content[i + 1] == '0')
                    {
                        warn_misra_violation(
                            t,
                            "MISRA Rule 4.1: Octal/hexadecimal escape sequences shall not be used");
                    }
                    i++;
                }
            }
        }

        zfree(content);
        return node;
    }

    // Plain string literal (no interpolation)
    content = token_get_string_content(t);

    if (ctx->config->misra_mode)
    {
        for (int i = 0; i < str_len; i++)
        {
            if (content[i] == '\\' && i + 1 < str_len)
            {
                if (content[i + 1] == 'x')
                {
                    // C hex escapes consume ALL hex digits.
                    // To follow Rule 4.1, it should not be followed by a character that could be a
                    // hex digit if that character was intended to be literal. We check if there are
                    // MORE than 2 hex digits.
                    if (i + 2 < str_len && isxdigit(content[i + 2]))
                    {
                        if (i + 3 < str_len && isxdigit(content[i + 3]))
                        {
                            if (i + 4 < str_len && isxdigit(content[i + 4]))
                            {
                                // \xHHH... at least 3 digits. This is ambiguous/unterminated.
                                zerror_at(t, "MISRA Rule 4.1: Hex escape sequence with more than 2 "
                                             "digits is ambiguous");
                            }
                        }
                    }
                }
                else if (content[i + 1] >= '0' && content[i + 1] <= '7')
                {
                    // Octal escape \d, \dd, or \ddd.
                    int count = 1;
                    if (i + 2 < str_len && content[i + 2] >= '0' && content[i + 2] <= '7')
                    {
                        count++;
                        if (i + 3 < str_len && content[i + 3] >= '0' && content[i + 3] <= '7')
                        {
                            count++;
                        }
                    }
                    // If it's shorter than 3 digits but followed by an octal digit, it's ambiguous.
                    if (count < 3 && i + count + 1 < str_len && content[i + count + 1] >= '0' &&
                        content[i + count + 1] <= '7')
                    {
                        zerror_at(t,
                                  "MISRA Rule 4.1: Octal escape sequence followed by octal digit");
                    }
                }
            }
        }
    }

    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.kind = LITERAL_STRING;

    node->literal.string_val = escape_c_string(content);
    zfree(content);

    node->type_info = type_new(TYPE_STRING);
    return node;
}

// Parse f-string literal
ASTNode *parse_fstring_literal(ParserContext *ctx, Token t)
{
    char *content = token_get_string_content(t);
    // Use safe, unified interpolation logic. is_raw=0, is_expr=1.
    char *code = process_printf_sugar(ctx, t, content, 0, "stdout", NULL, NULL, 0, 0, 1);

    ASTNode *node = ast_create(NODE_RAW_STMT);
    node->token = t;
    node->raw_stmt.content = code;
    node->type_info = type_new(TYPE_STRING);
    node->resolved_type = xstrdup("string");

    zfree(content);
    return node;
}

// Parse character literal
ASTNode *parse_char_literal(Token t)
{
    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.kind = LITERAL_CHAR;
    node->literal.string_val = token_strdup(t);

    // Decode character value
    uint32_t val = 0;
    const char *s = t.start + 1; // skip '
    if (*s == '\\')
    {
        s++;
        switch (*s)
        {
        case 'n':
            val = '\n';
            break;
        case 'r':
            val = '\r';
            break;
        case 't':
            val = '\t';
            break;
        case '\\':
            val = '\\';
            break;
        case '\'':
            val = '\'';
            break;
        case '"':
            val = '"';
            break;
        case '0':
            val = '\0';
            break;
        case 'u':
        case 'U':
        {
            if (s[1] == '{')
            {
                s += 2;
                while (*s && *s != '}' && *s != '\'')
                {
                    val = (val << 4);
                    if (*s >= '0' && *s <= '9')
                    {
                        val += (uint32_t)(*s - '0');
                    }
                    else if (*s >= 'a' && *s <= 'f')
                    {
                        val += (uint32_t)(*s - 'a' + 10);
                    }
                    else if (*s >= 'A' && *s <= 'F')
                    {
                        val += (uint32_t)(*s - 'A' + 10);
                    }
                    s++;
                }
                node->type_info = type_new(TYPE_RUNE);
                node->literal.int_val = val;
                return node;
            }
        }
        break;
        default:
            val = (unsigned char)*s;
            break;
        }
        node->type_info = type_new(TYPE_CHAR);
    }
    else
    {
        unsigned char first = (unsigned char)*s;
        if ((first & 0x80) == 0)
        {
            val = first;
            node->type_info = type_new(TYPE_CHAR);
        }
        else if ((first & 0xE0) == 0xC0)
        {
            val = (uint32_t)(((first & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F));
            node->type_info = type_new(TYPE_RUNE);
        }
        else if ((first & 0xF0) == 0xE0)
        {
            val = (uint32_t)(((first & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) |
                             ((unsigned char)s[2] & 0x3F));
            node->type_info = type_new(TYPE_RUNE);
        }
        else if ((first & 0xF8) == 0xF0)
        {
            val = (uint32_t)(((first & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) |
                             (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F));
            node->type_info = type_new(TYPE_RUNE);
        }
        else
        {
            val = first;
            node->type_info = type_new(TYPE_I8);
        }
    }

    node->literal.int_val = val;
    return node;
}

ASTNode *parse_size_or_typeof(ParserContext *ctx, Lexer *l, Token tk, int is_typeof)
{
    if (lexer_peek(l).kind != TOK_LPAREN)
    {
        zpanic_at(lexer_peek(l), is_typeof ? "Expected ( after typeof" : "Expected ( after sizeof");
    }
    lexer_next(l);

    int pos = l->pos;
    int col = l->col;
    int line = l->line;
    TypeUsage *old_pending = ctx->pending_type_validations;
    SliceType *old_slices = ctx->used_slices;
    TupleType *old_tuples = ctx->used_tuples;

    Type *ty = parse_type_formal(ctx, l);
    if (!ty)
    {
        return NULL;
    }

    int is_actually_var = 0;
    if (ty->kind != TYPE_UNKNOWN)
    {
        Type *base = ty;
        while (base->inner)
        {
            base = base->inner;
        }
        if (base->kind == TYPE_STRUCT && base->name)
        {
            if (!is_primitive_type_name(base->name) && !find_struct_def(ctx, base->name) &&
                !find_type_alias_node(ctx, base->name) && find_symbol_entry(ctx, base->name))
            {
                is_actually_var = 1;
            }
        }
    }

    ASTNode *node;
    if (ty->kind != TYPE_UNKNOWN && !is_actually_var && lexer_peek(l).kind == TOK_RPAREN)
    {
        lexer_next(l);
        char *ts = type_to_string(ty);
        node = ast_create(is_typeof ? NODE_TYPEOF : NODE_EXPR_SIZEOF);
        node->token = tk;
        node->size_of.target_type = ts;
        node->size_of.target_type_info = ty;
        node->size_of.is_type = 1;
        node->size_of.expr = NULL;
        if (!is_typeof)
        {
            node->type_info = type_new(TYPE_USIZE);
        }
    }
    else
    {
        ctx->pending_type_validations = old_pending;
        ctx->used_slices = old_slices;
        ctx->used_tuples = old_tuples;

        l->pos = pos;
        l->col = col;
        l->line = line;
        ASTNode *ex = parse_expression(ctx, l);
        if (lexer_next(l).kind != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), is_typeof ? "Expected ) after typeof expression"
                                               : "Expected ) after sizeof expression");
        }
        node = ast_create(is_typeof ? NODE_TYPEOF : NODE_EXPR_SIZEOF);
        node->token = tk;
        node->size_of.target_type = NULL;
        node->size_of.target_type_info = NULL;
        node->size_of.is_type = 0;
        node->size_of.expr = ex;
        if (!is_typeof)
        {
            node->type_info = type_new(TYPE_USIZE);
        }
    }
    return node;
}

// Parse sizeof expression: sizeof(type) or sizeof(expr)
ASTNode *parse_sizeof_expr(ParserContext *ctx, Lexer *l, Token sizeof_tk)
{
    return parse_size_or_typeof(ctx, l, sizeof_tk, 0);
}

// Parse typeof expression: typeof(type) or typeof(expr)
ASTNode *parse_typeof_expr(ParserContext *ctx, Lexer *l)
{
    Token t = lexer_peek(l);
    return parse_size_or_typeof(ctx, l, t, 1);
}

// Parse intrinsic expression: @type_name(T), @fields(T)
ASTNode *parse_intrinsic(ParserContext *ctx, Lexer *l)
{
    Token ident = lexer_next(l);
    if (ident.kind != TOK_IDENT)
    {
        zpanic_at(ident, "Expected intrinsic name after @");
    }

    int kind = -1;
    if (strncmp(ident.start, "type_name", 9) == 0 && ident.len == 9)
    {
        kind = 0;
    }
    else if (strncmp(ident.start, "fields", 6) == 0 && ident.len == 6)
    {
        kind = 1;
    }
    else
    {
        zpanic_at(ident, "Unknown intrinsic @%.*s", (int)ident.len, ident.start);
    }

    Token lparen = lexer_next(l);
    if (lparen.kind != TOK_LPAREN)
    {
        zpanic_at(lparen, "Expected ( after intrinsic");
    }

    Type *target = parse_type_formal(ctx, l);
    if (!target)
    {
        return NULL;
    }

    Token rparen = lexer_next(l);
    if (rparen.kind != TOK_RPAREN)
    {
        zpanic_at(rparen, "Expected ) after intrinsic type");
    }

    ASTNode *node = ast_create(NODE_REFLECTION);
    node->reflection.kind = kind;
    node->reflection.target_type = target;
    node->type_info = (kind == 0) ? type_new(TYPE_STRING) : type_new_ptr(type_new(TYPE_VOID));
    return node;
}

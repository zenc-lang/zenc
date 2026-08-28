// SPDX-License-Identifier: MIT

#include "parser.h"
#include "constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast/ast.h"
#include "utils/format_expr.h"
#include "plugins/plugin_manager.h"
#include "zen/zen_facts.h"
#include "zprep_plugin.h"
#include "analysis/move_check.h"

ASTNode *parse_defer(ParserContext *ctx, Lexer *l)
{
    Token defer_token = lexer_next(l);

    int prev_in_defer = ctx->cg.in_defer_block;
    ctx->cg.in_defer_block = 1;

    ASTNode *s;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        s = parse_block(ctx, l);
    }
    else
    {
        s = parse_statement(ctx, l);
    }

    ctx->cg.in_defer_block = prev_in_defer;

    ASTNode *n = ast_create(NODE_DEFER);
    n->token = defer_token;
    n->defer_stmt.stmt = s;
    return n;
}

ASTNode *parse_asm(ParserContext *ctx, Lexer *l)
{
    (void)ctx;
    Token t = lexer_peek(l);
    if (ctx->hook_zen_trigger)
    {
        ctx->hook_zen_trigger(TRIGGER_ASM, t, ctx->config);
    }
    lexer_next(l); // eat 'asm'

    int is_volatile = 0;
    if (lexer_peek(l).kind == TOK_VOLATILE)
    {
        is_volatile = 1;
        lexer_next(l);
    }

    if (lexer_peek(l).kind != TOK_LBRACE)
    {
        zpanic_at(lexer_peek(l), "Expected { after asm");
        return NULL;
    }
    lexer_next(l);

    size_t code_cap = 4096;
    char *code = xmalloc(code_cap);
    code[0] = 0;

    int last_pos = -1;
    while (1)
    {
        if (l->pos == last_pos)
        {
            zpanic_at(lexer_peek(l), "No progress in asm body");
            break;
        }
        last_pos = l->pos;
        Token inner_t = lexer_peek(l);

        if (inner_t.kind == TOK_RBRACE)
        {
            break;
        }
        if (inner_t.kind == TOK_COLON)
        {
            break;
        }
        if (inner_t.kind == TOK_EOF)
        {
            zpanic_at(inner_t, "Unexpected end of file in asm body");
            break;
        }

        if (inner_t.kind == TOK_STRING)
        {
            lexer_next(l);
            int str_len = (int)(inner_t.len) - 2;
            size_t current_len = strlen(code);
            size_t needed = current_len + (size_t)(str_len) + 5;
            if (needed >= code_cap)
            {
                code_cap = code_cap * 2 + needed;
                code = xrealloc(code, code_cap);
            }

            if (current_len > 0)
            {
                strcat(code, "\n");
            }
            strncat(code, inner_t.start + 1, (size_t)(str_len));
        }
        else if (inner_t.kind == TOK_IDENT)
        {
            lexer_next(l);
            size_t current_len = strlen(code);
            size_t needed = current_len + inner_t.len + 5;
            if (needed >= code_cap)
            {
                code_cap = code_cap * 2 + needed;
                code = xrealloc(code, code_cap);
            }

            if (current_len > 0)
            {
                strcat(code, "\n");
            }
            strncat(code, inner_t.start, inner_t.len);

            int arg_prev = -1;
            while (lexer_peek(l).kind != TOK_RBRACE && lexer_peek(l).kind != TOK_COLON)
            {
                if (l->pos == arg_prev)
                {
                    zpanic_at(lexer_peek(l), "No progress in asm body");
                    break;
                }
                arg_prev = l->pos;
                Token arg = lexer_peek(l);

                if (arg.kind == TOK_SEMICOLON)
                {
                    lexer_next(l);
                    break;
                }

                if (arg.kind == TOK_LBRACE)
                {
                    lexer_next(l);

                    if (strlen(code) + 2 >= code_cap)
                    {
                        code_cap *= 2;
                        code = xrealloc(code, code_cap);
                    }
                    strcat(code, "{");

                    while (lexer_peek(l).kind != TOK_RBRACE && lexer_peek(l).kind != TOK_EOF)
                    {
                        Token sub = lexer_next(l);
                        if (strlen(code) + sub.len + 1 >= code_cap)
                        {
                            code_cap = code_cap * 2 + sub.len;
                            code = xrealloc(code, code_cap);
                        }
                        strncat(code, sub.start, sub.len);
                    }
                    if (lexer_peek(l).kind == TOK_RBRACE)
                    {
                        lexer_next(l);
                        if (strlen(code) + 2 >= code_cap)
                        {
                            code_cap *= 2;
                            code = xrealloc(code, code_cap);
                        }
                        strcat(code, "}");
                    }
                    continue;
                }

                if (arg.kind == TOK_IDENT)
                {
                    char last_char = 0;
                    size_t clen = strlen(code);
                    if (clen > 0)
                    {
                        if (code[clen - 1] == ' ' && clen > 1)
                        {
                            last_char = code[clen - 2];
                        }
                        else
                        {
                            last_char = code[clen - 1];
                        }
                    }
                    if (last_char != '%' && last_char != '$' && last_char != ',')
                    {
                        break;
                    }
                }

                lexer_next(l);

                int no_space = 0;
                size_t clen = strlen(code);
                if (clen > 0)
                {
                    char lc = code[clen - 1];
                    if (lc == '%' || lc == '$')
                    {
                        no_space = 1;
                    }
                }

                if (!no_space)
                {
                    if (strlen(code) + 2 > code_cap)
                    {
                        code_cap = code_cap + 64;
                        code = xrealloc(code, code_cap);
                    }
                    strcat(code, " ");
                }
                if (strlen(code) + (size_t)(arg.len) + 1 > code_cap)
                {
                    code_cap = code_cap * 2 + (size_t)(arg.len);
                    code = xrealloc(code, code_cap);
                }
                strncat(code, arg.start, arg.len);
            }
        }
        else
        {
            zpanic_at(t, "Expected assembly string, instruction, or ':' in asm block");
            break;
        }
    }

    char **outputs = NULL;
    char **output_modes = NULL;
    int output_count = 0;

    if (lexer_peek(l).kind == TOK_COLON)
    {
        lexer_next(l);

        outputs = xmalloc(sizeof(char *) * 16);
        output_modes = xmalloc(sizeof(char *) * 16);

        while (1)
        {
            Token inner_t = lexer_peek(l);
            if (inner_t.kind == TOK_COLON || inner_t.kind == TOK_RBRACE)
            {
                break;
            }
            if (inner_t.kind == TOK_EOF)
            {
                zpanic_at(inner_t, "Unexpected end of file in asm outputs");
                break;
            }
            if (inner_t.kind == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }

            if (inner_t.kind == TOK_IDENT)
            {
                char *mode = token_strdup(inner_t);
                lexer_next(l);

                if (lexer_peek(l).kind != TOK_LPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ( after output mode");
                    return NULL;
                }
                lexer_next(l);

                Token var = lexer_next(l);
                if (var.kind != TOK_IDENT)
                {
                    zpanic_at(var, "Expected variable name");
                    return NULL;
                }

                if (lexer_peek(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after variable");
                    return NULL;
                }
                lexer_next(l);

                if (output_count >= 16)
                {
                    zpanic_at(lexer_peek(l), "Too many asm outputs (max 16)");
                    break;
                }
                outputs[output_count] = token_strdup(var);
                output_modes[output_count] = mode;
                output_count++;
            }
            else
            {
                break;
            }
        }
    }

    char **inputs = NULL;
    int input_count = 0;

    if (lexer_peek(l).kind == TOK_COLON)
    {
        lexer_next(l);

        inputs = xmalloc(sizeof(char *) * 16);

        while (1)
        {
            Token inner_t = lexer_peek(l);
            if (inner_t.kind == TOK_COLON || inner_t.kind == TOK_RBRACE)
            {
                break;
            }
            if (inner_t.kind == TOK_EOF)
            {
                zpanic_at(inner_t, "Unexpected end of file in asm inputs");
                break;
            }
            if (inner_t.kind == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }

            if (inner_t.kind == TOK_IDENT && strncmp(inner_t.start, "in", 2) == 0)
            {
                lexer_next(l);

                if (lexer_peek(l).kind != TOK_LPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ( after in");
                    return NULL;
                }
                lexer_next(l);

                Token var = lexer_next(l);
                if (var.kind != TOK_IDENT)
                {
                    zpanic_at(var, "Expected variable name");
                    return NULL;
                }

                if (lexer_peek(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after variable");
                    return NULL;
                }
                lexer_next(l);

                if (input_count >= 16)
                {
                    zpanic_at(lexer_peek(l), "Too many asm inputs (max 16)");
                    break;
                }
                inputs[input_count] = token_strdup(var);
                input_count++;
            }
            else
            {
                break;
            }
        }
    }

    char **clobbers = NULL;
    int clobber_count = 0;

    if (lexer_peek(l).kind == TOK_COLON)
    {
        lexer_next(l);

        clobbers = xmalloc(sizeof(char *) * 16);

        while (1)
        {
            Token inner_t = lexer_peek(l);
            if (inner_t.kind == TOK_RBRACE)
            {
                break;
            }
            if (inner_t.kind == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }

            if (inner_t.kind == TOK_IDENT && strncmp(inner_t.start, "clobber", 7) == 0)
            {
                lexer_next(l);
                if (lexer_peek(l).kind != TOK_LPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ( after clobber");
                    return NULL;
                }
                lexer_next(l);

                Token clob = lexer_next(l);
                if (clob.kind != TOK_STRING)
                {
                    zpanic_at(clob, "Expected string literal for clobber");
                    return NULL;
                }

                if (lexer_peek(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after clobber string");
                    return NULL;
                }
                lexer_next(l);

                char *c = xmalloc(clob.len);
                strncpy(c, clob.start + 1, (size_t)((int)(clob.len) - 2));
                c[(int)(clob.len) - 2] = 0;
                if (clobber_count >= 16)
                {
                    zpanic_at(lexer_peek(l), "Too many asm clobbers (max 16)");
                    break;
                }
                clobbers[clobber_count++] = c;
            }
            else
            {
                zpanic_at(t, "Expected 'clobber(\"...\")' in clobber list");
                break;
            }
        }
    }

    if (lexer_peek(l).kind != TOK_RBRACE)
    {
        zpanic_at(lexer_peek(l), "Expected } at end of asm block");
        return NULL;
    }
    lexer_next(l);

    ASTNode *n = ast_create(NODE_ASM);
    n->token = t;
    n->asm_stmt.code = code;
    n->asm_stmt.is_volatile = is_volatile;
    n->asm_stmt.outputs = outputs;
    n->asm_stmt.output_modes = output_modes;
    n->asm_stmt.inputs = inputs;
    n->asm_stmt.clobbers = clobbers;
    n->asm_stmt.output_count = output_count;
    n->asm_stmt.input_count = input_count;
    n->asm_stmt.clobber_count = clobber_count;

    return n;
}

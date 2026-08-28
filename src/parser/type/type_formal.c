// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "ast/ast.h"
#include "analysis/move_check.h"
#include "plugins/plugin_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analysis/const_fold.h"
#include "utils/utils.h"
#include "ast/primitives.h"

Type *parse_type_formal(ParserContext *ctx, Lexer *l)
{
    RECURSION_GUARD(ctx, l, type_new(TYPE_UNKNOWN));
    int is_restrict = 0;
    int is_const = 0;

    if (lexer_peek(l).kind == TOK_IDENT)
    {
        if (lexer_peek(l).len == 8 && strncmp(lexer_peek(l).start, "restrict", 8) == 0)
        {
            lexer_next(l); // eat restrict
            is_restrict = 1;
        }
        else if (lexer_peek(l).len == 5 && strncmp(lexer_peek(l).start, "const", 5) == 0)
        {
            lexer_next(l); // eat const
            is_const = 1;
        }
    }

    if (lexer_peek(l).kind == TOK_OP && lexer_peek(l).start[0] == '*')
    {
        zpanic_at(lexer_peek(l), "Zen C uses postfix pointers (e.g. 'Type*'). Prefix pointer "
                                 "syntax ('*Type') is not supported.");
        return NULL;
    }

    Type *t = NULL;

    // Example: fn(int, int) -> int
    if (lexer_peek(l).kind == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0 &&
        lexer_peek(l).len == 2)
    {
        lexer_next(l); // eat 'fn'

        int star_count = 0;
        while (lexer_peek(l).kind == TOK_OP && lexer_peek(l).start[0] == '*')
        {
            Token st = lexer_peek(l);
            int valid = 1;
            for (size_t i = 0; i < st.len; i++)
            {
                if (st.start[i] != '*')
                {
                    valid = 0;
                }
            }
            if (!valid)
            {
                break;
            }
            lexer_next(l);
            star_count += (int)(st.len);
        }

        Type *fn_type = type_new(TYPE_FUNCTION);
        fn_type->is_raw = (star_count > 0);
        if (fn_type->is_raw)
        {
            fn_type->traits.has_drop = 0;
        }
        fn_type->is_varargs = 0;

        Type *wrapped = fn_type;
        for (int i = 1; i < star_count; i++)
        {
            wrapped = type_new_ptr(wrapped);
        }

        z_parse_expect(l, TOK_LPAREN, "Expected '(' for function type");

        // Parse Arguments
        fn_type->count = 0;
        fn_type->args = NULL;

        while (lexer_peek(l).kind != TOK_RPAREN)
        {
            if (lexer_peek(l).kind == TOK_ELLIPSIS)
            {
                lexer_next(l);
                fn_type->is_varargs = 1;
                break;
            }

            Type *arg = parse_type_formal(ctx, l);
            if (!arg)
            {
                break;
            }
            fn_type->count++;
            fn_type->args = xrealloc(fn_type->args, sizeof(Type *) * (size_t)(fn_type->count));
            fn_type->args[fn_type->count - 1] = arg;

            if (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
            }
            else
            {
                break;
            }
        }
        z_parse_expect(l, TOK_RPAREN, "Expected ')' after function args");

        // Parse Return Type (-> Type)
        if (lexer_peek(l).kind == TOK_ARROW)
        {
            lexer_next(l); // eat ->
            fn_type->inner = parse_type_formal(ctx, l);
            if (!fn_type->inner)
            {
                RECURSION_EXIT(ctx);
                return NULL;
            }
        }
        else
        {
            fn_type->inner = type_new(TYPE_VOID);
        }

        t = wrapped;
    }
    else
    {
        // Handles: int, Struct, Generic<T>, [Slice], (Tuple)
        t = parse_type_base(ctx, l);
        if (!t)
        {
            RECURSION_EXIT(ctx);
            return NULL;
        }
    }

    // Handles: T*, T**, etc.
    while (lexer_peek(l).kind == TOK_OP && lexer_peek(l).start[0] == '*')
    {
        Token st = lexer_peek(l);
        int valid = 1;
        for (size_t i = 0; i < st.len; i++)
        {
            if (st.start[i] != '*')
            {
                valid = 0;
            }
        }
        if (!valid)
        {
            break;
        }

        lexer_next(l); // consume '*' or '**'
        for (size_t i = 0; i < st.len; i++)
        {
            t = type_new_ptr(t);
        }
    }

    int *dims = NULL;
    int dims_cap = 0;
    int dims_count = 0;

    while (lexer_peek(l).kind == TOK_LBRACKET)
    {
        lexer_next(l);

        if (dims_count == dims_cap)
        {
            dims_cap = dims_cap == 0 ? 4 : dims_cap * 2;
            dims = xrealloc(dims, sizeof(int) * (size_t)(dims_cap));
        }

        if (lexer_peek(l).kind == TOK_RBRACKET)
        {
            lexer_next(l);

            char *inner_str = type_to_string(t);
            register_slice(ctx, inner_str);
            zfree(inner_str);

            dims[dims_count++] = 0;
            continue;
        }

        ASTNode *size_expr = parse_expression(ctx, l);
        long long compiled_size = 0;
        int size = 0;
        if (!size_expr)
        {
            zpanic_at(lexer_peek(l), "Expected array size expression");
            return NULL;
        }
        if (eval_const_int_expr(size_expr, ctx, &compiled_size))
        {
            size = (int)compiled_size;
        }
        else
        {
            // Use lexer token for error location — the parsed expression's token
            // may have a corrupted start position from malformed input.
            if (ctx->config->misra_mode)
            {
                zpanic_at(lexer_peek(l), "MISRA Rule 18.8");
                return NULL;
            }
            else
            {
                zpanic_at(lexer_peek(l),
                          "Array size must be a known compile-time constant integer");
                return NULL;
            }
        }

        if (lexer_next(l).kind != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ']' in array type");
            return NULL;
        }

        dims[dims_count++] = size;
    }

    for (int i = dims_count - 1; i >= 0; i--)
    {
        Type *arr = type_new(TYPE_ARRAY);
        arr->inner = t;
        arr->array_size = dims[i];
        t = arr;
    }

    if (dims)
    {
        zfree(dims);
    }

    if (is_restrict)
    {
        t->is_restrict = 1;
    }
    if (is_const)
    {
        t->is_const = 1;
    }

    RECURSION_EXIT(ctx);
    return t;
}

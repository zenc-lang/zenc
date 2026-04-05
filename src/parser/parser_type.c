#include "../ast/ast.h"
#include "analysis/const_fold.h"
#include "parser.h"
#include "../ast/primitives.h"

Type *parse_type_base(ParserContext *ctx, Lexer *l)
{
    Token t = lexer_peek(l);

    if (t.type == TOK_IDENT)
    {
        int explicit_struct = 0;
        // Handle "struct Name" or "enum Name"
        if ((t.len == 6 && strncmp(t.start, "struct", 6) == 0) ||
            (t.len == 4 && strncmp(t.start, "enum", 4) == 0))
        {
            if (strncmp(t.start, "struct", 6) == 0)
            {
                explicit_struct = 1;
            }
            lexer_next(l); // consume keyword
            t = lexer_peek(l);
            if (t.type != TOK_IDENT)
            {
                zpanic_at(t, "Expected identifier after struct/enum");
            }
        }

        lexer_next(l);
        char *name = token_strdup(t);

        // Check for alias
        TypeAlias *alias_node = find_type_alias_node(ctx, name);
        if (alias_node)
        {
            free(name);
            Lexer tmp;
            lexer_init(&tmp, alias_node->original_type);

            if (alias_node->is_opaque)
            {
                Type *underlying = parse_type_formal(ctx, &tmp);
                Type *wrapper = type_new(TYPE_ALIAS);
                wrapper->name = xstrdup(alias_node->alias);
                wrapper->inner = underlying;
                wrapper->alias.is_opaque_alias = 1;
                wrapper->alias.alias_defined_in_file =
                    alias_node->defined_in_file ? xstrdup(alias_node->defined_in_file) : NULL;
                return wrapper;
            }

            return parse_type_formal(ctx, &tmp);
        }

        // Self type alias: Replace "Self" with current impl struct type
        if (strcmp(name, "Self") == 0 && ctx->current_impl_struct)
        {
            name = xstrdup(ctx->current_impl_struct);
        }

        // Handle Namespace :: (A::B -> A_B)
        while (lexer_peek(l).type == TOK_DCOLON)
        {
            lexer_next(l); // eat ::
            Token next = lexer_next(l);
            if (next.type != TOK_IDENT)
            {
                zpanic_at(t, "Expected identifier after ::");
            }

            char *suffix = token_strdup(next);
            // Map aliases (I32 -> int32_t, string -> char*) using centralized logic
            const char *resolved_suffix = normalize_type_name(suffix);

            // Check if 'name' is a module alias (e.g., m::Vector)
            Module *mod = find_module(ctx, name);
            char *merged;
            if (mod)
            {
                // Module-qualified type
                if (mod->is_c_header)
                {
                    // C header: Use type name directly without prefix
                    // To prevent name mangling, we might consider changing
                    // this to also use the prefix.
                    merged = xstrdup(resolved_suffix);

                    register_extern_symbol(ctx, merged);
                }
                else
                {
                    // Zen module: Use module base name as prefix
                    merged = xmalloc(strlen(mod->base_name) + strlen(resolved_suffix) + 2);
                    sprintf(merged, "%s__%s", mod->base_name, resolved_suffix);
                }
            }
            else
            {
                // Regular namespace or enum variant
                merged = xmalloc(strlen(name) + strlen(resolved_suffix) + 2);
                sprintf(merged, "%s__%s", name, resolved_suffix);
            }

            free(name);

            name = merged;
        }

        // Check for Primitives (Base types)
        // Check for Primitives (Base types)
        const ZenPrimitive *prim = find_primitive_by_name(name);
        if (prim)
        {
            free(name);
            return type_new(prim->kind);
        }

        // C23 BitInt Support (i42, u256, etc.)
        if ((name[0] == 'i' || name[0] == 'u') && isdigit(name[1]))
        {
            // Verify it is a purely numeric suffix
            int valid = 1;
            for (size_t k = 1; k < strlen(name); k++)
            {
                if (!isdigit(name[k]))
                {
                    valid = 0;
                    break;
                }
            }
            if (valid)
            {
                int width = atoi(name + 1);
                if (width > 0)
                {
                    // Map standard widths to standard types for standard ABI/C compabitility
                    if (name[0] == 'i')
                    {
                        if (width == 8)
                        {
                            free(name);
                            return type_new(TYPE_I8);
                        }
                        if (width == 16)
                        {
                            free(name);
                            return type_new(TYPE_I16);
                        }
                        if (width == 32)
                        {
                            free(name);
                            return type_new(TYPE_I32);
                        }
                        if (width == 64)
                        {
                            free(name);
                            return type_new(TYPE_I64);
                        }
                        if (width == 128)
                        {
                            free(name);
                            return type_new(TYPE_I128);
                        }
                    }
                    else
                    {
                        if (width == 8)
                        {
                            free(name);
                            return type_new(TYPE_U8);
                        }
                        if (width == 16)
                        {
                            free(name);
                            return type_new(TYPE_U16);
                        }
                        if (width == 32)
                        {
                            free(name);
                            return type_new(TYPE_U32);
                        }
                        if (width == 64)
                        {
                            free(name);
                            return type_new(TYPE_U64);
                        }
                        if (width == 128)
                        {
                            free(name);
                            return type_new(TYPE_U128);
                        }
                    }

                    Type *inner_t = type_new(name[0] == 'u' ? TYPE_UBITINT : TYPE_BITINT);
                    inner_t->array_size = width;
                    free(name);
                    return inner_t;
                }
            }
        }

        // Relaxed Type Check: If explicit 'struct Name', trust the user.
        if (explicit_struct)
        {
            Type *ty = type_new(TYPE_STRUCT);
            ty->name = name;
            ty->is_explicit_struct = 1;
        }

        // Selective imports ONLY apply when we're NOT in a module context
        if (!ctx->current_module_prefix)
        {
            SelectiveImport *si = find_selective_import(ctx, name);
            if (si)
            {
                // This is a selectively imported symbol
                // Resolve to the actual struct name which was prefixed during module
                // parsing
                free(name);
                name = xmalloc(strlen(si->source_module) + strlen(si->symbol) + 3);
                sprintf(name, "%s__%s", si->source_module, si->symbol);
            }
        }

        // If we're IN a module and no selective import matched, apply module prefix
        if (ctx->current_module_prefix && !is_known_generic(ctx, name) &&
            !is_primitive_type_name(name) && strcasecmp(name, "Self") != 0 &&
            !is_extern_symbol(ctx, name))
        {
            // Auto-prefix struct name if in module context (unless it's a known
            // primitive/generic)
            char *prefixed_name = xmalloc(strlen(ctx->current_module_prefix) + strlen(name) + 3);
            sprintf(prefixed_name, "%s__%s", ctx->current_module_prefix, name);
            free(name);
            name = prefixed_name;
        }

        if (!is_known_generic(ctx, name) && strcmp(name, "Self") != 0)
        {
            register_type_usage(ctx, name, t);
        }

        Type *ty = type_new(TYPE_STRUCT);
        ty->name = name;
        ty->is_explicit_struct = explicit_struct;

        // Handle Generics <T> or <K, V>
        if (lexer_peek(l).type == TOK_LANGLE ||
            (lexer_peek(l).type == TOK_OP && strncmp(lexer_peek(l).start, "<", 1) == 0))
        {
            lexer_next(l); // eat <
            Type *first_arg = parse_type_formal(ctx, l);
            char *first_arg_str = type_to_string(first_arg);

            // Check for multi-arg: <K, V>
            Token next_tok = lexer_peek(l);
            if (next_tok.type == TOK_COMMA)
            {
                // Multi-arg case
                char **args = xmalloc(sizeof(char *) * 8);
                int arg_count = 0;
                args[arg_count++] = xstrdup(first_arg_str);

                while (lexer_peek(l).type == TOK_COMMA)
                {
                    lexer_next(l); // eat ,
                    Type *arg = parse_type_formal(ctx, l);
                    char *arg_str = type_to_string(arg);
                    args = realloc(args, sizeof(char *) * (arg_count + 1));
                    args[arg_count++] = xstrdup(arg_str);
                    free(arg_str);
                }

                // Consume >
                next_tok = lexer_peek(l);
                if (next_tok.type == TOK_RANGLE)
                {
                    lexer_next(l);
                }
                else if (next_tok.type == TOK_OP && next_tok.len == 2 &&
                         strncmp(next_tok.start, ">>", 2) == 0)
                {
                    l->pos += 1;
                    l->col += 1;
                }
                else
                {
                    zpanic_at(t, "Expected > after generic");
                }

                // Call multi-arg instantiation
                int is_generic_dep = 0;
                for (int i = 0; i < arg_count; ++i)
                {
                    if (is_generic_dependent_str(ctx, args[i]))
                    {
                        is_generic_dep = 1;
                        break;
                    }
                }

                if (!is_generic_dep)
                {
                    instantiate_generic_multi(ctx, name, args, arg_count, t);
                }

                // Build mangled name dynamically
                size_t mangled_len = strlen(name) + 1;
                for (int i = 0; i < arg_count; i++)
                {
                    char *clean = sanitize_mangled_name(args[i]);
                    mangled_len += 2 + strlen(clean);
                    free(clean);
                }
                char *mangled = xmalloc(mangled_len);
                strcpy(mangled, name);
                for (int i = 0; i < arg_count; i++)
                {
                    char *clean = sanitize_mangled_name(args[i]);
                    strcat(mangled, "__");
                    strcat(mangled, clean);
                    free(clean);
                    free(args[i]);
                }
                free(args);

                free(ty->name);
                ty->name = mangled;
            }
            else
            {
                // Single-arg case - PRESERVE ORIGINAL FLOW EXACTLY
                if (next_tok.type == TOK_RANGLE)
                {
                    lexer_next(l); // Consume >
                }
                else if (next_tok.type == TOK_OP && next_tok.len == 2 &&
                         strncmp(next_tok.start, ">>", 2) == 0)
                {
                    // Split >> into two > tokens
                    l->pos += 1;
                    l->col += 1;
                }
                else
                {
                    zpanic_at(t, "Expected > after generic");
                }

                char *unmangled_arg = type_to_string(first_arg);

                int is_single_dep = is_generic_dependent_str(ctx, first_arg_str);

                if (!is_single_dep)
                {
                    instantiate_generic(ctx, name, first_arg_str, unmangled_arg, t);
                }
                free(unmangled_arg);

                char *clean_arg = sanitize_mangled_name(first_arg_str);
                size_t mangled_sz = strlen(name) + strlen(clean_arg) + 3;
                char *mangled = xmalloc(mangled_sz);
                snprintf(mangled, mangled_sz, "%s__%s", name, clean_arg);
                free(clean_arg);

                free(ty->name);
                ty->name = mangled;
            }

            free(first_arg_str);
            ty->kind = TYPE_STRUCT;
            ty->args = NULL;
            ty->arg_count = 0;
        }
        return ty;
    }

    if (t.type == TOK_LBRACKET)
    {
        lexer_next(l); // eat [
        Type *inner = parse_type_formal(ctx, l);

        // Check for fixed-size array [T; N]
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l); // eat ;
            ASTNode *size_expr = parse_expression(ctx, l);
            long long compiled_size = 0;
            int size = 0;
            if (eval_const_int_expr(size_expr, ctx, &compiled_size))
            {
                size = (int)compiled_size;
            }
            else
            {
                zpanic_at(size_expr->token,
                          "Array size must be a compile-time constant or integer literal");
            }
            if (lexer_next(l).type != TOK_RBRACKET)
            {
                zpanic_at(lexer_peek(l), "Expected ] after array size");
            }

            Type *arr = type_new(TYPE_ARRAY);
            arr->inner = inner;
            arr->array_size = size;
            return arr;
        }

        // Otherwise it's a slice [T]
        if (lexer_next(l).type != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ] in type");
        }

        char *inner_str = type_to_string(inner);
        if (!is_known_generic(ctx, inner_str))
        {
            register_slice(ctx, inner_str);
        }

        Type *arr = type_new(TYPE_ARRAY);
        arr->inner = inner;
        arr->array_size = 0; // 0 means slice, not fixed-size
        return arr;
    }

    if (t.type == TOK_LPAREN)
    {
        lexer_next(l); // eat (
        char sig[256];
        sig[0] = 0;

        while (1)
        {
            Type *sub = parse_type_formal(ctx, l);
            char *s = type_to_string(sub);
            strcat(sig, s);
            free(s);

            if (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
                strcat(sig, "__");
            }
            else
            {
                break;
            }
        }
        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), "Expected ) in tuple");
        }

        register_tuple(ctx, sig);

        char *clean_sig = sanitize_mangled_name(sig);
        char *tuple_name = xmalloc(strlen(clean_sig) + 8);
        sprintf(tuple_name, "Tuple__%s", clean_sig);
        free(clean_sig);

        Type *ty = type_new(TYPE_STRUCT);
        ty->name = tuple_name;
        return ty;
    }

    // If we have an identifier that wasn't found,
    // assume it is a valid external C type
    // (for example, a struct defined in implementation).
    if (t.type == TOK_IDENT)
    {
        char *fallback = token_strdup(t);
        lexer_next(l);
        Type *ty = type_new(TYPE_STRUCT);
        ty->name = fallback;
        ty->is_explicit_struct = 0;
        return ty;
    }

    return type_new(TYPE_UNKNOWN);
}

Type *parse_type_formal(ParserContext *ctx, Lexer *l)
{
    int is_restrict = 0;
    int is_const = 0;

    if (lexer_peek(l).type == TOK_IDENT)
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

    Type *t = NULL;

    // Example: fn(int, int) -> int
    if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0 &&
        lexer_peek(l).len == 2)
    {
        lexer_next(l); // eat 'fn'

        int star_count = 0;
        while (lexer_peek(l).type == TOK_OP && lexer_peek(l).start[0] == '*')
        {
            Token st = lexer_peek(l);
            int valid = 1;
            for (int i = 0; i < st.len; i++)
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
            star_count += st.len;
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

        expect(l, TOK_LPAREN, "Expected '(' for function type");

        // Parse Arguments
        fn_type->arg_count = 0;
        fn_type->args = NULL;

        while (lexer_peek(l).type != TOK_RPAREN)
        {
            Type *arg = parse_type_formal(ctx, l);
            fn_type->arg_count++;
            fn_type->args = xrealloc(fn_type->args, sizeof(Type *) * fn_type->arg_count);
            fn_type->args[fn_type->arg_count - 1] = arg;

            if (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
            }
            else
            {
                break;
            }
        }
        expect(l, TOK_RPAREN, "Expected ')' after function args");

        // Parse Return Type (-> Type)
        if (lexer_peek(l).type == TOK_ARROW)
        {
            lexer_next(l);                              // eat ->
            fn_type->inner = parse_type_formal(ctx, l); // Return type stored in inner
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
    }

    // Handles: T*, T**, etc.
    while (lexer_peek(l).type == TOK_OP && lexer_peek(l).start[0] == '*')
    {
        Token st = lexer_peek(l);
        int valid = 1;
        for (int i = 0; i < st.len; i++)
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
        for (int i = 0; i < st.len; i++)
        {
            t = type_new_ptr(t);
        }
    }

    int *dims = NULL;
    int dims_cap = 0;
    int dims_count = 0;

    while (lexer_peek(l).type == TOK_LBRACKET)
    {
        lexer_next(l);

        if (dims_count == dims_cap)
        {
            dims_cap = dims_cap == 0 ? 4 : dims_cap * 2;
            dims = xrealloc(dims, sizeof(int) * dims_cap);
        }

        if (lexer_peek(l).type == TOK_RBRACKET)
        {
            lexer_next(l);

            char *inner_str = type_to_string(t);
            register_slice(ctx, inner_str);
            free(inner_str);

            dims[dims_count++] = 0;
            continue;
        }

        ASTNode *size_expr = parse_expression(ctx, l);
        long long compiled_size = 0;
        int size = 0;
        if (eval_const_int_expr(size_expr, ctx, &compiled_size))
        {
            size = (int)compiled_size;
        }
        else
        {
            zpanic_at(size_expr->token, "Array size must be a known compile-time constant integer");
        }

        if (lexer_next(l).type != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ']' in array type");
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
        free(dims);
    }

    if (is_restrict)
    {
        t->is_restrict = 1;
    }
    if (is_const)
    {
        t->is_const = 1;
    }
    return t;
}

char *parse_type(ParserContext *ctx, Lexer *l)
{
    Type *t = parse_type_formal(ctx, l);

    return type_to_string(t);
}

char *parse_array_literal(ParserContext *ctx, Lexer *l, const char *st)
{
    (void)ctx;
    lexer_next(l);
    size_t cap = 128;
    char *c = xmalloc(cap);
    c[0] = 0;
    int n = 0;

    while (1)
    {
        Token t = lexer_peek(l);
        if (t.type == TOK_RBRACKET)
        {
            lexer_next(l);
            break;
        }
        if (t.type == TOK_COMMA)
        {
            lexer_next(l);
            continue;
        }

        const char *s = l->src + l->pos;
        int d = 0;
        while (1)
        {
            Token it = lexer_peek(l);
            if (it.type == TOK_EOF)
            {
                break;
            }
            if (d == 0 && (it.type == TOK_COMMA || it.type == TOK_RBRACKET))
            {
                break;
            }
            if (it.type == TOK_LBRACKET || it.type == TOK_LPAREN)
            {
                d++;
            }
            if (it.type == TOK_RBRACKET || it.type == TOK_RPAREN)
            {
                d--;
            }
            lexer_next(l);
        }

        int len = (l->src + l->pos) - s;
        if (strlen(c) + len + 5 > cap)
        {
            cap *= 2;
            c = xrealloc(c, cap);
        }
        if (n > 0)
        {
            strcat(c, ", ");
        }
        strncat(c, s, len);
        n++;
    }

    char rt[64];
    if (strncmp(st, "Slice__", 7) == 0)
    {
        strcpy(rt, st + 7);
    }
    else
    {
        strcpy(rt, "int");
    }

    char *o = xmalloc(strlen(c) + 128);
    sprintf(o, "(%s){.data=(%s[]){%s},.len=%d,.cap=%d}", st, rt, c, n, n);
    free(c);
    return o;
}
char *parse_tuple_literal(ParserContext *ctx, Lexer *l, const char *tn)
{
    (void)ctx; // suppress unused parameter warning
    lexer_next(l);
    size_t cap = 128;
    char *c = xmalloc(cap);
    c[0] = 0;

    while (1)
    {
        Token t = lexer_peek(l);
        if (t.type == TOK_RPAREN)
        {
            lexer_next(l);
            break;
        }
        if (t.type == TOK_COMMA)
        {
            lexer_next(l);
            continue;
        }

        const char *s = l->src + l->pos;
        int d = 0;
        while (1)
        {
            Token it = lexer_peek(l);
            if (it.type == TOK_EOF)
            {
                break;
            }
            if (d == 0 && (it.type == TOK_COMMA || it.type == TOK_RPAREN))
            {
                break;
            }
            if (it.type == TOK_LPAREN)
            {
                d++;
            }
            if (it.type == TOK_RPAREN)
            {
                d--;
            }
            lexer_next(l);
        }

        int len = (l->src + l->pos) - s;
        if (strlen(c) + len + 5 > cap)
        {
            cap *= 2;
            c = xrealloc(c, cap);
        }
        if (strlen(c) > 0)
        {
            strcat(c, ", ");
        }
        strncat(c, s, len);
    }

    char *o = xmalloc(strlen(c) + 128);
    sprintf(o, "(%s){%s}", tn, c);
    free(c);
    return o;
}
ASTNode *parse_embed(ParserContext *ctx, Lexer *l)
{
    lexer_next(l);
    Token t = lexer_next(l);
    if (t.type != TOK_STRING)
    {
        zpanic_at(t, "String required");
    }
    char fn[256];
    strncpy(fn, t.start + 1, t.len - 2);
    fn[t.len - 2] = 0;

    // Check for optional "as Type"
    Type *target_type = NULL;
    if (lexer_peek(l).type == TOK_IDENT && lexer_peek(l).len == 2 &&
        strncmp(lexer_peek(l).start, "as", 2) == 0)
    {
        lexer_next(l); // consume 'as'
        target_type = parse_type_formal(ctx, l);
    }

    FILE *f = fopen(fn, "rb");
    if (!f)
    {
        zpanic_at(t, "404: %s", fn);
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    unsigned char *b = xmalloc(len);
    fread(b, 1, len, f);
    fclose(f);

    size_t oc = len * 6 + 256;
    char *o = xmalloc(oc);

    // Default Type if none
    if (!target_type)
    {
        // Default: Slice__char
        register_slice(ctx, "char");

        Type *slice_type = type_new(TYPE_STRUCT);
        slice_type->name = xstrdup("Slice__char");
        target_type = slice_type;

        sprintf(o, "(Slice__char){.data=(char[]){");
    }
    else
    {
        // Handle specific type
        char *ts = type_to_string(target_type);

        if (target_type->kind == TYPE_ARRAY)
        {
            char *inner_ts = type_to_string(target_type->inner);
            if (target_type->array_size > 0)
            {
                Type *ptr_type = type_new_ptr(target_type->inner); // Reuse inner
                target_type = ptr_type;
                sprintf(o, "(%s[]){", inner_ts);
            }
            else
            {
                // Slice -> Slice__T struct
                register_slice(ctx, inner_ts);
                char slice_name[256];
                sprintf(slice_name, "Slice__%s", inner_ts);
                Type *slice_t = type_new(TYPE_STRUCT);
                slice_t->name = xstrdup(slice_name);
                target_type = slice_t;
                sprintf(o, "(%s){.data=(%s[]){", slice_name, inner_ts);
            }
            free(inner_ts);
        }
        else
        {
            if (strcmp(ts, "string") == 0 || strcmp(ts, "char*") == 0)
            {
                sprintf(o, "(char*)\"");
            }
            else
            {
                sprintf(o, "(%s){", ts);
            }
        }
        free(ts);
    }

    char *p = o + strlen(o);

    // Check if string mode
    int is_string = (target_type && (strcmp(type_to_string(target_type), "string") == 0 ||
                                     strcmp(type_to_string(target_type), "char*") == 0));

    for (int i = 0; i < len; i++)
    {
        if (is_string)
        {
            // Hex escape for string
            p += sprintf(p, "\\x%02X", b[i]);
        }
        else
        {
            p += sprintf(p, "0x%02X,", b[i]);
        }
    }

    if (is_string)
    {
        sprintf(p, "\"");
    }
    else
    {
        int is_slice = (strncmp(o, "(Slice__", 8) == 0);

        if (is_slice)
        {
            sprintf(p, "},.len=%ld,.cap=%ld}", len, len);
        }
        else
        {
            sprintf(p, "}");
        }
    }

    free(b);

    ASTNode *n = ast_create(NODE_RAW_STMT);
    n->token = t;
    n->raw_stmt.content = o;
    n->type_info = target_type;
    return n;
}

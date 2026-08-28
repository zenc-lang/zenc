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

char *parse_condition_raw(ParserContext *ctx, Lexer *l)
{
    (void)ctx; // suppress unused parameter warning
    Token t = lexer_peek(l);
    if (t.kind == TOK_LPAREN)
    {
        Token op = lexer_next(l);
        const char *s = op.start;
        int d = 1;
        while (d > 0)
        {
            t = lexer_next(l);
            if (t.kind == TOK_EOF)
            {
                zpanic_at(t, "Unterminated condition");
                return NULL;
            }
            if (t.kind == TOK_LPAREN)
            {
                d++;
            }
            if (t.kind == TOK_RPAREN)
            {
                d--;
            }
        }
        const char *cs = s + 1;
        ptrdiff_t len = t.start - cs;
        char *c = xmalloc((size_t)(len + 1));
        strncpy(c, cs, (size_t)(len));
        c[len] = 0;
        return c;
    }
    else
    {
        const char *start = l->src + l->pos;
        while (1)
        {
            t = lexer_peek(l);
            if (t.kind == TOK_LBRACE || t.kind == TOK_EOF)
            {
                break;
            }
            lexer_next(l);
        }
        ptrdiff_t len = (l->src + l->pos) - start;
        if (len == 0)
        {
            zpanic_at(lexer_peek(l), "Empty condition or missing body");
            return NULL;
        }
        char *c = xmalloc((size_t)(len + 1));
        strncpy(c, start, (size_t)(len));
        c[len] = 0;
        return c;
    }
}

typedef struct
{
    char *final_struct;
    char *final_cast;
} MixinResolution;

static MixinResolution resolve_mixin_method(ParserContext *ctx, const char *struct_name,
                                            const char *method_name, int is_ptr)
{
    MixinResolution res = {xstrdup(struct_name), NULL};

    char target_func_raw[MAX_FUNC_NAME_LEN];
    snprintf(target_func_raw, sizeof(target_func_raw), "%s__%s", struct_name,
             method_name); /* TODO: check buffer size */
    char *target_func = merge_underscores(target_func_raw);

    if (!find_func(ctx, target_func))
    {
        ASTNode *mixin_def = find_struct_def(ctx, struct_name);
        if (mixin_def && mixin_def->kind == NODE_STRUCT && mixin_def->strct.used_structs)
        {
            for (int k = 0; k < mixin_def->strct.used_struct_count; k++)
            {
                char mixin_func_raw[128];
                snprintf(mixin_func_raw, 128, "%s__%s", mixin_def->strct.used_structs[k],
                         method_name);
                char *mixin_func = merge_underscores(mixin_func_raw);
                if (find_func(ctx, mixin_func))
                {
                    zfree(res.final_struct);
                    res.final_struct = xstrdup(mixin_def->strct.used_structs[k]);
                    char cast_buf[128];
                    if (is_ptr)
                    {
                        snprintf(cast_buf, 128, "(%s*)", res.final_struct);
                    }
                    else
                    {
                        snprintf(cast_buf, 128, "(%s*)&", res.final_struct);
                    }
                    res.final_cast = xstrdup(cast_buf);
                    zfree(mixin_func);
                    break;
                }
                zfree(mixin_func);
            }
        }
    }
    zfree(target_func);
    return res;
}

static MixinResolution resolve_method_from_type_str(ParserContext *ctx, const char *vtype,
                                                    const char *method)
{
    char ptr_check[64];
    strncpy(ptr_check, vtype, 63);
    ptr_check[63] = 0;
    int is_ptr = (strchr(ptr_check, '*') != NULL);
    if (is_ptr)
    {
        char *p = (char *)strchr(ptr_check, '*');
        if (p)
        {
            *p = 0;
        }
    }
    return resolve_mixin_method(ctx, ptr_check, method, is_ptr);
}

char *rewrite_expr_methods(ParserContext *ctx, char *raw)
{
    if (!raw)
    {
        return NULL;
    }

    int in_expr = 0;
    char *result = xmalloc(strlen(raw) * 4 + 100);
    char *dest = result;
    char *src = raw;

    while (*src)
    {
        if (strncmp(src, "#{", 2) == 0)
        {
            in_expr = 1;
            src += 2;
            *dest++ = '(';
            continue;
        }

        if (in_expr && *src == '}')
        {
            in_expr = 0;
            *dest++ = ')';
            src++;
            continue;
        }

        if (in_expr && *src == '.')
        {
            char acc[64];
            int i = 0;
            char *back = src - 1;
            while (back >= raw && (isalnum(*back) || *back == '_'))
            {
                back--;
            }
            back++;
            while (back < src && i < 63)
            {
                acc[i++] = *back++;
            }
            acc[i] = 0;

            char *vtype = find_symbol_type(ctx, acc);
            if (!vtype)
            {
                *dest++ = *src++;
                continue;
            }

            char method[64];
            i = 0;
            src++;
            while (isalnum(*src) || *src == '_')
            {
                method[i++] = *src++;
            }
            method[i] = 0;

            // Check for field access
            char *base_t = xstrdup(vtype);
            char *pc = (char *)strchr(base_t, '*');
            int is_ptr_type = (pc != NULL);
            if (pc)
            {
                *pc = 0;
            }

            // Resolve type alias if exists (for example: Vec2f -> Vec2_float)
            const char *resolved_type = find_type_alias(ctx, base_t);
            if (resolved_type)
            {
                zfree(base_t);
                base_t = xstrdup(resolved_type);
            }

            ASTNode *def = find_struct_def(ctx, base_t);
            int is_field = 0;
            if (def && (def->kind == NODE_STRUCT))
            {
                ASTNode *f = def->strct.fields;
                while (f)
                {
                    if (strcmp(f->field.name, method) == 0)
                    {
                        is_field = 1;
                        break;
                    }
                    f = f->next;
                }
            }
            zfree(base_t);

            if (is_field)
            {
                dest -= strlen(acc);
                if (is_ptr_type)
                {
                    dest += sprintf(dest, "(%s)->%s", acc, method); /* TODO: check buffer size */
                }
                else
                {
                    dest += sprintf(dest, "(%s).%s", acc, method); /* TODO: check buffer size */
                }
                continue;
            }

            if (*src == '(')
            {
                dest -= strlen(acc);
                int paren_depth = 0;
                src++;
                paren_depth++;

                int is_ptr = (strchr(vtype, '*') != NULL);

                // Mixin Lookup Logic
                MixinResolution res = resolve_method_from_type_str(ctx, vtype, method);
                char *final_cast = res.final_cast;
                char *final_method = xstrdup(method);
                char *final_struct = res.final_struct;

                if (final_cast)
                {
                    // Mixin call: Foo__method((Foo*)&obj
                    char call_buf[MAX_ERROR_MSG_LEN];
                    snprintf(call_buf, sizeof(call_buf), "%s__%s", final_struct, final_method);
                    char *mangled_call = merge_underscores(call_buf);

                    dest += sprintf(dest, "%s(%s%s", mangled_call, final_cast,
                                    acc); /* TODO: check buffer size */
                    zfree(final_cast);
                }
                else
                {
                    // Standard call
                    char call_buf[MAX_ERROR_MSG_LEN];
                    snprintf(call_buf, sizeof(call_buf), "%s__%s", final_struct, final_method);
                    char *mangled_call = merge_underscores(call_buf);

                    dest += sprintf(dest, "%s(%s%s", mangled_call, is_ptr ? "" : "&",
                                    acc); /* TODO: check buffer size */
                }
                zfree(final_struct);
                zfree(final_method);

                int has_args = 0;
                while (*src && paren_depth > 0)
                {
                    if (!isspace(*src))
                    {
                        has_args = 1;
                    }
                    if (*src == '(')
                    {
                        paren_depth++;
                    }
                    if (*src == ')')
                    {
                        paren_depth--;
                    }
                    if (paren_depth == 0)
                    {
                        break;
                    }
                    *dest++ = *src++;
                }

                if (has_args)
                {
                    *dest++ = ')';
                }
                else
                {
                    *dest++ = ')';
                }

                src++;
                continue;
            }
            else
            {
                dest -= strlen(acc);
                int is_ptr = (strchr(vtype, '*') != NULL);
                // Mixin Lookup Logic (No Parens)
                MixinResolution res = resolve_method_from_type_str(ctx, vtype, method);
                char *final_cast = res.final_cast;
                char *final_method = xstrdup(method);
                char *final_struct = res.final_struct;

                if (final_cast)
                {
                    char call_buf[MAX_ERROR_MSG_LEN];
                    snprintf(call_buf, sizeof(call_buf), "%s__%s", final_struct, final_method);
                    char *mangled_call = merge_underscores(call_buf);

                    dest += sprintf(dest, "%s(%s%s)", mangled_call, final_cast,
                                    acc); /* TODO: check buffer size */
                    zfree(final_cast);
                }
                else
                {
                    char call_buf[MAX_ERROR_MSG_LEN];
                    snprintf(call_buf, sizeof(call_buf), "%s__%s", final_struct, final_method);
                    char *mangled_call = merge_underscores(call_buf);

                    dest += sprintf(dest, "%s(%s%s)", mangled_call, is_ptr ? "" : "&",
                                    acc); /* TODO: check buffer size */
                }
                zfree(final_struct);
                zfree(final_method);
                continue;
            }
        }

        if (!in_expr && strncmp(src, "::", 2) == 0)
        {
            char acc[64];
            int i = 0;
            char *back = src - 1;
            while (back >= raw && (isalnum(*back) || *back == '_'))
            {
                back--;
            }
            back++;
            while (back < src && i < 63)
            {
                acc[i++] = *back++;
            }
            acc[i] = 0;

            src += 2;
            char field[64];
            i = 0;
            while (isalnum(*src) || *src == '_')
            {
                field[i++] = *src++;
            }
            field[i] = 0;

            dest -= strlen(acc);

            Module *mod = find_module(ctx, acc);
            if (mod && mod->is_c_header)
            {
                dest += sprintf(dest, "%s", field); /* TODO: check buffer size */
            }
            else
            {
                ASTNode *sdef = find_struct_def(ctx, acc);
                if (sdef && sdef->kind == NODE_ENUM)
                {
                    // For Enums, check if it's a variant
                    int is_variant = 0;
                    ASTNode *v = sdef->enm.variants;
                    while (v)
                    {
                        if (strcmp(v->variant.name, field) == 0)
                        {
                            is_variant = 1;
                            break;
                        }
                        v = v->next;
                    }
                    if (is_variant)
                    {
                        dest += sprintf(dest, "%s__%s", acc, field); /* TODO: check buffer size */
                    }
                    else
                    {
                        // Static method on Enum
                        dest += sprintf(dest, "%s__%s", acc, field); /* TODO: check buffer size */
                    }
                }
                else if (sdef || !mod)
                {
                    // Struct static method, or Type static method
                    dest += sprintf(dest, "%s__%s", acc, field); /* TODO: check buffer size */
                }
                else
                {
                    // Module function
                    dest += sprintf(dest, "%s__%s", acc, field); /* TODO: check buffer size */
                }
            }
            continue;
        }

        if (in_expr && isalpha(*src))
        {
            char tok[128];
            int i = 0;
            while ((isalnum(*src) || *src == '_') && i < 127)
            {
                tok[i++] = *src++;
            }
            tok[i] = 0;

            while (*src == ' ' || *src == '\t')
            {
                src++;
            }

            if (strncmp(src, "::", 2) == 0)
            {
                src += 2;
                char func_name[128];
                snprintf(func_name, sizeof(func_name), "%s", tok);
                char method[64];
                i = 0;
                while (isalnum(*src) || *src == '_')
                {
                    method[i++] = *src++;
                }
                method[i] = 0;

                while (*src == ' ' || *src == '\t')
                {
                    src++;
                }

                if (*src == '(')
                {
                    src++;

                    char mangled[MAX_MANGLED_NAME_LEN];

                    const char *aliased = find_type_alias(ctx, func_name);
                    const char *use_name = aliased ? aliased : func_name;

                    Module *mod = find_module(ctx, use_name);
                    if (mod)
                    {
                        if (mod->is_c_header)
                        {
                            snprintf(mangled, sizeof(mangled), "%s", method);
                        }
                        else
                        {
                            char mangled_raw[MAX_MANGLED_NAME_LEN];
                            snprintf(mangled_raw, sizeof(mangled_raw), "%s__%s", mod->base_name,
                                     method);
                            char *mangled_merged = merge_underscores(mangled_raw);
                            strncpy(mangled, mangled_merged, sizeof(mangled) - 1);
                            mangled[sizeof(mangled) - 1] = 0;
                        }
                    }
                    else
                    {
                        ASTNode *sdef = find_struct_def(ctx, use_name);
                        if (sdef)
                        {
                            char mangled_raw[MAX_MANGLED_NAME_LEN];
                            snprintf(mangled_raw, sizeof(mangled_raw), "%s__%s", use_name, method);
                            char *mangled_merged = merge_underscores(mangled_raw);
                            strncpy(mangled, mangled_merged, sizeof(mangled) - 1);
                            mangled[sizeof(mangled) - 1] = 0;
                        }
                        else
                        {
                            char mangled_raw[MAX_MANGLED_NAME_LEN];
                            snprintf(mangled_raw, sizeof(mangled_raw), "%s__%s", use_name, method);
                            char *mangled_merged = merge_underscores(mangled_raw);
                            strncpy(mangled, mangled_merged, sizeof(mangled) - 1);
                            mangled[sizeof(mangled) - 1] = 0;
                        }
                    }

                    if (*src == ')')
                    {
                        dest += sprintf(dest, "%s()", mangled); /* TODO: check buffer size */
                        src++;
                    }
                    else
                    {
                        FuncSig *sig = find_func(ctx, func_name);
                        if (sig)
                        {
                            dest += sprintf(dest, "%s(&(%s){0}", mangled,
                                            func_name); /* TODO: check buffer size */
                            while (*src && *src != ')')
                            {
                                *dest++ = *src++;
                            }
                            *dest++ = ')';
                            if (*src == ')')
                            {
                                src++;
                            }
                        }
                        else
                        {
                            dest += sprintf(dest, "%s(", mangled); /* TODO: check buffer size */
                            while (*src && *src != ')')
                            {
                                *dest++ = *src++;
                            }
                            *dest++ = ')';
                            if (*src == ')')
                            {
                                src++;
                            }
                        }
                    }
                    continue;
                }
            }

            strcpy(dest, tok);
            dest += strlen(tok);
            continue;
        }

        *dest++ = *src++;
    }

    *dest = 0;
    return result;
}

char *parse_and_convert_args(ParserContext *ctx, Lexer *l, char ***defaults_out,
                             ASTNode ***default_values_out, int *count_out, Type ***types_out,
                             char ***names_out, int *is_varargs_out, char ***ctype_overrides_out)
{
    Token t = lexer_next(l);
    if (t.kind != TOK_LPAREN)
    {
        zpanic_at(t, "Expected '(' in function args");
        return NULL;
    }

    size_t buf_size = 8192;
    char *buf = xmalloc((size_t)(buf_size));
    buf[0] = 0;
    int count = 0;
    int max_args = 16;
    char **defaults = xcalloc((size_t)(max_args), sizeof(char *));
    ASTNode **default_values = xcalloc((size_t)(max_args), sizeof(ASTNode *));
    Type **types = xcalloc((size_t)(max_args), sizeof(Type *));
    char **names = xcalloc((size_t)(max_args), sizeof(char *));
    char **ctype_overrides = xcalloc((size_t)(max_args), sizeof(char *));

    // Initial 16 entries already zeroed by xcalloc

    if (lexer_peek(l).kind != TOK_RPAREN)
    {
        while (1)
        {
            if (lexer_peek(l).kind == TOK_EOF)
            {
                break;
            }
            if (count >= max_args)
            {
                int new_max = max_args * 2;
                defaults = xrealloc(defaults, sizeof(char *) * (size_t)(new_max));
                memset(defaults + max_args, 0, sizeof(char *) * (size_t)(new_max - max_args));
                default_values = xrealloc(default_values, sizeof(ASTNode *) * (size_t)(new_max));
                memset(default_values + max_args, 0,
                       sizeof(ASTNode *) * (size_t)(new_max - max_args));
                types = xrealloc(types, sizeof(Type *) * (size_t)(new_max));
                memset(types + max_args, 0, sizeof(Type *) * (size_t)(new_max - max_args));
                names = xrealloc(names, sizeof(char *) * (size_t)(new_max));
                memset(names + max_args, 0, sizeof(char *) * (size_t)(new_max - max_args));
                ctype_overrides = xrealloc(ctype_overrides, sizeof(char *) * (size_t)(new_max));
                memset(ctype_overrides + max_args, 0,
                       sizeof(char *) * (size_t)(new_max - max_args));
                max_args = new_max;
            }

            // Check for @ctype("...") before parameter
            char *ctype_override = NULL;
            if (lexer_peek(l).kind == TOK_AT)
            {
                lexer_next(l); // eat @
                Token attr = lexer_next(l);
                if (attr.kind == TOK_IDENT && attr.len == 5 && strncmp(attr.start, "ctype", 5) == 0)
                {
                    if (lexer_next(l).kind != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after @ctype");
                        return NULL;
                    }
                    Token ctype_tok = lexer_next(l);
                    if (ctype_tok.kind != TOK_STRING)
                    {
                        zpanic_at(ctype_tok, "@ctype requires a string argument");
                        return NULL;
                    }
                    // Extract string content (strip quotes)
                    ctype_override = xmalloc(ctype_tok.len - 1);
                    strncpy(ctype_override, ctype_tok.start + 1, ctype_tok.len - 2);
                    ctype_override[ctype_tok.len - 2] = 0;
                    if (lexer_next(l).kind != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after @ctype string");
                        return NULL;
                    }
                }
                else
                {
                    zpanic_at(attr, "Unknown parameter attribute @%.*s", (int)attr.len, attr.start);
                    return NULL;
                }
            }

            int is_const_param = 0;
            Token param_tok = lexer_next(l);

            if (is_token(param_tok, "const"))
            {
                is_const_param = 1;
                param_tok = lexer_next(l);
            }

            // Handle 'self'
            if (is_token(param_tok, "self"))
            {
                names[count] = xstrdup("self");
                if (ctx->current_impl_struct)
                {
                    char *buf_type = xmalloc(strlen(ctx->current_impl_struct) + 2);
                    sprintf(buf_type, "%s*", ctx->current_impl_struct); /* safe */

                    if (is_primitive_type_name(ctx->current_impl_struct))
                    {
                        // Primitives: self is a pointer in signature and body
                        TypeKind pk = get_primitive_type_kind(ctx->current_impl_struct);
                        Type *bt = type_new(pk);
                        if (pk == TYPE_STRUCT)
                        { // Fallback if get_primitive_type_kind failed for some reason
                            bt->name = xstrdup(ctx->current_impl_struct);
                        }
                        bt->is_const = is_const_param;
                        Type *ptr = type_new_ptr(bt);

                        add_symbol(ctx, "self", buf_type, ptr, 0);
                        types[count] = ptr;
                    }
                    else
                    {
                        // Structs: self is a pointer in signature and body
                        Type *st = type_new(TYPE_STRUCT);
                        st->name = xstrdup(ctx->current_impl_struct);
                        st->is_const = is_const_param;
                        Type *ptr = type_new_ptr(st);

                        add_symbol(ctx, "self", buf_type, ptr, 0);
                        types[count] = ptr;
                    }
                    zfree(buf_type);
                    if (is_const_param)
                    {
                        strcat(buf, "const void* self");
                    }
                    else
                    {
                        strcat(buf, "void* self");
                    }
                }
                else
                {
                    if (is_const_param)
                    {
                        strcat(buf, "const void* self");
                    }
                    else
                    {
                        strcat(buf, "void* self");
                    }
                    Type *void_type = type_new(TYPE_VOID);
                    void_type->is_const = is_const_param;
                    types[count] = type_new_ptr(void_type);
                    add_symbol(ctx, "self", is_const_param ? "const void*" : "void*", types[count],
                               0);
                }
                ctype_overrides[count] = ctype_override;
                count++;
            }
            else
            {
                if (param_tok.kind != TOK_IDENT)
                {
                    zpanic_at(lexer_peek(l), "Expected arg name");
                    return NULL;
                }
                check_identifier(param_tok);
                char *name = token_strdup(param_tok);
                names[count] = name; // Store name
                if (lexer_next(l).kind != TOK_COLON)
                {
                    zpanic_at(lexer_peek(l), "Expected ':'");
                    return NULL;
                }

                Type *arg_type = parse_type_formal(ctx, l);
                if (!arg_type)
                {
                    return NULL;
                }
                if (is_const_param)
                {
                    arg_type->is_const = 1;
                }
                char *type_str = type_to_string(arg_type);

                add_symbol(ctx, name, type_str, arg_type, 0);
                types[count] = arg_type;

                if (strlen(buf) > 0)
                {
                    strcat(buf, ", ");
                }

                // Ensure buf has enough space before appending
                size_t needed = strlen(buf) + strlen(type_str) + strlen(name) + 32;
                if (needed > buf_size)
                {
                    while (needed > buf_size)
                    {
                        buf_size *= 2;
                    }
                    buf = xrealloc(buf, buf_size);
                }

                char *fn_ptr = (char *)strstr(type_str, "(*)");
                if (get_inner_type(arg_type)->kind == TYPE_FUNCTION)
                {
                    strcat(buf, "z_closure_T ");
                    strcat(buf, name);
                }
                else if (fn_ptr)
                {
                    // Inject name into function pointer: int (*)(int) -> int (*name)(int)
                    ptrdiff_t prefix_len = fn_ptr - type_str;
                    strncat(buf, type_str, (size_t)(prefix_len));
                    strcat(buf, " (*");
                    strcat(buf, name);
                    strcat(buf, ")");
                    strcat(buf, fn_ptr + 3); // Skip "(*)"
                }
                else
                {
                    // Use @ctype override if present
                    if (ctype_override)
                    {
                        strcat(buf, ctype_override);
                    }
                    else
                    {
                        strcat(buf, type_str);
                    }
                    strcat(buf, " ");
                    strcat(buf, name);
                }

                ctype_overrides[count] = ctype_override;
                count++;

                if (lexer_peek(l).kind == TOK_OP && is_token(lexer_peek(l), "="))
                {
                    lexer_next(l); // consume =

                    // Parse the expression into an AST node
                    ASTNode *def_node = parse_expression(ctx, l);

                    // Store both the AST node and the reconstructed string for legacy support
                    default_values[count - 1] = def_node;
                    defaults[count - 1] = ast_to_string(def_node);
                }
            }
            if (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
                // Check if next is ...
                if (lexer_peek(l).kind == TOK_ELLIPSIS)
                {
                    lexer_next(l);
                    if (is_varargs_out)
                    {
                        *is_varargs_out = 1;
                    }
                    if (strlen(buf) > 0)
                    {
                        strcat(buf, ", ");
                    }
                    strcat(buf, "...");
                    break; // Must be last
                }
            }
            else
            {
                break;
            }
        }
    }
    if (lexer_next(l).kind != TOK_RPAREN)
    {
        zpanic_at(lexer_peek(l), "Expected ')' after args");
        return NULL;
    }

    *defaults_out = defaults;
    *default_values_out = default_values;
    *count_out = count;
    *types_out = types;
    *names_out = names;
    if (ctype_overrides_out)
    {
        *ctype_overrides_out = ctype_overrides;
    }
    return buf;
}

// Helper to find similar symbol name in current scope
char *find_similar_symbol(ParserContext *ctx, const char *name)
{
    if (!ctx->current_scope)
    {
        return NULL;
    }

    const char *best_match = NULL;
    int best_dist = 999;

    // Check local scopes
    Scope *s = ctx->current_scope;
    while (s)
    {
        ZenSymbol *sym = s->symbols;
        while (sym)
        {
            int dist = levenshtein(name, sym->name);
            if (dist < best_dist && dist <= 3)
            {
                best_dist = dist;
                best_match = sym->name;
            }
            sym = sym->next;
        }
        s = s->parent;
    }

    // Check builtins/globals if any (simplified)
    return best_match ? xstrdup(best_match) : NULL;
}

const char *get_closest_type_hint(ParserContext *ctx, const char *name)
{
    int best_dist = 4;
    const char *best = NULL;

    StructDef *def = ctx->struct_defs;
    while (def)
    {
        int dist = levenshtein(name, def->name);
        if (dist < best_dist)
        {
            best_dist = dist;
            best = def->name;
        }
        def = def->next;
    }

    StructRef *er = ctx->parsed_enums_list;
    while (er)
    {
        if (er->node && er->node->kind == NODE_ENUM)
        {
            int dist = levenshtein(name, er->node->enm.name);
            if (dist < best_dist)
            {
                best_dist = dist;
                best = er->node->enm.name;
            }
        }
        er = er->next;
    }

    TypeAlias *ta = ctx->type_aliases;
    while (ta)
    {
        int dist = levenshtein(name, ta->alias);
        if (dist < best_dist)
        {
            best_dist = dist;
            best = ta->alias;
        }
        ta = ta->next;
    }

    return best;
}

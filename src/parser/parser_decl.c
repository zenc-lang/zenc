
#include "parser.h"
#include <ctype.h>
#include "analysis/const_fold.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../ast/ast.h"
#include "../plugins/plugin_manager.h"
#include "../zen/zen_facts.h"
#include "zprep_plugin.h"
#include "../codegen/codegen.h"
#include "analysis/move_check.h"

ASTNode *parse_function(ParserContext *ctx, Lexer *l, int is_async)
{
    lexer_next(l);
    Token name_tok = lexer_next(l);
    check_identifier(ctx, name_tok);
    char *name = token_strdup(name_tok);

    if (is_async)
    {
        ctx->has_async = 1;
    }

    // Check for C reserved word conflict
    if (is_c_reserved_word(name))
    {
        warn_c_reserved_word(name_tok, name);
    }

    char *gen_param = NULL;
    if (lexer_peek(l).type == TOK_LANGLE)
    {
        lexer_next(l);

        size_t buf_size = 1024;
        char *buf = xmalloc(buf_size);
        buf[0] = 0;

        while (1)
        {
            Token gt = lexer_next(l);
            if (gt.type != TOK_IDENT)
            {
                zpanic_at(gt, "Expected generic parameter name");
            }
            char *s = token_strdup(gt);

            if (strlen(buf) + strlen(s) + 2 >= buf_size)
            {
                buf_size *= 2;
                buf = xrealloc(buf, buf_size);
            }

            if (buf[0])
            {
                strcat(buf, ",");
            }
            strcat(buf, s);

            // Check for shadowing
            if (is_known_generic(ctx, s))
            {
                zpanic_at(gt, "Generic parameter '%s' shadows an existing generic parameter", s);
            }

            free(s);

            if (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }
            break;
        }

        if (lexer_next(l).type != TOK_RANGLE)
        {
            zpanic_at(lexer_peek(l), "Expected >");
        }
        gen_param = xstrdup(buf);
        free(buf);
    }

    // Register generic parameters so type parsing recognizes them
    int saved_generic_count = ctx->known_generics_count;
    if (gen_param)
    {
        char *tmp = xstrdup(gen_param);
        char *tok = strtok(tmp, ",");
        while (tok)
        {
            register_generic(ctx, tok);
            tok = strtok(NULL, ",");
        }
        free(tmp);
    }

    enter_scope(ctx);
    char **defaults;
    ASTNode **default_values;
    int count;
    Type **arg_types;
    char **param_names;
    char **ctype_overrides;
    int is_varargs = 0;

    char *args = parse_and_convert_args(ctx, l, &defaults, &default_values, &count, &arg_types,
                                        &param_names, &is_varargs, &ctype_overrides);

    char *ret = "void";
    Type *ret_type_obj = type_new(TYPE_VOID);

    if (strcmp(name, "main") == 0)
    {
        ret = "int";
        ret_type_obj = type_new(TYPE_C_INT);
    }

    if (lexer_peek(l).type == TOK_ARROW)
    {
        lexer_next(l);
        ret_type_obj = parse_type_formal(ctx, l);
        ret = type_to_string(ret_type_obj);
    }
    else if (lexer_peek(l).type == TOK_COLON)
    {
        zpanic_at(lexer_peek(l), "Functions use '->' for the return type, not ':'");
    }

    extern char *curr_func_ret;
    curr_func_ret = ret;

    // Auto-prefix function name if in module context
    // Don't prefix generic templates or functions inside impl blocks (already
    // mangled)
    if (ctx->current_module_prefix && !gen_param && !ctx->current_impl_struct)
    {
        char *prefixed_name = xmalloc(strlen(ctx->current_module_prefix) + strlen(name) + 3);
        sprintf(prefixed_name, "%s__%s", ctx->current_module_prefix, name);
        free(name);
        name = prefixed_name;
    }

    // Register if concrete (Global functions only)
    if (!gen_param && !ctx->current_impl_struct)
    {
        register_func(ctx, ctx->current_scope->parent, name, count, defaults, arg_types,
                      ret_type_obj, is_varargs, is_async, 0, name_tok);
        // Note: required is set after return by caller (parser_core.c)
    }

    ASTNode *body = NULL;
    Token next_tok = lexer_peek(l);
    if (next_tok.type == TOK_SEMICOLON)
    {
        lexer_next(l); // consume ;
    }
    else if (next_tok.type == TOK_LBRACE)
    {
        // Set self context flags for .member shorthand in methods with self
        int prev_in_method = ctx->in_method_with_self;
        int prev_self_ptr = ctx->self_is_pointer;
        if (args && strstr(args, "self"))
        {
            ctx->in_method_with_self = 1;
            ctx->self_is_pointer = (strstr(args, "self*") != NULL);
        }

        body = parse_block(ctx, l);

        // Restore previous state
        ctx->in_method_with_self = prev_in_method;
        ctx->self_is_pointer = prev_self_ptr;
    }
    else
    {
        zpanic_at(next_tok, "Expected '{' or ';' after function signature");
    }

    // Check for unused parameters
    // The current scope contains arguments (since parse_block creates a new child
    // scope for body) Only check if we parsed a body (not a prototype) function
    if (body && ctx->current_scope)
    {
        ZenSymbol *sym = ctx->current_scope->symbols;
        while (sym)
        {
            // Check if unused and not prefixed with '_' (conventional ignore)
            // also ignore 'self' as it is often mandated by traits
            if (!sym->is_used && sym->name[0] != '_' && strcmp(sym->name, "self") != 0 &&
                strcmp(name, "main") != 0)
            {
                warn_unused_parameter(sym->decl_token, sym->name, name);
            }
            sym = sym->next;
        }
    }

    exit_scope(ctx);

    // Restore generic count to unregister function-scoped generics
    ctx->known_generics_count = saved_generic_count;

    curr_func_ret = NULL;

    ASTNode *node = ast_create(NODE_FUNCTION);
    node->token = name_tok; // Save definition location
    node->func.name = name;
    node->func.args = args;
    node->func.ret_type = ret;
    node->func.body = body;

    node->func.arg_types = arg_types;
    node->func.param_names = param_names;
    node->func.arg_count = count;
    node->func.defaults = defaults;
    node->func.default_values = default_values;
    node->func.ret_type_info = ret_type_obj;
    node->func.is_varargs = is_varargs;
    node->func.is_async = is_async;
    node->func.c_type_overrides = ctype_overrides;

    if (gen_param)
    {
        node->func.generic_params = xstrdup(gen_param);
        if (!ctx->current_impl_struct)
        {
            register_func_template(ctx, name, gen_param, node);
            return NULL;
        }
    }
    if (!ctx->current_impl_struct)
    {
        add_to_func_list(ctx, node);
    }
    return node;
}

char *patch_self_args(const char *args, const char *struct_name)
{
    if (!args)
    {
        return NULL;
    }

    // Sanitize struct name for C usage (Vec<T> -> Vec_T)
    char *safe_name = xmalloc(strlen(struct_name) + 1);
    int j = 0;
    for (int i = 0; struct_name[i]; i++)
    {
        if (struct_name[i] == '<')
        {
            safe_name[j++] = '_';
        }
        else if (struct_name[i] == '>')
        {
            // skip
        }
        else if (struct_name[i] == ' ')
        {
            // skip
        }
        else
        {
            safe_name[j++] = struct_name[i];
        }
    }
    safe_name[j] = 0;

    char *new_args = xmalloc(strlen(args) + strlen(safe_name) + 10);

    // Check if it starts with "void* self"
    if (strncmp(args, "void* self", 10) == 0)
    {
        sprintf(new_args, "%s* self%s", safe_name, args + 10);
    }
    else
    {
        strcpy(new_args, args);
    }
    free(safe_name);
    return new_args;
}
// Helper for Value-Returning Defer
static void replace_it_with_var(ASTNode *node, char *var_name)
{
    if (!node)
    {
        return;
    }
    if (node->type == NODE_EXPR_VAR)
    {
        if (strcmp(node->var_ref.name, "it") == 0)
        {
            // Replace 'it' with var_name
            node->var_ref.name = xstrdup(var_name);
        }
    }
    else if (node->type == NODE_EXPR_CALL)
    {
        replace_it_with_var(node->call.callee, var_name);
        ASTNode *arg = node->call.args;
        while (arg)
        {
            replace_it_with_var(arg, var_name);
            arg = arg->next;
        }
    }
    else if (node->type == NODE_EXPR_MEMBER)
    {
        replace_it_with_var(node->member.target, var_name);
    }
    else if (node->type == NODE_EXPR_BINARY)
    {
        replace_it_with_var(node->binary.left, var_name);
        replace_it_with_var(node->binary.right, var_name);
    }
    else if (node->type == NODE_EXPR_UNARY)
    {
        replace_it_with_var(node->unary.operand, var_name);
    }
    else if (node->type == NODE_BLOCK)
    {
        ASTNode *s = node->block.statements;
        while (s)
        {
            replace_it_with_var(s, var_name);
            s = s->next;
        }
    }
}

ASTNode *parse_var_decl(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l); // eat 'var'

    // Destructuring: var {x, y} = ... OR var (a: type, b: type) = ...
    if (lexer_peek(l).type == TOK_LBRACE || lexer_peek(l).type == TOK_LPAREN)
    {
        int is_struct = (lexer_peek(l).type == TOK_LBRACE);
        lexer_next(l);
        char **names = xmalloc(16 * sizeof(char *));
        char **types = xmalloc(16 * sizeof(char *));
        Type **type_infos = xmalloc(16 * sizeof(Type *));
        int count = 0;
        while (1)
        {
            Token t = lexer_next(l);
            check_identifier(ctx, t);
            char *nm = token_strdup(t);
            names[count] = nm;
            types[count] = NULL;
            type_infos[count] = NULL;

            // Check for optional type annotation: name: type
            if (!is_struct && lexer_peek(l).type == TOK_COLON)
            {
                lexer_next(l); // eat :
                Type *type_obj = parse_type_formal(ctx, l);
                types[count] = type_to_string(type_obj);
                type_infos[count] = type_obj;
                add_symbol(ctx, nm, types[count], type_obj);
            }
            else
            {
                add_symbol(ctx, nm, "unknown", NULL);
            }
            count++;

            Token next = lexer_next(l);
            if (next.type == (is_struct ? TOK_RBRACE : TOK_RPAREN))
            {
                break;
            }
            if (next.type != TOK_COMMA)
            {
                zpanic_at(next, "Expected comma");
            }
        }
        if (lexer_next(l).type != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected =");
        }
        ASTNode *init = parse_expression(ctx, l);
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }
        ASTNode *n = ast_create(NODE_DESTRUCT_VAR);
        n->token = tk;
        n->destruct.names = names;
        n->destruct.types = types;
        n->destruct.type_infos = type_infos;
        n->destruct.count = count;
        n->destruct.init_expr = init;
        n->destruct.is_struct_destruct = is_struct;
        return n;
    }

    // Normal Declaration OR Named Struct Destructuring
    Token name_tok = lexer_next(l);
    check_identifier(ctx, name_tok);
    char *name = token_strdup(name_tok);

    // Check for Struct Destructuring: var Point { x, y }
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        lexer_next(l); // eat {
        char **names = xmalloc(16 * sizeof(char *));
        char **fields = xmalloc(16 * sizeof(char *));
        int count = 0;

        while (1)
        {
            // Parse field:name or just name
            Token t = lexer_next(l);
            check_identifier(ctx, t);
            char *ident = token_strdup(t);

            if (lexer_peek(l).type == TOK_COLON)
            {
                // field: var_name
                lexer_next(l); // eat :
                Token v = lexer_next(l);
                check_identifier(ctx, v);
                fields[count] = ident;
                names[count] = token_strdup(v);
            }
            else
            {
                // Shorthand: field (implies var name = field)
                fields[count] = ident;
                names[count] = ident; // Share pointer or duplicate? duplicate safer if we free
            }
            // Register symbol for variable
            add_symbol(ctx, names[count], "unknown", NULL);

            count++;

            Token next = lexer_next(l);
            if (next.type == TOK_RBRACE)
            {
                break;
            }
            if (next.type != TOK_COMMA)
            {
                zpanic_at(next, "Expected comma in struct pattern");
            }
        }

        if (lexer_next(l).type != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected =");
        }
        ASTNode *init = parse_expression(ctx, l);
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        ASTNode *n = ast_create(NODE_DESTRUCT_VAR);
        n->token = name_tok;
        n->destruct.names = names;
        n->destruct.field_names = fields;
        n->destruct.count = count;
        n->destruct.init_expr = init;
        n->destruct.is_struct_destruct = 1;
        n->destruct.struct_name = name; // "Point"
        return n;
    }

    // Check for Guard Pattern: var Some(val) = opt else { ... }
    if (lexer_peek(l).type == TOK_LPAREN)
    {
        lexer_next(l); // eat (
        Token val_tok = lexer_next(l);
        check_identifier(ctx, val_tok);
        char *val_name = token_strdup(val_tok);

        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), "Expected ')' in guard pattern");
        }

        if (lexer_next(l).type != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected '=' after guard pattern");
        }

        ASTNode *init = parse_expression(ctx, l);

        Token t = lexer_next(l);
        if (t.type != TOK_IDENT || strncmp(t.start, "else", 4) != 0)
        {
            zpanic_at(t, "Expected 'else' in guard statement");
        }

        ASTNode *else_blk;
        if (lexer_peek(l).type == TOK_LBRACE)
        {
            else_blk = parse_block(ctx, l);
        }
        else
        {
            else_blk = ast_create(NODE_BLOCK);
            else_blk->block.statements = parse_statement(ctx, l);
        }

        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        ASTNode *n = ast_create(NODE_DESTRUCT_VAR);
        n->token = t;
        n->destruct.names = xmalloc(sizeof(char *));
        n->destruct.names[0] = val_name;
        n->destruct.count = 1;
        n->destruct.init_expr = init;
        n->destruct.is_guard = 1;
        n->destruct.guard_variant = name;
        n->destruct.else_block = else_blk;

        add_symbol(ctx, val_name, "unknown", NULL);

        return n;
    }

    char *type = NULL;
    Type *type_obj = NULL; // --- NEW: Formal Type Object ---

    if (lexer_peek(l).type == TOK_COLON)
    {
        lexer_next(l);
        // Hybrid Parse: Get Object AND String
        type_obj = parse_type_formal(ctx, l);
        type = type_to_string(type_obj);
    }

    ASTNode *init = NULL;
    if (lexer_peek(l).type == TOK_OP && is_token(lexer_peek(l), "="))
    {
        lexer_next(l);

        // Peek for special initializers
        Token next = lexer_peek(l);
        if (next.type == TOK_IDENT && strncmp(next.start, "embed", 5) == 0)
        {
            init = parse_embed(ctx, l);

            if (!type && init->type_info)
            {
                type = type_to_string(init->type_info);
            }
            if (!type)
            {
                register_slice(ctx, "char");
                type = xstrdup("Slice__char");
            }
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else if (next.type == TOK_LBRACKET && type && strncmp(type, "Slice__", 7) == 0)
        {
            char *code = parse_array_literal(ctx, l, type);
            init = ast_create(NODE_RAW_STMT);
            init->token = next;
            init->raw_stmt.content = code;
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else if (next.type == TOK_LPAREN && type && strncmp(type, "Tuple__", 7) == 0)
        {
            char *code = parse_tuple_literal(ctx, l, type);
            init = ast_create(NODE_RAW_STMT);
            init->token = next;
            init->raw_stmt.content = code;
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else
        {
            init = parse_expression(ctx, l);
        }

        if (init && type)
        {
            char *rhs_type = init->resolved_type;
            if (!rhs_type && init->type_info)
            {
                rhs_type = type_to_string(init->type_info);
            }

            if (rhs_type && strchr(type, '*') && strchr(rhs_type, '*'))
            {
                // Strip stars to get struct names
                char target_struct[256];
                strcpy(target_struct, type);
                target_struct[strlen(target_struct) - 1] = 0;
                char source_struct[256];
                strcpy(source_struct, rhs_type);
                source_struct[strlen(source_struct) - 1] = 0;

                ASTNode *def = find_struct_def(ctx, source_struct);

                if (def && def->strct.parent && strcmp(def->strct.parent, target_struct) == 0)
                {
                    // Create Cast Node
                    ASTNode *cast = ast_create(NODE_EXPR_CAST);
                    cast->cast.target_type = xstrdup(type);
                    cast->cast.expr = init;
                    cast->type_info = type_obj; // Inherit formal type

                    init = cast; // Replace init with cast
                }
            }
        }

        // ** Type Inference Logic **
        if (!type && init)
        {
            if (init->type_info)
            {
                // Create new type to avoid inheriting is_const from builtins like true/false
                type_obj = type_new(init->type_info->kind);
                if (init->type_info->name)
                {
                    type_obj->name = xstrdup(init->type_info->name);
                }
                if (init->type_info->inner)
                {
                    type_obj->inner = init->type_info->inner; // Shallow copy for inner
                }
                if (init->type_info->kind == TYPE_ALIAS)
                {
                    type_obj->alias = init->type_info->alias;
                }
                // Copy function type args for lambda/closure support
                if (init->type_info->args && init->type_info->arg_count > 0)
                {
                    type_obj->args = init->type_info->args;
                    type_obj->arg_count = init->type_info->arg_count;
                    type_obj->is_varargs = init->type_info->is_varargs;
                }
                type_obj->array_size = init->type_info->array_size;
                type_obj->is_raw = init->type_info->is_raw;
                type_obj->is_explicit_struct = init->type_info->is_explicit_struct;
                type = type_to_string(type_obj);
            }
            else if (init->type == NODE_EXPR_SLICE)
            {
                zpanic_at(init->token, "Slice Node has NO Type Info!");
            }
            // Fallbacks for literals
            else if (init->type == NODE_EXPR_LITERAL)
            {
                if (init->literal.type_kind == LITERAL_INT)
                {
                    type = xstrdup("int");
                    type_obj = type_new(TYPE_INT);
                }
                else if (init->literal.type_kind == LITERAL_FLOAT)
                {
                    type = xstrdup("float");
                    type_obj = type_new(TYPE_FLOAT);
                }
                else if (init->literal.type_kind == LITERAL_STRING)
                {
                    type = xstrdup("string");
                    type_obj = type_new(TYPE_STRING);
                }
            }
            else if (init->type == NODE_EXPR_STRUCT_INIT)
            {
                type = xstrdup(init->struct_init.struct_name);
                type_obj = type_new(TYPE_STRUCT);
                type_obj->name = xstrdup(type);
            }
        }
    }

    if (!type && !init)
    {
        zpanic_at(name_tok, "Variable '%s' requires a type or initializer", name);
    }

    // Register in symbol table with actual token
    add_symbol_with_token(ctx, name, type, type_obj, name_tok);

    if (init && type_obj)
    {
        Type *t = init->type_info;
        if (!t && init->type == NODE_EXPR_VAR)
        {
            t = find_symbol_type_info(ctx, init->var_ref.name);
        }

        // Literal type construction for validation
        Type *temp_literal_type = NULL;
        if (!t && init->type == NODE_EXPR_LITERAL)
        {
            if (init->literal.type_kind == LITERAL_INT)
            {
                temp_literal_type = type_new(TYPE_INT);
            }
            else if (init->literal.type_kind == LITERAL_FLOAT)
            {
                temp_literal_type = type_new(TYPE_FLOAT);
            }
            else if (init->literal.type_kind == LITERAL_STRING)
            {
                temp_literal_type = type_new(TYPE_STRING);
            }
            else if (init->literal.type_kind == LITERAL_CHAR)
            {
                temp_literal_type = type_new(TYPE_CHAR);
            }
            t = temp_literal_type;
        }

        // Special case for literals: if implicit conversion works
        if (t && !type_eq(type_obj, t))
        {
            // Allow integer compatibility if types are roughly ints (lax check in type_eq handles
            // most, but let's be safe)
            if (!check_opaque_alias_compat(ctx, type_obj, t))
            {
                char *expected = type_to_string(type_obj);
                char *got = type_to_string(t);
                zpanic_at(init->token, "Type validation failed. Expected '%s', but got '%s'",
                          expected, got);
                free(expected);
                free(got);
            }
        }

        if (temp_literal_type)
        {
            free(temp_literal_type); // Simple free, shallow
        }
    }

    // NEW: Capture Const Integer Values
    if (init && init->type == NODE_EXPR_LITERAL && init->literal.type_kind == LITERAL_INT)
    {
        ZenSymbol *s = find_symbol_entry(ctx, name); // Helper to find the struct
        if (s)
        {
            s->is_const_value = 1;
            s->const_int_val = init->literal.int_val;
        }
    }

    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *n = ast_create(NODE_VAR_DECL);
    n->token = name_tok; // Save location
    n->var_decl.name = name;
    n->var_decl.type_str = type;
    n->type_info = type_obj;

    // Auto-construct Trait Object
    if (type && is_trait(type))
    {
        init = transform_to_trait_object(ctx, type, init);
    }

    n->var_decl.init_expr = init;

    // Move Semantics Logic for Initialization
    if (init && init->type == NODE_EXPR_VAR)
    {
        // Move semantics placeholder: find_symbol_entry(ctx, init->var_ref.name);
    }

    // Global detection: Either no scope (yet) OR root scope (no parent)
    if (!ctx->current_scope || !ctx->current_scope->parent)
    {
        add_to_global_list(ctx, n);
    }

    // Check for 'defer' (Value-Returning Defer)
    // Only capture if it is NOT a block defer (defer { ... })
    // If it is a block defer, we leave it for the next parse_statement call.
    if (lexer_peek(l).type == TOK_DEFER)
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead); // Eat defer
        if (lexer_peek(&lookahead).type != TOK_LBRACE)
        {
            // Proceed to consume
            tk = lexer_next(l); // eat defer (real)

            // Parse the defer expression/statement
            // Usually defer close(it);
            // We parse expression.
            ASTNode *expr = parse_expression(ctx, l);

            // Handle "it" substitution
            replace_it_with_var(expr, name);

            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }

            ASTNode *d = ast_create(NODE_DEFER);
            d->token = tk;
            d->defer_stmt.stmt = expr;

            // Chain it: var_decl -> defer
            n->next = d;
        }
    }

    return n;
}

ASTNode *parse_def(ParserContext *ctx, Lexer *l)
{
    lexer_next(l); // eat def
    Token n = lexer_next(l);

    char *type_str = NULL;
    Type *type_obj = NULL;

    if (lexer_peek(l).type == TOK_COLON)
    {
        lexer_next(l);
        // Hybrid Parse
        type_obj = parse_type_formal(ctx, l);
        type_str = type_to_string(type_obj);
    }

    char *ns = token_strdup(n);
    if (!type_obj)
    {
        type_obj = type_new(TYPE_UNKNOWN); // Ensure we have an object
    }
    type_obj->is_const = 1;

    // Use is_def flag for manifest constants
    add_symbol(ctx, ns, type_str ? type_str : "unknown", type_obj);
    ZenSymbol *sym_entry = find_symbol_entry(ctx, ns);
    if (sym_entry)
    {
        sym_entry->is_def = 1;
        // is_const_value set only if literal
    }

    ASTNode *i = 0;
    if (lexer_peek(l).type == TOK_OP && is_token(lexer_peek(l), "="))
    {
        lexer_next(l);

        Token tk = lexer_peek(l);
        if (tk.type == TOK_LPAREN && type_str && strncmp(type_str, "Tuple__", 7) == 0)
        {
            char *code = parse_tuple_literal(ctx, l, type_str);
            i = ast_create(NODE_RAW_STMT);
            i->token = tk;
            i->raw_stmt.content = code;
        }
        else
        {
            i = parse_expression(ctx, l);

            // Try to evaluate constant expression for symbol table
            long long val;
            if (eval_const_int_expr(i, ctx, &val))
            {
                ZenSymbol *s = find_symbol_entry(ctx, ns);
                if (s)
                {
                    s->is_const_value = 1;
                    s->const_int_val = (int)val;
                    s->is_def = 1;

                    // Auto-infer type for def if unknown
                    if (!s->type_name || strcmp(s->type_name, "unknown") == 0)
                    {
                        if (s->type_name)
                        {
                            free(s->type_name);
                        }
                        s->type_name = xstrdup("int");
                        if (s->type_info)
                        {
                            free(s->type_info);
                        }
                        s->type_info = type_new(TYPE_INT);
                        s->type_info->is_const = 1;
                    }
                }
            }
        }
    }

    else
    {
        zpanic_at(n, "'def' constants must be initialized");
    }

    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *o = ast_create(NODE_CONST);
    o->token = n;
    o->var_decl.name = ns;
    o->var_decl.type_str = type_str;
    o->var_decl.init_expr = i;
    // Store extra metadata if needed, but NODE_CONST usually suffices

    if (!ctx->current_scope || !ctx->current_scope->parent)
    {
        add_to_global_list(ctx, o);
    }

    return o;
}

ASTNode *parse_type_alias(ParserContext *ctx, Lexer *l, int is_opaque)
{
    lexer_next(l); // consume 'type' or 'alias'
    Token n = lexer_next(l);
    if (n.type != TOK_IDENT)
    {
        zpanic_at(n, "Expected identifier for type alias");
    }

    lexer_next(l); // consume '='

    Type *t = parse_type_formal(ctx, l);
    char *o = type_to_string(t);

    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *node = ast_create(NODE_TYPE_ALIAS);
    node->type_alias.alias = xmalloc(n.len + 1);
    strncpy(node->type_alias.alias, n.start, n.len);
    node->type_alias.alias[n.len] = 0;
    node->type_alias.original_type = o;
    node->type_info = t;
    node->type_alias.is_opaque = is_opaque;
    node->type_alias.defined_in_file = g_current_filename ? xstrdup(g_current_filename) : NULL;

    register_type_alias(ctx, node->type_alias.alias, o, t, is_opaque,
                        node->type_alias.defined_in_file);

    return node;
}

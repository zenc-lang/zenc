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

void replace_it_with_var(ASTNode *node, char *var_name)
{
    if (!node)
    {
        return;
    }
    if (node->kind == NODE_EXPR_VAR)
    {
        if (strcmp(node->var_ref.name, "it") == 0)
        {
            // Replace 'it' with var_name
            node->var_ref.name = xstrdup(var_name);
        }
    }
    else if (node->kind == NODE_EXPR_CALL)
    {
        replace_it_with_var(node->call.callee, var_name);
        ASTNode *arg = node->call.args;
        while (arg)
        {
            replace_it_with_var(arg, var_name);
            arg = arg->next;
        }
    }
    else if (node->kind == NODE_EXPR_MEMBER)
    {
        replace_it_with_var(node->member.target, var_name);
    }
    else if (node->kind == NODE_EXPR_BINARY)
    {
        replace_it_with_var(node->binary.left, var_name);
        replace_it_with_var(node->binary.right, var_name);
    }
    else if (node->kind == NODE_EXPR_UNARY)
    {
        replace_it_with_var(node->unary.operand, var_name);
    }
    else if (node->kind == NODE_BLOCK)
    {
        ASTNode *s = node->block.statements;
        while (s)
        {
            replace_it_with_var(s, var_name);
            s = s->next;
        }
    }
}

ASTNode *parse_var_decl(ParserContext *ctx, Lexer *l, int is_export)
{
    Token tk = lexer_next(l); // eat 'let'

    // Destructuring: let {x, y} = ... OR let (a: type, b: type) = ...
    if (lexer_peek(l).kind == TOK_LBRACE || lexer_peek(l).kind == TOK_LPAREN)
    {
        int is_struct = (lexer_peek(l).kind == TOK_LBRACE);
        lexer_next(l);
        int cap = 16;
        char **names = xmalloc((size_t)cap * sizeof(char *));
        char **types = xmalloc((size_t)cap * sizeof(char *));
        Type **type_infos = xmalloc((size_t)cap * sizeof(Type *));
        int count = 0;
        while (1)
        {
            if (count >= cap)
            {
                cap *= 2;
                names = xrealloc(names, (size_t)cap * sizeof(char *));
                types = xrealloc(types, (size_t)cap * sizeof(char *));
                type_infos = xrealloc(type_infos, (size_t)cap * sizeof(Type *));
            }
            Token t = lexer_next(l);
            check_identifier(t);
            char *nm = token_strdup(t);
            names[count] = nm;
            types[count] = NULL;
            type_infos[count] = NULL;

            // Check for optional type annotation: name: type
            if (!is_struct && lexer_peek(l).kind == TOK_COLON)
            {
                lexer_next(l); // eat :
                Type *type_obj = parse_type_formal(ctx, l);
                types[count] = type_obj ? type_to_string(type_obj) : xstrdup("unknown");
                type_infos[count] = type_obj;
                add_symbol(ctx, nm, types[count], type_obj, is_export);
            }
            else
            {
                add_symbol(ctx, nm, "unknown", NULL, is_export);
            }
            count++;

            Token next = lexer_next(l);
            if (next.kind == TOK_EOF)
            {
                zpanic_at(next, "Unexpected end of file in destructuring pattern");
                break;
            }
            if (next.kind == (is_struct ? TOK_RBRACE : TOK_RPAREN))
            {
                break;
            }
            if (next.kind != TOK_COMMA)
            {
                zpanic_at(next, "Expected comma in destructuring list");
                return NULL;
            }
        }
        if (lexer_next(l).kind != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected =");
            return NULL;
        }
        ASTNode *init = parse_expression(ctx, l);
        if (lexer_peek(l).kind == TOK_SEMICOLON)
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
    check_identifier(name_tok);
    char *name = token_strdup(name_tok);

    // Check for Struct Destructuring: var Point { x, y }
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        lexer_next(l);
        char **names = xmalloc(16 * sizeof(char *));
        char **fields = xmalloc(16 * sizeof(char *));
        int count = 0;

        while (1)
        {
            // Parse field:name or just name
            Token t = lexer_next(l);
            check_identifier(t);
            char *ident = token_strdup(t);

            if (lexer_peek(l).kind == TOK_COLON)
            {
                // field: var_name
                lexer_next(l); // eat :
                Token v = lexer_next(l);
                check_identifier(v);
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
            add_symbol(ctx, names[count], "unknown", NULL, is_export);

            count++;

            Token next = lexer_next(l);
            if (next.kind == TOK_EOF)
            {
                zpanic_at(next, "Unexpected end of file in struct destructuring");
                break;
            }
            if (next.kind == TOK_RBRACE)
            {
                break;
            }
            if (next.kind != TOK_COMMA)
            {
                zpanic_at(next, "Expected comma in struct pattern");
                return NULL;
            }
        }

        if (lexer_next(l).kind != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected =");
            return NULL;
        }
        ASTNode *init = parse_expression(ctx, l);
        if (lexer_peek(l).kind == TOK_SEMICOLON)
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
    if (lexer_peek(l).kind == TOK_LPAREN)
    {
        lexer_next(l);
        Token val_tok = lexer_next(l);
        check_identifier(val_tok);
        char *val_name = token_strdup(val_tok);

        if (lexer_next(l).kind != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), "Expected ')' in guard pattern");
            return NULL;
        }

        if (lexer_next(l).kind != TOK_OP)
        {
            zpanic_at(lexer_peek(l), "Expected '=' after guard pattern");
            return NULL;
        }

        ASTNode *init = parse_expression(ctx, l);

        Token t = lexer_next(l);
        if (t.kind != TOK_IDENT || strncmp(t.start, "else", 4) != 0)
        {
            zpanic_at(t, "Expected 'else' in guard statement");
            return NULL;
        }

        ASTNode *else_blk;
        if (lexer_peek(l).kind == TOK_LBRACE)
        {
            else_blk = parse_block(ctx, l);
        }
        else
        {
            else_blk = ast_create(NODE_BLOCK);
            else_blk->block.statements = parse_statement(ctx, l);
        }

        if (lexer_peek(l).kind == TOK_SEMICOLON)
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

        add_symbol(ctx, val_name, "unknown", NULL, is_export);

        return n;
    }

    char *type = NULL;
    Type *type_obj = NULL; // --- NEW: Formal Type Object ---

    if (lexer_peek(l).kind == TOK_COLON)
    {
        lexer_next(l);
        // Hybrid Parse: Get Object AND String
        type_obj = parse_type_formal(ctx, l);
        if (!type_obj)
        {
            return NULL;
        }
        type = type_to_string(type_obj);
    }

    ASTNode *init = NULL;
    if (lexer_peek(l).kind == TOK_OP && is_token(lexer_peek(l), "="))
    {
        lexer_next(l);

        // Peek for special initializers
        Token next = lexer_peek(l);
        if (next.kind == TOK_IDENT && strncmp(next.start, "embed", 5) == 0)
        {
            init = parse_embed(ctx, l);

            // In fault-tolerant mode (LSP), parse_embed may return NULL
            // if the embedded file cannot be found. Create a placeholder.
            if (!init)
            {
                init = ast_create(NODE_RAW_STMT);
                init->token = next;
                init->raw_stmt.content = xstrdup("((Slice__char){0})");
                register_slice(ctx, "char");
                Type *fallback_t = type_new(TYPE_STRUCT);
                fallback_t->name = xstrdup("Slice__char");
                init->type_info = fallback_t;
            }

            if (!type && init->type_info)
            {
                type = type_to_string(init->type_info);
            }
            if (!type)
            {
                register_slice(ctx, "char");
                type = xstrdup("Slice__char");
            }
            if (lexer_peek(l).kind == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else if (next.kind == TOK_LBRACKET && type && strncmp(type, "Slice__", 7) == 0)
        {
            char *code = parse_array_literal(ctx, l, type);
            init = ast_create(NODE_RAW_STMT);
            init->token = next;
            init->raw_stmt.content = code;
            if (lexer_peek(l).kind == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else if (next.kind == TOK_LPAREN && type && strncmp(type, "Tuple__", 7) == 0)
        {
            char *code = parse_tuple_literal(ctx, l, type);
            init = ast_create(NODE_RAW_STMT);
            init->token = next;
            init->raw_stmt.content = code;
            if (lexer_peek(l).kind == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
        else
        {
            if (type_obj && type_obj->name)
            {
                if (type_obj->count > 0 && type_obj->args)
                {
                    size_t len = strlen(type_obj->name) + 1;
                    char **clean_args = xmalloc(sizeof(char *) * (size_t)type_obj->count);
                    for (int ai = 0; ai < type_obj->count; ai++)
                    {
                        const char *an = type_obj->args[ai] ? type_obj->args[ai]->name : "int";
                        clean_args[ai] = sanitize_mangled_name(an);
                        len += 2 + strlen(clean_args[ai]);
                    }
                    char *mangled = xmalloc(len);
                    strcpy(mangled, type_obj->name);
                    for (int ai = 0; ai < type_obj->count; ai++)
                    {
                        strcat(mangled, "__");
                        strcat(mangled, clean_args[ai]);
                        zfree(clean_args[ai]);
                    }
                    zfree(clean_args);
                    ctx->cg.expected_init_type = mangled;
                }
                else
                {
                    ctx->cg.expected_init_type = xstrdup(type_obj->name);
                }
            }
            init = parse_expression(ctx, l);
            zfree(ctx->cg.expected_init_type);
            ctx->cg.expected_init_type = NULL;
        }

        // Multi-let check: if this is the first of let a = 0, b = 1, handle it now
        if (lexer_peek(l).kind == TOK_COMMA)
        {
            // Save first decl info, jump to multi-let handling
            ASTNode *first = ast_create(NODE_VAR_DECL);
            first->token = name_tok;
            first->var_decl.name = name;
            first->var_decl.type_str = type;
            first->var_decl.type_info = type_obj;
            first->type_info = type_obj;
            first->var_decl.init_expr = init;
            if (name)
            {
                add_symbol_with_token(ctx, name, type, type_obj, name_tok, is_export);
            }

            // Parse remaining variables after comma
            ASTNode *prev = first;
            while (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l); // eat comma
                Token ntok = lexer_next(l);
                check_identifier(ntok);
                char *nname = token_strdup(ntok);
                char *ntype = NULL;
                Type *ntype_obj = NULL;
                if (lexer_peek(l).kind == TOK_COLON)
                {
                    lexer_next(l);
                    ntype_obj = parse_type_formal(ctx, l);
                    if (!ntype_obj)
                    {
                        return NULL;
                    }
                    ntype = type_to_string(ntype_obj);
                }
                ASTNode *ninit = NULL;
                if (lexer_peek(l).kind == TOK_OP && is_token(lexer_peek(l), "="))
                {
                    lexer_next(l);
                    ninit = parse_expression(ctx, l);
                }
                if (!ntype && ninit)
                {
                    if (ninit->type_info)
                    {
                        ntype_obj = type_new(ninit->type_info->kind);
                        if (ninit->type_info->name)
                        {
                            ntype_obj->name = xstrdup(ninit->type_info->name);
                        }
                        if (ninit->type_info->inner)
                        {
                            ntype_obj->inner = ninit->type_info->inner;
                        }
                        ntype_obj->array_size = ninit->type_info->array_size;
                        ntype = type_to_string(ntype_obj);
                    }
                    else if (ninit->kind == NODE_EXPR_LITERAL)
                    {
                        if (ninit->literal.kind == LITERAL_INT)
                        {
                            ntype = xstrdup("int");
                            ntype_obj = type_new(TYPE_INT);
                        }
                        else if (ninit->literal.kind == LITERAL_FLOAT)
                        {
                            ntype = xstrdup("float");
                            ntype_obj = type_new(TYPE_FLOAT);
                        }
                        else if (ninit->literal.kind == LITERAL_STRING)
                        {
                            ntype = xstrdup("string");
                            ntype_obj = type_new(TYPE_STRING);
                        }
                    }
                }
                if (!ntype && !ninit)
                {
                    zpanic_at(ntok, "Variable '%s' requires a type or initializer", nname);
                    return NULL;
                }
                add_symbol_with_token(ctx, nname, ntype, ntype_obj, ntok, is_export);
                ASTNode *nn = ast_create(NODE_VAR_DECL);
                nn->token = ntok;
                nn->var_decl.name = nname;
                nn->var_decl.type_str = ntype;
                nn->var_decl.type_info = ntype_obj;
                nn->type_info = ntype_obj;
                nn->var_decl.init_expr = ninit;
                prev->next = nn;
                prev = nn;
            }
            if (lexer_peek(l).kind == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
            return first;
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
                char target_struct[MAX_TYPE_NAME_LEN];
                strcpy(target_struct, type);
                target_struct[strlen(target_struct) - 1] = 0;
                char source_struct[MAX_TYPE_NAME_LEN];
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
                if (init->type_info->args && init->type_info->count > 0)
                {
                    type_obj->args = init->type_info->args;
                    type_obj->count = init->type_info->count;
                    type_obj->is_varargs = init->type_info->is_varargs;
                }
                type_obj->array_size = init->type_info->array_size;
                type_obj->is_raw = init->type_info->is_raw;
                type_obj->is_explicit_struct = init->type_info->is_explicit_struct;
                type = type_to_string(type_obj);
            }
            else if (init->kind == NODE_EXPR_SLICE)
            {
                zpanic_at(init->token, "Slice Node has NO Type Info!");
                return NULL;
            }
            // Fallbacks for literals
            else if (init->kind == NODE_EXPR_LITERAL)
            {
                if (init->literal.kind == LITERAL_INT)
                {
                    type = xstrdup("int");
                    type_obj = type_new(TYPE_INT);
                }
                else if (init->literal.kind == LITERAL_FLOAT)
                {
                    type = xstrdup("float");
                    type_obj = type_new(TYPE_FLOAT);
                }
                else if (init->literal.kind == LITERAL_STRING)
                {
                    type = xstrdup("string");
                    type_obj = type_new(TYPE_STRING);
                }
            }
            else if (init->kind == NODE_EXPR_STRUCT_INIT)
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
        return NULL;
    }

    // Register in symbol table with actual token
    add_symbol_with_token(ctx, name, type, type_obj, name_tok, is_export);

    if (init && type_obj)
    {
        Type *t = init->type_info;
        if (!t && init->kind == NODE_EXPR_VAR)
        {
            t = find_symbol_type_info(ctx, init->var_ref.name);
        }

        // Literal type construction for validation
        Type *temp_literal_type = NULL;
        if (!t && init->kind == NODE_EXPR_LITERAL)
        {
            if (init->literal.kind == LITERAL_INT)
            {
                temp_literal_type = type_new(TYPE_INT);
            }
            else if (init->literal.kind == LITERAL_FLOAT)
            {
                temp_literal_type = type_new(TYPE_FLOAT);
            }
            else if (init->literal.kind == LITERAL_STRING)
            {
                temp_literal_type = type_new(TYPE_STRING);
            }
            else if (init->literal.kind == LITERAL_CHAR)
            {
                temp_literal_type = type_new(TYPE_CHAR);
            }
            t = temp_literal_type;
        }

        // Parser-level type validation: catches obvious mismatches like assigning
        // string to int. More comprehensive type checking happens in the typechecker.
        if (t && !type_eq(type_obj, t))
        {
            // Allow conversions that are safe or handled by the typechecker:
            // numeric (int->float), pointer (char*->void*), function (closures),
            // struct/array (Slice__char vs Slice_char naming), and opaque alias compat.
            int is_numeric = (is_integer_type(type_obj) || is_float_type(type_obj)) &&
                             (is_integer_type(t) || is_float_type(t));
            int is_pointer = (type_obj->kind == TYPE_POINTER) || (t->kind == TYPE_POINTER);
            int is_function = (type_obj->kind == TYPE_FUNCTION) || (t->kind == TYPE_FUNCTION);
            int is_struct = (type_obj->kind == TYPE_STRUCT) || (t->kind == TYPE_STRUCT);
            int is_array = (type_obj->kind == TYPE_ARRAY) || (t->kind == TYPE_ARRAY);
            int is_opaque = (type_obj->kind == TYPE_ALIAS && type_obj->alias.is_opaque_alias) ||
                            (t->kind == TYPE_ALIAS && t->alias.is_opaque_alias);
            if (!is_numeric && !is_pointer && !is_function && !is_struct && !is_array &&
                (!is_opaque || !check_opaque_alias_compat(ctx, type_obj, t)))
            {
                char *expected = type_to_string(type_obj);
                char *got = type_to_string(t);
                zpanic_at(init->token, "Type validation failed. Expected '%s', but got '%s'",
                          expected, got);
                return NULL;
                zfree(expected);
                zfree(got);
            }
        }

        if (temp_literal_type)
        {
            zfree(temp_literal_type); // Simple free, shallow
        }
    }

    // NEW: Capture Const Integer Values
    if (init && init->kind == NODE_EXPR_LITERAL && init->literal.kind == LITERAL_INT)
    {
        ZenSymbol *s = find_symbol_entry(ctx, name); // Helper to find the struct
        if (s)
        {
            s->is_const_value = 1;
            s->const_int_val = (int)init->literal.int_val;
        }
    }

    if (lexer_peek(l).kind == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *n = ast_create(NODE_VAR_DECL);
    n->token = name_tok; // Save location
    n->var_decl.name = name;
    n->var_decl.type_str = type;
    n->var_decl.type_info = type_obj;
    n->type_info = type_obj;

    // Auto-construct Trait Object
    if (type && is_trait(type))
    {
        init = transform_to_trait_object(ctx, type, init);
    }

    n->var_decl.init_expr = init;

    // Move Semantics Logic for Initialization
    if (init && init->kind == NODE_EXPR_VAR)
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
    if (lexer_peek(l).kind == TOK_DEFER)
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead); // Eat defer
        if (lexer_peek(&lookahead).kind != TOK_LBRACE)
        {
            // Proceed to consume
            lexer_next(l); // Eat defer
            // Parse the defer expression/statement
            // Usually defer close(it);
            // We parse expression.
            ASTNode *expr = parse_expression(ctx, l);

            // Handle "it" substitution
            replace_it_with_var(expr, name);

            if (lexer_peek(l).kind == TOK_SEMICOLON)
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

    // Multiple declarations: let a = 0, b = 1
    if (lexer_peek(l).kind == TOK_COMMA)
    {
        ASTNode *head = NULL;
        ASTNode *prev = NULL;
        // The current n is the first declaration; include it in the chain
        head = n;
        prev = n;

        while (lexer_peek(l).kind == TOK_COMMA)
        {
            lexer_next(l); // eat comma

            // Parse next variable: name [:type] [= expr]
            Token next_name_tok = lexer_next(l);
            check_identifier(next_name_tok);
            char *next_name = token_strdup(next_name_tok);

            char *next_type = NULL;
            Type *next_type_obj = NULL;
            if (lexer_peek(l).kind == TOK_COLON)
            {
                lexer_next(l);
                next_type_obj = parse_type_formal(ctx, l);
                if (!next_type_obj)
                {
                    return NULL;
                }
                next_type = type_to_string(next_type_obj);
            }

            ASTNode *next_init = NULL;
            if (lexer_peek(l).kind == TOK_OP && is_token(lexer_peek(l), "="))
            {
                lexer_next(l);
                next_init = parse_expression(ctx, l);
            }

            if (!next_type && !next_init)
            {
                zpanic_at(next_name_tok, "Variable '%s' requires a type or initializer", next_name);
                return NULL;
            }

            // Type inference from init
            if (!next_type && next_init)
            {
                if (next_init->type_info)
                {
                    next_type_obj = type_new(next_init->type_info->kind);
                    if (next_init->type_info->name)
                    {
                        next_type_obj->name = xstrdup(next_init->type_info->name);
                    }
                    if (next_init->type_info->inner)
                    {
                        next_type_obj->inner = next_init->type_info->inner;
                    }
                    next_type_obj->array_size = next_init->type_info->array_size;
                    next_type = type_to_string(next_type_obj);
                }
                else if (next_init->kind == NODE_EXPR_LITERAL)
                {
                    if (next_init->literal.kind == LITERAL_INT)
                    {
                        next_type = xstrdup("int");
                        next_type_obj = type_new(TYPE_INT);
                    }
                    else if (next_init->literal.kind == LITERAL_FLOAT)
                    {
                        next_type = xstrdup("float");
                        next_type_obj = type_new(TYPE_FLOAT);
                    }
                    else if (next_init->literal.kind == LITERAL_STRING)
                    {
                        next_type = xstrdup("string");
                        next_type_obj = type_new(TYPE_STRING);
                    }
                }
            }

            add_symbol_with_token(ctx, next_name, next_type, next_type_obj, next_name_tok,
                                  is_export);

            ASTNode *next_node = ast_create(NODE_VAR_DECL);
            next_node->token = next_name_tok;
            next_node->var_decl.name = next_name;
            next_node->var_decl.type_str = next_type;
            next_node->var_decl.type_info = next_type_obj;
            next_node->type_info = next_type_obj;
            next_node->var_decl.init_expr = next_init;

            if (!ctx->current_scope || !ctx->current_scope->parent)
            {
                add_to_global_list(ctx, next_node);
            }

            prev->next = next_node;
            prev = next_node;
        }

        if (lexer_peek(l).kind == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        return head;
    }

    return n;
}

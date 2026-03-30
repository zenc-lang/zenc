#include "../codegen/codegen.h"
#include "../plugins/plugin_manager.h"
#include "parser.h"
#include "../ast/primitives.h"
#include <ctype.h>
#include "analysis/const_fold.h"

static int is_unmangle_primitive(const char *base);

void try_parse_macro_const(ParserContext *ctx, const char *content)
{
    Lexer l;
    lexer_init(&l, content);
    l.emit_comments = 0;

    lexer_next(&l); // Skip start

    // Manual skip of #
    const char *p = content;
    while (isspace(*p))
    {
        p++;
    }
    if (*p == '#')
    {
        p++;
    }

    // Now lex the rest
    lexer_init(&l, p);

    // Expect 'define'
    Token def = lexer_next(&l);
    if (def.type != TOK_IDENT || strncmp(def.start, "define", 6) != 0)
    {
        return;
    }

    // Expect NAME
    Token name = lexer_next(&l);
    if (name.type != TOK_IDENT)
    {
        return;
    }

    const char *after_name = name.start + name.len;
    if (*after_name == '(')
    {
        return; // Function-like macro definition
    }

    // Check remaining tokens for SAFETY
    Lexer check_l = l;
    int balance = 0;
    while (1)
    {
        Token ct = lexer_next(&check_l);
        if (ct.type == TOK_EOF)
        {
            break;
        }
        if (ct.type == TOK_LPAREN)
        {
            balance++;
        }
        else if (ct.type == TOK_RPAREN)
        {
            balance--;
        }
        else if (ct.type == TOK_LBRACE || ct.type == TOK_RBRACE || ct.type == TOK_SEMICOLON)
        {
            return; // Unsafe or complex
        }

        // Safety check for C casts/pointers that cause compiler crash in expression parser
        if (ct.type == TOK_IDENT)
        {
            char *tok_str = token_strdup(ct);
            int is_prim = is_primitive_type_name(tok_str);

            // Check other keywords not covered by is_primitive_type_name
            if (!is_prim)
            {
                if (is_token(ct, "signed") || is_token(ct, "unsigned") || is_token(ct, "struct") ||
                    is_token(ct, "union") || is_token(ct, "enum") || is_token(ct, "const") ||
                    is_token(ct, "volatile") || is_token(ct, "extern") || is_token(ct, "static") ||
                    is_token(ct, "register") || is_token(ct, "auto") || is_token(ct, "typedef"))
                {
                    is_prim = 1;
                }
            }

            free(tok_str);

            if (is_prim)
            {
                return;
            }
        }
    }
    if (balance != 0)
    {
        return; // Unbalanced
    }

    // Ensure we have something to parse
    if (lexer_peek(&l).type == TOK_EOF)
    {
        return;
    }

    // Try parse expression
    // We need to handle potential parsing errors gracefully.
    // If parse_expression errors, zpanic unwinds.
    // But we filtered hopefully unsafe tokens.

    ASTNode *expr = parse_expression(ctx, &l);
    if (!expr)
    {
        return;
    }

    long long val;
    if (eval_const_int_expr(expr, ctx, &val))
    {
        // Success! Register as constant.
        char *n = token_strdup(name);

        // Check if already defined?
        ZenSymbol *existing = find_symbol_entry(ctx, n);
        if (!existing)
        {
            // Add to symbol table
            add_symbol(ctx, n, "int", type_new(TYPE_INT)); // Placeholder type
            // find_symbol_entry to set properties
            ZenSymbol *sym = find_symbol_entry(ctx, n);
            if (sym)
            {
                sym->is_const_value = 1;
                sym->const_int_val = (int)val;
                sym->is_def = 1;
            }
        }
        else
        {
            free(n);
        }
    }
}
#include <stdlib.h>
#include <string.h>

void instantiate_methods(ParserContext *ctx, GenericImplTemplate *it,
                         const char *mangled_struct_name, const char *arg,
                         const char *unmangled_arg);

Token expect(Lexer *l, ZenTokenType type, const char *msg)
{
    Token t = lexer_next(l);
    if (t.type != type)
    {
        zpanic_at(t, "Expected %s, but got '%.*s'", msg, t.len, t.start);
        return (Token){type, t.start, 0, t.line, t.col, t.file};
    }
    return t;
}

// Helper to check if a type name is a primitive type
int is_primitive_type_name(const char *name)
{
    return find_primitive_kind(name) != TYPE_UNKNOWN;
}

TypeKind get_primitive_type_kind(const char *name)
{
    return find_primitive_kind(name);
}

// Forward declaration
char *ast_to_string(ASTNode *node);

// Temporary lightweight AST printer for default args
// Comprehensive AST printer for default args and other code generation needs
char *ast_to_string(ASTNode *node)
{
    if (!node)
    {
        return xstrdup("");
    }

    char *buf = xmalloc(4096);
    buf[0] = 0;

    switch (node->type)
    {
    case NODE_EXPR_LITERAL:
        if (node->literal.type_kind == LITERAL_INT)
        {
            sprintf(buf, "%llu", node->literal.int_val);
        }
        else if (node->literal.type_kind == LITERAL_FLOAT)
        {
            sprintf(buf, "%f", node->literal.float_val);
        }
        else if (node->literal.type_kind == LITERAL_STRING)
        {
            sprintf(buf, "\"%s\"", node->literal.string_val);
        }
        else if (node->literal.type_kind == LITERAL_CHAR)
        {
            if (node->literal.int_val == '\'')
            {
                sprintf(buf, "'\\''");
            }
            else if (node->literal.int_val == '\n')
            {
                sprintf(buf, "'\\n'");
            }
            else if (node->literal.int_val == '\\')
            {
                sprintf(buf, "'\\\\'");
            }
            else if (node->literal.int_val == '\0')
            {
                sprintf(buf, "'\\0'");
            }
            else
            {
                sprintf(buf, "'%c'", (char)node->literal.int_val);
            }
        }
        break;
    case NODE_EXPR_VAR:
        strcpy(buf, node->var_ref.name);
        break;
    case NODE_EXPR_BINARY:
    {
        char *l = ast_to_string(node->binary.left);
        char *r = ast_to_string(node->binary.right);
        // Add parens to be safe
        sprintf(buf, "(%s %s %s)", l, node->binary.op, r);
        free(l);
        free(r);
        break;
    }
    case NODE_EXPR_UNARY:
    {
        char *o = ast_to_string(node->unary.operand);
        sprintf(buf, "(%s%s)", node->unary.op, o);
        free(o);
        break;
    }
    case NODE_EXPR_CAST:
    {
        char *e = ast_to_string(node->cast.expr);
        sprintf(buf, "((%s)%s)", node->cast.target_type, e);
        free(e);
        break;
    }
    case NODE_EXPR_CALL:
    {
        char *callee = ast_to_string(node->call.callee);
        snprintf(buf, 256, "%s(", callee);
        free(callee);

        ASTNode *arg = node->call.args;
        int first = 1;
        while (arg)
        {
            if (!first)
            {
                strcat(buf, ", ");
            }
            char *a = ast_to_string(arg);
            if (strlen(buf) + strlen(a) < 4090)
            {
                strcat(buf, a);
            }
            free(a);
            first = 0;
            arg = arg->next;
        }
        strcat(buf, ")");
        break;
    }
    case NODE_EXPR_STRUCT_INIT:
    {
        char *name = node->struct_init.struct_name;
        sprintf(buf, "%s{", name);

        ASTNode *field = node->struct_init.fields;
        int first = 1;
        while (field)
        {
            if (!first)
            {
                strcat(buf, ", ");
            }

            if (field->type == NODE_VAR_DECL)
            {
                strcat(buf, field->var_decl.name);
                strcat(buf, ": ");
                char *val = ast_to_string(field->var_decl.init_expr);
                strcat(buf, val);
                free(val);
            }

            first = 0;
            field = field->next;
        }
        strcat(buf, "}");
        break;
    }
    case NODE_EXPR_MEMBER:
    {
        char *t = ast_to_string(node->member.target);
        sprintf(buf, "%s.%s", t, node->member.field);
        free(t);
        break;
    }
    case NODE_EXPR_INDEX:
    {
        char *arr = ast_to_string(node->index.array);
        char *idx = ast_to_string(node->index.index);
        sprintf(buf, "%s[%s]", arr, idx);
        free(arr);
        free(idx);
        break;
    }
    default:
        // Minimal fallback
        break;
    }
    return buf;
}

int is_token(Token t, const char *s)
{
    int len = strlen(s);
    return (t.len == len && strncmp(t.start, s, len) == 0);
}

char *token_strdup(Token t)
{
    char *s = xmalloc(t.len + 1);
    strncpy(s, t.start, t.len);
    s[t.len] = 0;
    return s;
}

void skip_comments(Lexer *l)
{
    while (lexer_peek(l).type == TOK_COMMENT)
    {
        lexer_next(l);
    }
}

// C reserved words that conflict with C when used as identifiers.

static const char *C_RESERVED_WORDS[] = {
    // C types that could be used as names
    "double", "float", "signed", "unsigned", "short", "long", "auto", "register",
    // C keywords
    "switch", "case", "default", "do", "goto", "typedef", "static", "extern", "volatile", "inline",
    "restrict", "sizeof", "const",
    // C11+ keywords
    "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn",
    "_Static_assert", "_Thread_local", NULL};

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
        if (t.type == TOK_EOF)
        {
            break;
        }
        if (t.type == TOK_LBRACE || t.type == TOK_LPAREN || t.type == TOK_LBRACKET)
        {
            d++;
        }
        if (t.type == TOK_RBRACE || t.type == TOK_RPAREN || t.type == TOK_RBRACKET)
        {
            d--;
        }

        if (d == 0 && t.type == TOK_SEMICOLON)
        {
            int len = t.start - s;
            char *r = xmalloc(len + 1);
            strncpy(r, s, len);
            r[len] = 0;
            lexer_next(l);
            return r;
        }
        lexer_next(l);
    }
    return xstrdup("");
}

void enter_scope(ParserContext *ctx)
{
    Scope *s = symbol_scope_create(ctx->current_scope, NULL);
    ctx->current_scope = s;
}

void exit_scope(ParserContext *ctx)
{
    if (!ctx->current_scope || ctx->current_scope == ctx->global_scope)
    {
        return;
    }

    // Check for unused variables (legacy logic)
    ZenSymbol *sym = ctx->current_scope->symbols;
    while (sym)
    {
        if (!sym->is_used && strcmp(sym->name, "self") != 0 && sym->name[0] != '_')
        {
            // Could emit warning here
        }
        sym = sym->next;
    }

    ctx->current_scope = ctx->current_scope->parent;
}

// Helper to register a symbol for LSP persistent queries
void register_symbol_to_lsp(ParserContext *ctx, ZenSymbol *s)
{
    if (!ctx || !s)
    {
        return;
    }

    ZenSymbol *lsp_copy = xmalloc(sizeof(ZenSymbol));
    memcpy(lsp_copy, s, sizeof(ZenSymbol));
    if (s->name)
    {
        lsp_copy->name = xstrdup(s->name);
    }
    if (s->cfg_condition)
    {
        lsp_copy->cfg_condition = xstrdup(s->cfg_condition);
    }

    lsp_copy->next = ctx->all_symbols;
    ctx->all_symbols = lsp_copy;
}

void add_symbol(ParserContext *ctx, const char *n, const char *t, Type *type_info)
{
    add_symbol_with_token(ctx, n, t, type_info, (Token){0});
}

void add_symbol_with_token(ParserContext *ctx, const char *n, const char *t, Type *type_info,
                           Token tok)
{
    if (!ctx->current_scope)
    {
        if (!ctx->global_scope)
        {
            ctx->global_scope = symbol_scope_create(NULL, "Global");
        }
        ctx->current_scope = ctx->global_scope;
    }

    if (n[0] != '_' && ctx->current_scope->parent && strcmp(n, "it") != 0 && strcmp(n, "self") != 0)
    {
        Scope *p = ctx->current_scope->parent;
        while (p)
        {
            ZenSymbol *sh = p->symbols;
            while (sh)
            {
                if (strcmp(sh->name, n) == 0 && !ctx->silent_warnings)
                {
                    warn_shadowing(tok, n);
                    break;
                }
                sh = sh->next;
            }
            if (sh)
            {
                break; // found it
            }
            p = p->parent;
        }
    }

    ZenSymbol *s = symbol_add(ctx->current_scope, n, SYM_VARIABLE);
    s->type_name = t ? xstrdup(t) : NULL;
    s->type_info = type_info;
    s->decl_token = tok;

    register_symbol_to_lsp(ctx, s);
}

Type *find_symbol_type_info(ParserContext *ctx, const char *n)
{
    ZenSymbol *sym = symbol_lookup(ctx->current_scope, n);
    return sym ? sym->type_info : NULL;
}

char *find_symbol_type(ParserContext *ctx, const char *n)
{
    ZenSymbol *sym = symbol_lookup(ctx->current_scope, n);
    return sym ? sym->type_name : NULL;
}

ZenSymbol *find_symbol_entry(ParserContext *ctx, const char *n)
{
    return symbol_lookup(ctx->current_scope, n);
}

// LSP: Search flat symbol list (works after scopes are destroyed).
ZenSymbol *find_symbol_in_all(ParserContext *ctx, const char *n)
{
    ZenSymbol *sym = ctx->all_symbols;
    while (sym)
    {
        if (strcmp(sym->name, n) == 0)
        {
            return sym;
        }
        sym = sym->next;
    }
    return NULL;
}

void init_builtins()
{
    static int init = 0;
    if (init)
    {
        return;
    }
    init = 1;
}

void register_func(ParserContext *ctx, Scope *scope, const char *name, int count, char **defaults,
                   Type **arg_types, Type *ret_type, int is_varargs, int is_async, int is_pure,
                   Token decl_token)
{
    FuncSig *f = xmalloc(sizeof(FuncSig));
    f->name = xstrdup(name);
    f->decl_token = decl_token;
    f->total_args = count;
    f->defaults = defaults;
    f->arg_types = arg_types;
    f->ret_type = ret_type;
    f->is_varargs = is_varargs;
    f->is_async = is_async;
    f->is_pure = is_pure;
    f->required = 0; // Default: can discard result
    f->next = ctx->func_registry;
    ctx->func_registry = f;

    // Unified logic
    ZenSymbol *sym = symbol_add(scope ? scope : ctx->current_scope, name, SYM_FUNCTION);
    sym->data.sig = f;
    sym->decl_token = decl_token;

    register_symbol_to_lsp(ctx, sym);

    // Create formal type for the function pointer
    Type *ft = type_new(TYPE_FUNCTION);
    ft->arg_count = count;
    ft->args = arg_types;
    ft->inner = ret_type;
    ft->is_raw = 1;          // Static functions are raw pointers, not closures
    ft->traits.has_drop = 0; // Static functions don't need drop
    sym->type_info = ft;
}

void register_func_template(ParserContext *ctx, const char *name, const char *param, ASTNode *node)
{
    GenericFuncTemplate *t = xmalloc(sizeof(GenericFuncTemplate));
    t->name = xstrdup(name);
    t->generic_param = xstrdup(param);
    t->func_node = node;
    t->next = ctx->func_templates;
    ctx->func_templates = t;
}

void register_deprecated_func(ParserContext *ctx, const char *name, const char *reason)
{
    DeprecatedFunc *d = xmalloc(sizeof(DeprecatedFunc));
    d->name = xstrdup(name);
    d->reason = reason ? xstrdup(reason) : NULL;
    d->next = ctx->deprecated_funcs;
    ctx->deprecated_funcs = d;
}

DeprecatedFunc *find_deprecated_func(ParserContext *ctx, const char *name)
{
    DeprecatedFunc *d = ctx->deprecated_funcs;
    while (d)
    {
        if (strcmp(d->name, name) == 0)
        {
            return d;
        }
        d = d->next;
    }
    return NULL;
}

GenericFuncTemplate *find_func_template(ParserContext *ctx, const char *name)
{
    GenericFuncTemplate *t = ctx->func_templates;
    while (t)
    {
        if (strcmp(t->name, name) == 0)
        {
            return t;
        }
        t = t->next;
    }
    return NULL;
}

void register_generic(ParserContext *ctx, char *name)
{
    for (int i = 0; i < ctx->known_generics_count; i++)
    {
        if (strcmp(ctx->known_generics[i], name) == 0)
        {
            return;
        }
    }
    ctx->known_generics[ctx->known_generics_count++] = xstrdup(name);
}

int is_known_generic(ParserContext *ctx, char *name)
{
    for (int i = 0; i < ctx->known_generics_count; i++)
    {
        if (strcmp(ctx->known_generics[i], name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

int is_generic_dependent_str(ParserContext *ctx, const char *type_str)
{
    if (!type_str || !ctx)
    {
        return 0;
    }
    for (int i = 0; i < ctx->known_generics_count; i++)
    {
        const char *g = ctx->known_generics[i];
        const char *p = strstr(type_str, g);
        while (p)
        {
            // Boundaries: Must not be preceded or followed by identifier chars (except Ptr or _)
            int valid = 1;
            if (p > type_str && is_ident_char(*(p - 1)) && *(p - 1) != '_')
            {
                valid = 0;
            }
            if (valid)
            {
                const char *next = p + strlen(g);
                if (*next != '\0' && is_ident_char(*next) && *next != '_')
                {
                    // Allow Ptr suffix (mangled)
                    if (strncmp(next, "Ptr", 3) != 0)
                    {
                        valid = 0;
                    }
                }
            }
            if (valid)
            {
                return 1;
            }
            p = strstr(p + 1, g);
        }
    }
    return 0;
}

void register_impl_template(ParserContext *ctx, const char *sname, const char *param, ASTNode *node)
{
    GenericImplTemplate *t = xmalloc(sizeof(GenericImplTemplate));
    t->struct_name = xstrdup(sname);
    t->generic_param = xstrdup(param);
    t->impl_node = node;
    t->next = ctx->impl_templates;
    ctx->impl_templates = t;

    // Late binding: Check if any existing instantiations match this new impl
    // template
    Instantiation *inst = ctx->instantiations;
    while (inst)
    {
        if (inst->template_name && strcmp(inst->template_name, sname) == 0)
        {
            instantiate_methods(ctx, t, inst->name, inst->concrete_arg, inst->unmangled_arg);
        }
        inst = inst->next;
    }
}

void add_to_struct_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_structs_list;
    ctx->parsed_structs_list = r;
}

void register_type_alias(ParserContext *ctx, const char *alias, const char *original,
                         Type *type_info, int is_opaque, const char *defined_in_file)
{
    TypeAlias *ta = xmalloc(sizeof(TypeAlias));
    ta->alias = xstrdup(alias);
    ta->original_type = xstrdup(original);
    ta->type_info = type_info;
    ta->is_opaque = is_opaque;
    ta->defined_in_file = defined_in_file ? xstrdup(defined_in_file) : NULL;
    ta->next = ctx->type_aliases;
    ctx->type_aliases = ta;

    // Unified logic
    ZenSymbol *sym = symbol_add(ctx->current_scope, alias, SYM_ALIAS);
    sym->data.alias.original_type = xstrdup(original);
    sym->data.alias.resolved_type = type_info;
}

const char *find_type_alias(ParserContext *ctx, const char *alias)
{
    ZenSymbol *sym = symbol_lookup_kind(ctx->current_scope, alias, SYM_ALIAS);
    if (sym)
    {
        return sym->data.alias.original_type;
    }

    TypeAlias *ta = find_type_alias_node(ctx, alias);
    return ta ? ta->original_type : NULL;
}

TypeAlias *find_type_alias_node(ParserContext *ctx, const char *alias)
{
    TypeAlias *ta = ctx->type_aliases;
    while (ta)
    {
        if (strcmp(ta->alias, alias) == 0)
        {

            return ta;
        }
        ta = ta->next;
    }
    return NULL;
}

void add_to_enum_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_enums_list;
    ctx->parsed_enums_list = r;
}

void add_to_func_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_funcs_list;
    ctx->parsed_funcs_list = r;
}

void add_to_impl_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_impls_list;
    ctx->parsed_impls_list = r;
}

void add_to_global_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_globals_list;
    ctx->parsed_globals_list = r;
}

void register_builtins(ParserContext *ctx)
{
    Type *t = type_new(TYPE_BOOL);
    t->is_const = 1;
    add_symbol(ctx, "true", "bool", t);

    t = type_new(TYPE_BOOL);
    t->is_const = 1;
    add_symbol(ctx, "false", "bool", t);

    // Register 'free'
    Type *void_t = type_new(TYPE_VOID);
    add_symbol(ctx, "free", "void", void_t);
    ctx->current_scope->symbols->kind = SYM_FUNCTION;

    // Register common libc functions to avoid warnings
    add_symbol(ctx, "strdup", "string", type_new(TYPE_STRING));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "malloc", "void*", type_new_ptr(void_t));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "realloc", "void*", type_new_ptr(void_t));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "calloc", "void*", type_new_ptr(void_t));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "puts", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "printf", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "strcmp", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "strlen", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "strcpy", "string", type_new(TYPE_STRING));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "strcat", "string", type_new(TYPE_STRING));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "memset", "void*", type_new_ptr(void_t));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "memcpy", "void*", type_new_ptr(void_t));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "exit", "void", void_t);
    ctx->current_scope->symbols->kind = SYM_FUNCTION;

    // Stdio Globals
    add_symbol(ctx, "stdin", "void*", type_new_ptr(void_t));
    add_symbol(ctx, "stdout", "void*", type_new_ptr(void_t));
    add_symbol(ctx, "stderr", "void*", type_new_ptr(void_t));

    // File I/O
    add_symbol(ctx, "fopen", "void*", type_new_ptr(void_t));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "fclose", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "fread", "usize", type_new(TYPE_USIZE));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "fwrite", "usize", type_new(TYPE_USIZE));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "fseek", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "ftell", "long", type_new(TYPE_I64));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "rewind", "void", void_t);
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "fprintf", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "vprintf", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "vfprintf", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "sprintf", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "vsnprintf", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "snprintf", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "feof", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "ferror", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "mkdir", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "rmdir", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "chdir", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "getcwd", "string", type_new(TYPE_STRING));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "system", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "getenv", "string", type_new(TYPE_STRING));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "fgets", "string", type_new(TYPE_STRING));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;
    add_symbol(ctx, "usleep", "int", type_new(TYPE_INT));
    ctx->current_scope->symbols->kind = SYM_FUNCTION;

    ASTNode *va_def = ast_create(NODE_STRUCT);
    va_def->strct.name = xstrdup("va_list");
    register_struct_def(ctx, "va_list", va_def);
    register_impl(ctx, "Copy", "va_list");
}

void register_comptime_builtins(ParserContext *ctx)
{
    Type *void_t = type_new(TYPE_VOID);
    add_symbol(ctx, "yield", "void", void_t);
    add_symbol(ctx, "code", "void", void_t); // Alias for yield
    add_symbol(ctx, "compile_warn", "void", void_t);
    add_symbol(ctx, "compile_error", "void", void_t);

    register_extern_symbol(ctx, "yield");
    register_extern_symbol(ctx, "code");
    register_extern_symbol(ctx, "compile_warn");
    register_extern_symbol(ctx, "compile_error");
}

void add_instantiated_func(ParserContext *ctx, ASTNode *fn)
{
    fn->next = ctx->instantiated_funcs;
    ctx->instantiated_funcs = fn;
}

void register_enum_variant(ParserContext *ctx, const char *ename, const char *vname, int tag)
{
    EnumVariantReg *r = xmalloc(sizeof(EnumVariantReg));
    r->enum_name = xstrdup(ename);
    r->variant_name = xstrdup(vname);
    r->tag_id = tag;
    r->next = ctx->enum_variants;
    ctx->enum_variants = r;
}

EnumVariantReg *find_enum_variant(ParserContext *ctx, const char *vname)
{
    EnumVariantReg *r = ctx->enum_variants;
    while (r)
    {
        if (strcmp(r->variant_name, vname) == 0)
        {
            return r;
        }
        r = r->next;
    }
    return NULL;
}

void register_lambda(ParserContext *ctx, ASTNode *node)
{
    LambdaRef *ref = xmalloc(sizeof(LambdaRef));
    ref->node = node;
    ref->next = ctx->global_lambdas;
    ctx->global_lambdas = ref;
}

void register_extern_symbol(ParserContext *ctx, const char *name)
{
    // Check for duplicates
    for (int i = 0; i < ctx->extern_symbol_count; i++)
    {
        if (strcmp(ctx->extern_symbols[i], name) == 0)
        {
            return;
        }
    }

    // Grow array if needed
    if (ctx->extern_symbol_count == 0)
    {
        ctx->extern_symbols = xmalloc(sizeof(char *) * 64);
    }
    else if (ctx->extern_symbol_count % 64 == 0)
    {
        ctx->extern_symbols =
            xrealloc(ctx->extern_symbols, sizeof(char *) * (ctx->extern_symbol_count + 64));
    }

    ctx->extern_symbols[ctx->extern_symbol_count++] = xstrdup(name);
}

int is_extern_symbol(ParserContext *ctx, const char *name)
{
    for (int i = 0; i < ctx->extern_symbol_count; i++)
    {
        if (strcmp(ctx->extern_symbols[i], name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Unified check: should we suppress "undefined variable" warning for this name?
int should_suppress_undef_warning(ParserContext *ctx, const char *name)
{
    if (strcmp(name, "struct") == 0 || strcmp(name, "tv") == 0)
    {
        return 1;
    }

    if (is_extern_symbol(ctx, name))
    {
        return 1;
    }

    int is_all_caps = 1;
    for (const char *p = name; *p; p++)
    {
        if (islower((unsigned char)*p))
        {
            is_all_caps = 0;
            break;
        }
    }
    if (is_all_caps && name[0] != '\0')
    {
        return 1;
    }

    if (ctx->has_external_includes)
    {
        return 1;
    }

    return 0;
}

void register_slice(ParserContext *ctx, const char *type)
{
    SliceType *c = ctx->used_slices;
    while (c)
    {
        if (strcmp(c->name, type) == 0)
        {
            return;
        }
        c = c->next;
    }
    SliceType *n = xmalloc(sizeof(SliceType));
    n->name = xstrdup(type);
    n->next = ctx->used_slices;
    ctx->used_slices = n;

    // Register Struct Def for Reflection
    char slice_name[256];
    sprintf(slice_name, "Slice_%s", type);

    ASTNode *len_f = ast_create(NODE_FIELD);
    len_f->field.name = xstrdup("len");
    len_f->field.type = xstrdup("int");
    ASTNode *cap_f = ast_create(NODE_FIELD);
    cap_f->field.name = xstrdup("cap");
    cap_f->field.type = xstrdup("int");
    ASTNode *data_f = ast_create(NODE_FIELD);
    data_f->field.name = xstrdup("data");
    char ptr_type[256];
    sprintf(ptr_type, "%s*", type);
    data_f->field.type = xstrdup(ptr_type);

    data_f->next = len_f;
    len_f->next = cap_f;

    ASTNode *def = ast_create(NODE_STRUCT);
    def->strct.name = xstrdup(slice_name);
    def->strct.fields = data_f;

    register_struct_def(ctx, slice_name, def);
}

void register_tuple(ParserContext *ctx, const char *sig)
{
    TupleType *c = ctx->used_tuples;
    while (c)
    {
        if (strcmp(c->sig, sig) == 0)
        {
            return;
        }
        c = c->next;
    }
    TupleType *n = xmalloc(sizeof(TupleType));
    n->sig = xstrdup(sig);
    n->next = ctx->used_tuples;
    ctx->used_tuples = n;

    char struct_name[1024];
    char *clean_sig = sanitize_mangled_name(sig);
    sprintf(struct_name, "Tuple_%s", clean_sig);
    free(clean_sig);

    ASTNode *s_def = ast_create(NODE_STRUCT);
    s_def->strct.name = xstrdup(struct_name);

    char *s_sig = xstrdup(sig);
    char *current = s_sig;
    char *next_sep = strstr(current, "__");
    ASTNode *head = NULL, *tail = NULL;
    int i = 0;
    while (current)
    {
        if (next_sep)
        {
            *next_sep = 0;
        }

        ASTNode *f = ast_create(NODE_FIELD);
        char fname[32];
        sprintf(fname, "v%d", i++);
        f->field.name = xstrdup(fname);
        f->field.type = xstrdup(current);

        if (!head)
        {
            head = f;
        }
        else
        {
            tail->next = f;
        }
        tail = f;

        if (next_sep)
        {
            current = next_sep + 2;
            next_sep = strstr(current, "__");
        }
        else
        {
            break;
        }
    }
    free(s_sig);
    s_def->strct.fields = head;

    register_struct_def(ctx, struct_name, s_def);
}

void register_struct_def(ParserContext *ctx, const char *name, ASTNode *node)
{
    StructDef *d = xmalloc(sizeof(StructDef));
    d->name = xstrdup(name);
    d->node = node;
    d->next = ctx->struct_defs;
    ctx->struct_defs = d;

    // Unified logic
    ZenSymbol *sym = symbol_add(ctx->global_scope, name,
                                (node && node->type == NODE_ENUM) ? SYM_ENUM : SYM_STRUCT);
    sym->data.node = node;
    if (node)
    {
        sym->decl_token = node->token;
    }
    sym->type_info = node ? node->type_info : NULL;
    register_symbol_to_lsp(ctx, sym);
}

ASTNode *find_struct_def(ParserContext *ctx, const char *name)
{
    ZenSymbol *sym = symbol_lookup_kind(ctx->current_scope, name, SYM_STRUCT);
    if (!sym)
    {
        sym = symbol_lookup_kind(ctx->current_scope, name, SYM_ENUM);
    }
    if (sym)
    {
        return sym->data.node;
    }

    extern ASTNode *global_user_structs;
    if (global_user_structs)
    {
        ASTNode *s = global_user_structs;
        while (s)
        {
            if ((s->type == NODE_STRUCT || s->type == NODE_ENUM) &&
                strcmp((s->type == NODE_STRUCT ? s->strct.name : s->enm.name), name) == 0)
            {
                if (s->type == NODE_STRUCT && s->strct.is_incomplete)
                {
                    s = s->next;
                    continue;
                }
                return s;
            }
            s = s->next;
        }
    }

    if (!ctx)
    {
        return NULL;
    }

    Instantiation *i = ctx->instantiations;
    while (i)
    {
        if (strcmp(i->name, name) == 0)
        {
            return i->struct_node;
        }
        i = i->next;
    }

    ASTNode *s = ctx->instantiated_structs;
    while (s)
    {
        if ((s->type == NODE_STRUCT || s->type == NODE_ENUM) &&
            strcmp((s->type == NODE_STRUCT ? s->strct.name : s->enm.name), name) == 0)
        {
            return s;
        }
        s = s->next;
    }

    StructRef *r = ctx->parsed_structs_list;
    while (r)
    {
        if (strcmp(r->node->strct.name, name) == 0)
        {
            return r->node;
        }
        r = r->next;
    }

    // Check manually registered definitions (e.g. Slices)
    StructDef *d = ctx->struct_defs;
    while (d)
    {
        if (strcmp(d->name, name) == 0)
        {
            return d->node;
        }
        d = d->next;
    }

    // Check enums list (for @derive(Eq) and field type lookups)
    StructRef *e = ctx->parsed_enums_list;
    while (e)
    {
        if (e->node->type == NODE_ENUM && strcmp(e->node->enm.name, name) == 0)
        {
            return e->node;
        }
        e = e->next;
    }

    return NULL;
}

ASTNode *find_concrete_struct_def(ParserContext *ctx, const char *name)
{
    Instantiation *i = ctx->instantiations;
    while (i)
    {
        if (strcmp(i->name, name) == 0 && i->struct_node && i->struct_node->type == NODE_STRUCT &&
            !i->struct_node->strct.is_template)
        {
            return i->struct_node;
        }
        i = i->next;
    }

    ASTNode *s = ctx->instantiated_structs;
    while (s)
    {
        if (s->type == NODE_STRUCT && !s->strct.is_template && strcmp(s->strct.name, name) == 0)
        {
            return s;
        }
        s = s->next;
    }

    StructRef *r = ctx->parsed_structs_list;
    while (r)
    {
        if (r->node->type == NODE_STRUCT && !r->node->strct.is_template &&
            strcmp(r->node->strct.name, name) == 0)
        {
            return r->node;
        }
        r = r->next;
    }

    StructDef *d = ctx->struct_defs;
    while (d)
    {
        if (d->node && d->node->type == NODE_STRUCT && !d->node->strct.is_template &&
            strcmp(d->name, name) == 0)
        {
            return d->node;
        }
        d = d->next;
    }

    return NULL;
}

Module *find_module(ParserContext *ctx, const char *alias)
{
    Module *m = ctx->modules;
    while (m)
    {
        if (m->alias && strcmp(m->alias, alias) == 0)
        {
            return m;
        }
        m = m->next;
    }
    return NULL;
}

void register_module(ParserContext *ctx, const char *alias, const char *path)
{
    Module *m = xmalloc(sizeof(Module));
    m->alias = alias ? xstrdup(alias) : NULL;
    m->path = xstrdup(path);
    m->base_name = extract_module_name(path);
    m->next = ctx->modules;
    ctx->modules = m;
}

void register_selective_import(ParserContext *ctx, const char *symbol, const char *alias,
                               const char *source_module)
{
    SelectiveImport *si = xmalloc(sizeof(SelectiveImport));
    si->symbol = xstrdup(symbol);
    si->alias = alias ? xstrdup(alias) : NULL;
    si->source_module = xstrdup(source_module);
    si->next = ctx->selective_imports;
    ctx->selective_imports = si;
}

SelectiveImport *find_selective_import(ParserContext *ctx, const char *name)
{
    SelectiveImport *si = ctx->selective_imports;
    while (si)
    {
        if (si->alias && strcmp(si->alias, name) == 0)
        {
            return si;
        }
        if (!si->alias && strcmp(si->symbol, name) == 0)
        {
            return si;
        }
        si = si->next;
    }
    return NULL;
}

char *extract_module_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash))
    {
        slash = backslash;
    }

    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    int len = dot ? (int)(dot - base) : (int)strlen(base);
    char *name = xmalloc(len + 1);
    strncpy(name, base, len);
    name[len] = 0;

    // Sanitize to ensure valid C identifier
    for (int i = 0; i < len; i++)
    {
        if (!isalnum(name[i]))
        {
            name[i] = '_';
        }
    }

    return name;
}

int is_ident_char(char c)
{
    return isalnum(c) || c == '_';
}

ASTNode *copy_fields(ASTNode *fields)
{
    if (!fields)
    {
        return NULL;
    }
    ASTNode *n = ast_create(NODE_FIELD);
    n->field.name = xstrdup(fields->field.name);
    n->field.type = xstrdup(fields->field.type);
    n->next = copy_fields(fields->next);
    return n;
}

char *replace_in_string(const char *src, const char *old_w, const char *new_w)
{
    if (!src || !old_w || !new_w)
    {
        return src ? xstrdup(src) : NULL;
    }

    // Check for multiple parameters (comma separated)
    if (strchr(old_w, ','))
    {
        char *running_src = xstrdup(src);

        char *p_ptr = (char *)old_w;
        char *c_ptr = (char *)new_w;

        while (*p_ptr && *c_ptr)
        {
            char *p_end = strchr(p_ptr, ',');
            int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);

            char *c_end = strchr(c_ptr, ',');
            int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

            char *curr_p = xmalloc(p_len + 1);
            strncpy(curr_p, p_ptr, p_len);
            curr_p[p_len] = 0;

            char *curr_c = xmalloc(c_len + 1);
            strncpy(curr_c, c_ptr, c_len);
            curr_c[c_len] = 0;

            char *next_src = replace_in_string(running_src, curr_p, curr_c);
            free(running_src);
            running_src = next_src;

            free(curr_p);
            free(curr_c);

            if (p_end)
            {
                p_ptr = p_end + 1;
            }
            else
            {
                break;
            }
            if (c_end)
            {
                c_ptr = c_end + 1;
            }
            else
            {
                break;
            }
        }
        return running_src;
    }

    char *result;
    int i, cnt = 0;
    int newWlen = strlen(new_w);
    int oldWlen = strlen(old_w);

    // Pass 1: Count replacements
    int in_string = 0;
    for (i = 0; src[i] != '\0'; i++)
    {
        if (src[i] == '"' && (i == 0 || src[i - 1] != '\\'))
        {
            in_string = !in_string;
        }

        if (!in_string && strstr(&src[i], old_w) == &src[i])
        {
            // Check boundaries
            int valid = 1;
            if (i > 0 && is_ident_char(src[i - 1]))
            {
                valid = 0;
            }
            if (valid && is_ident_char(src[i + oldWlen]))
            {
                valid = 0;
            }

            if (valid)
            {
                cnt++;
                i += oldWlen - 1;
            }
        }
    }

    // Allocate result buffer
    result = (char *)xmalloc(i + cnt * (newWlen - oldWlen) + 1);

    // Pass 2: Perform replacement
    int j = 0;
    in_string = 0;

    int src_idx = 0;

    while (src[src_idx] != '\0')
    {
        if (src[src_idx] == '"' && (src_idx == 0 || src[src_idx - 1] != '\\'))
        {
            in_string = !in_string;
        }

        int replaced = 0;
        if (!in_string && strstr(&src[src_idx], old_w) == &src[src_idx])
        {
            int valid = 1;
            if (src_idx > 0 && is_ident_char(src[src_idx - 1]))
            {
                valid = 0;
            }
            if (valid && is_ident_char(src[src_idx + oldWlen]))
            {
                valid = 0;
            }

            if (valid)
            {
                strcpy(&result[j], new_w);
                j += newWlen;
                src_idx += oldWlen;
                replaced = 1;
            }
        }

        if (!replaced)
        {
            result[j++] = src[src_idx++];
        }
    }
    result[j] = '\0';
    return result;
}

char *replace_type_str(const char *src, const char *param, const char *concrete,
                       const char *old_struct, const char *new_struct)
{
    if (!src)
    {
        return NULL;
    }

    // Handle multi-param match
    if (param && concrete && strchr(param, ','))
    {
        char *p_ptr = (char *)param;
        char *c_ptr = (char *)concrete;

        while (*p_ptr && *c_ptr)
        {
            char *p_end = strchr(p_ptr, ',');
            int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);

            char *c_end = strchr(c_ptr, ',');
            int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

            if ((int)strlen(src) == p_len && strncmp(src, p_ptr, p_len) == 0)
            {
                char *ret = xmalloc(c_len + 1);
                strncpy(ret, c_ptr, c_len);
                ret[c_len] = 0;
                return ret;
            }

            if (p_end)
            {
                p_ptr = p_end + 1;
            }
            else
            {
                break;
            }
            if (c_end)
            {
                c_ptr = c_end + 1;
            }
            else
            {
                break;
            }
        }
    }

    size_t len = strlen(src);
    if (len > 0 && src[len - 1] == ']')
    {
        int depth = 0;
        int bracket_idx = -1;
        for (int i = len - 1; i >= 0; i--)
        {
            if (src[i] == ']')
            {
                depth++;
            }
            else if (src[i] == '[')
            {
                depth--;
                if (depth == 0)
                {
                    bracket_idx = i;
                    break;
                }
            }
        }

        if (bracket_idx > 0)
        {
            char *base = xmalloc(bracket_idx + 1);
            strncpy(base, src, bracket_idx);
            base[bracket_idx] = 0;

            char *new_base = replace_type_str(base, param, concrete, old_struct, new_struct);

            if (new_base && strcmp(new_base, base) != 0)
            {
                char *suffix = (char *)src + bracket_idx;
                char *res = xmalloc(strlen(new_base) + strlen(suffix) + 1);
                sprintf(res, "%s%s", new_base, suffix);
                free(base);
                free(new_base);
                return res;
            }
            free(base);
            if (new_base)
            {
                free(new_base);
            }
        }
    }

    if (param && strcmp(src, param) == 0)
    {

        return xstrdup(concrete);
    }

    if (old_struct && new_struct && strcmp(src, old_struct) == 0)
    {
        return xstrdup(new_struct);
    }

    if (old_struct && new_struct && param)
    {
        int size = strlen(old_struct) + strlen(param) + 2;
        char *mangled = xmalloc(size);
        snprintf(mangled, size, "%s_%s", old_struct, param);
        if (strcmp(src, mangled) == 0)
        {
            free(mangled);
            return xstrdup(new_struct);
        }
        free(mangled);
    }

    if (param && concrete && src)
    {
        // Construct mangled suffix from param ("F,S" -> "_F_S")
        char p_suffix[1024];
        p_suffix[0] = 0;

        char *p_temp = xstrdup(param);
        char *tok = strtok(p_temp, ",");
        while (tok)
        {
            strcat(p_suffix, "_");
            strcat(p_suffix, tok);
            tok = strtok(NULL, ",");
        }
        free(p_temp);

        size_t slen = strlen(src);
        size_t plen = strlen(p_suffix);

        int match = 0;
        int found_plen = 0;
        int num_ptr_suffixes = 0;
        if (slen >= plen && strcmp(src + slen - plen, p_suffix) == 0)
        {
            match = 1;
            found_plen = plen;
        }
        else if (slen > plen)
        {
            // Try matching with Ptr suffix
            const char *p_match = strstr(src, p_suffix);
            while (p_match)
            {
                const char *after = p_match + plen;
                int is_all_ptr = 1;
                if (*after == '\0')
                {
                    is_all_ptr = 0; // Handled by exact match above
                }
                while (*after)
                {
                    if (strncmp(after, "Ptr", 3) == 0)
                    {
                        after += 3;
                    }
                    else
                    {
                        is_all_ptr = 0;
                        break;
                    }
                }
                if (is_all_ptr)
                {
                    match = 1;
                    found_plen = slen - (p_match - src);
                    num_ptr_suffixes = (slen - (p_match - src) - plen) / 3;
                    break;
                }
                p_match = strstr(p_match + 1, p_suffix);
            }
        }

        if (match)
        {
            plen = found_plen;
            // Construct replacement suffix from concrete ("int,float" -> "_int_float")
            char c_suffix[1024];
            c_suffix[0] = 0;

            char *c_temp = xstrdup(concrete);
            tok = strtok(c_temp, ",");
            while (tok)
            {
                strcat(c_suffix, "_");
                char *clean = sanitize_mangled_name(tok);
                strcat(c_suffix, clean);
                free(clean);
                tok = strtok(NULL, ",");
            }
            free(c_temp);

            // Perform replacement
            size_t ret_len = slen - plen + strlen(c_suffix) + (num_ptr_suffixes * 3) + 1;
            char *ret = xmalloc(ret_len);
            strncpy(ret, src, slen - plen);
            ret[slen - plen] = 0;

            // Avoid double underscore if base already ends with one
            if (slen > plen && src[slen - plen - 1] == '_' && c_suffix[0] == '_')
            {
                strcat(ret, c_suffix + 1);
            }
            else
            {
                strcat(ret, c_suffix);
            }

            // Restore Ptr suffixes that were stripped by plen
            for (int k = 0; k < num_ptr_suffixes; k++)
            {
                strcat(ret, "Ptr");
            }
            return ret;
        }
    }

    len = strlen(src);
    if (len > 1 && src[len - 1] == '*')
    {
        size_t base_len = len - 1;
        char *base = xmalloc(base_len + 1);
        strncpy(base, src, base_len);
        base[base_len] = 0;

        char *new_base = replace_type_str(base, param, concrete, old_struct, new_struct);
        free(base);

        if (strcmp(new_base, base) != 0)
        {
            char *ret = xmalloc(strlen(new_base) + 2);
            sprintf(ret, "%s*", new_base);
            free(new_base);
            return ret;
        }
        free(new_base);
    }

    if (strncmp(src, "Slice_", 6) == 0)
    {
        char *base = xstrdup(src + 6);
        char *new_base = replace_type_str(base, param, concrete, old_struct, new_struct);
        free(base);

        if (strcmp(new_base, base) != 0)
        {
            char *ret = xmalloc(strlen(new_base) + 7);
            sprintf(ret, "Slice_%s", new_base);
            free(new_base);
            return ret;
        }
        free(new_base);
    }

    return xstrdup(src);
}

ASTNode *copy_ast_replacing(ASTNode *n, const char *p, const char *c, const char *os,
                            const char *ns);

Type *type_from_string_helper(const char *c)
{
    if (!c)
    {
        return NULL;
    }

    // Check for pointer suffix '*'
    size_t len = strlen(c);
    if (len > 0 && c[len - 1] == '*')
    {
        size_t base_len = len - 1;
        char *base = xmalloc(base_len + 1);
        strncpy(base, c, base_len);
        base[base_len] = 0;

        Type *inner = type_from_string_helper(base);
        free(base);

        return type_new_ptr(inner);
    }

    if (strncmp(c, "struct ", 7) == 0)
    {
        Type *n = type_new(TYPE_STRUCT);
        n->name = sanitize_mangled_name(c + 7);
        n->is_explicit_struct = 1;
        return n;
    }

    if (strcmp(c, "int") == 0)
    {
        return type_new(TYPE_INT);
    }
    if (strcmp(c, "float") == 0)
    {
        return type_new(TYPE_FLOAT);
    }
    if (strcmp(c, "void") == 0)
    {
        return type_new(TYPE_VOID);
    }
    if (strcmp(c, "string") == 0)
    {
        return type_new(TYPE_STRING);
    }
    if (strcmp(c, "bool") == 0)
    {
        return type_new(TYPE_BOOL);
    }
    if (strcmp(c, "char") == 0)
    {
        return type_new(TYPE_CHAR);
    }
    if (strcmp(c, "I8") == 0 || strcmp(c, "i8") == 0)
    {
        return type_new(TYPE_I8);
    }
    if (strcmp(c, "U8") == 0 || strcmp(c, "u8") == 0)
    {
        return type_new(TYPE_U8);
    }
    if (strcmp(c, "I16") == 0 || strcmp(c, "i16") == 0)
    {
        return type_new(TYPE_I16);
    }
    if (strcmp(c, "U16") == 0 || strcmp(c, "u16") == 0)
    {
        return type_new(TYPE_U16);
    }
    if (strcmp(c, "I32") == 0 || strcmp(c, "i32") == 0 || strcmp(c, "int32_t") == 0)
    {
        return type_new(TYPE_I32);
    }
    if (strcmp(c, "U32") == 0 || strcmp(c, "u32") == 0 || strcmp(c, "uint32_t") == 0)
    {
        return type_new(TYPE_U32);
    }
    if (strcmp(c, "I64") == 0 || strcmp(c, "i64") == 0 || strcmp(c, "int64_t") == 0)
    {
        return type_new(TYPE_I64);
    }
    if (strcmp(c, "U64") == 0 || strcmp(c, "u64") == 0 || strcmp(c, "uint64_t") == 0)
    {
        return type_new(TYPE_U64);
    }
    if (strcmp(c, "float") == 0 || strcmp(c, "f32") == 0)
    {
        return type_new(TYPE_F32);
    }
    if (strcmp(c, "double") == 0 || strcmp(c, "f64") == 0)
    {
        return type_new(TYPE_F64);
    }
    if (strcmp(c, "I128") == 0 || strcmp(c, "i128") == 0)
    {
        return type_new(TYPE_I128);
    }
    if (strcmp(c, "U128") == 0 || strcmp(c, "u128") == 0)
    {
        return type_new(TYPE_U128);
    }
    if (strcmp(c, "rune") == 0)
    {
        return type_new(TYPE_RUNE);
    }
    if (strcmp(c, "uint") == 0)
    {
        return type_new(TYPE_UINT);
    }

    if (strcmp(c, "byte") == 0)
    {
        return type_new(TYPE_BYTE);
    }
    if (strcmp(c, "usize") == 0)
    {
        return type_new(TYPE_USIZE);
    }
    if (strcmp(c, "isize") == 0)
    {
        return type_new(TYPE_ISIZE);
    }

    Type *n = type_new(TYPE_STRUCT);
    n->name = sanitize_mangled_name(c);
    return n;
}

Type *replace_type_formal(Type *t, const char *p, const char *c, const char *os, const char *ns)
{
    if (!t)
    {
        return NULL;
    }

    // Exact Match Logic (with multi-param splitting)
    if ((t->kind == TYPE_STRUCT || t->kind == TYPE_GENERIC) && t->name)
    {

        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                if ((int)strlen(t->name) == p_len && strncmp(t->name, p_ptr, p_len) == 0)
                {
                    char *c_part = xmalloc(c_len + 1);
                    strncpy(c_part, c_ptr, c_len);
                    c_part[c_len] = 0;

                    Type *res = type_from_string_helper(c_part);
                    free(c_part);
                    return res;
                }
                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else if (strcmp(t->name, p) == 0)
        {
            return type_from_string_helper(c);
        }
    }

    Type *n = xmalloc(sizeof(Type));
    *n = *t;

    if (t->name)
    {
        if (os && ns && strcmp(t->name, os) == 0)
        {
            n->name = xstrdup(ns);
            n->kind = TYPE_STRUCT;
            n->arg_count = 0;
            n->args = NULL;
        }
        else if (p && c)
        {
            // Suffix Match Logic (with multi-param splitting)
            char p_suffix[1024];
            p_suffix[0] = 0;

            char *p_temp = xstrdup(p);
            char *tok = strtok(p_temp, ",");
            while (tok)
            {
                strcat(p_suffix, "_");
                strcat(p_suffix, tok);
                tok = strtok(NULL, ",");
            }
            free(p_temp);

            size_t nlen = strlen(t->name);
            size_t slen = strlen(p_suffix);

            int match = 0;
            int found_slen = 0;
            int num_ptr_suffixes = 0;
            if (nlen >= slen && strcmp(t->name + nlen - slen, p_suffix) == 0)
            {
                match = 1;
                found_slen = slen;
            }
            else if (nlen > slen)
            {
                // Try matching with Ptr suffix
                const char *p_match = strstr(t->name, p_suffix);
                while (p_match)
                {
                    const char *after = p_match + slen;
                    int is_all_ptr = 1;
                    if (*after == '\0')
                    {
                        is_all_ptr = 0; // Handled by exact match above
                    }
                    while (*after)
                    {
                        if (strncmp(after, "Ptr", 3) == 0)
                        {
                            after += 3;
                        }
                        else
                        {
                            is_all_ptr = 0;
                            break;
                        }
                    }
                    if (is_all_ptr)
                    {
                        match = 1;
                        found_slen = nlen - (p_match - t->name);
                        num_ptr_suffixes = (nlen - (p_match - t->name) - slen) / 3;
                        break;
                    }
                    p_match = strstr(p_match + 1, p_suffix);
                }
            }

            if (match)
            {
                slen = found_slen;
                char c_suffix[1024];
                c_suffix[0] = 0;
                char *c_temp = xstrdup(c);
                tok = strtok(c_temp, ",");
                while (tok)
                {
                    strcat(c_suffix, "_");
                    char *clean = sanitize_mangled_name(tok);
                    strcat(c_suffix, clean);
                    free(clean);
                    tok = strtok(NULL, ",");
                }
                free(c_temp);

                char *new_name =
                    xmalloc(nlen - slen + strlen(c_suffix) + (num_ptr_suffixes * 3) + 1);
                strncpy(new_name, t->name, nlen - slen);
                new_name[nlen - slen] = 0;

                // If the base name already ends with an underscore and our suffix starts with one,
                // don't double it up.
                if (nlen > slen && t->name[nlen - slen - 1] == '_' && c_suffix[0] == '_')
                {
                    strcat(new_name, c_suffix + 1);
                }
                else
                {
                    strcat(new_name, c_suffix);
                }

                // Restore Ptr suffixes
                for (int k = 0; k < num_ptr_suffixes; k++)
                {
                    strcat(new_name, "Ptr");
                }
                n->name = new_name;
                n->kind = TYPE_STRUCT;
                n->arg_count = 0;
                n->args = NULL;
            }
            else
            {
                n->name = xstrdup(t->name);
            }
        }
        else
        {
            n->name = xstrdup(t->name);
        }
    }

    if (t->kind == TYPE_POINTER || t->kind == TYPE_ARRAY)
    {
        n->inner = replace_type_formal(t->inner, p, c, os, ns);
    }

    if (n->arg_count > 0 && t->args)
    {
        n->args = xmalloc(sizeof(Type *) * t->arg_count);
        for (int i = 0; i < t->arg_count; i++)
        {
            n->args[i] = replace_type_formal(t->args[i], p, c, os, ns);
        }
    }

    return n;
}

// Helper to replace generic params in mangled names (e.g. Option_V_None ->
// Option_int_None)
char *replace_mangled_part(const char *src, const char *param, const char *concrete)
{
    if (!src || !param || !concrete)
    {
        return src ? xstrdup(src) : NULL;
    }

    char *result = xmalloc(4096); // Basic buffer for simplicity
    result[0] = 0;

    const char *curr = src;
    char *out = result;
    int plen = strlen(param);

    while (*curr)
    {
        // Check if param matches here
        if (strncmp(curr, param, plen) == 0)
        {
            // Check boundaries: Must be delimited by underscores to be a mangled identifier
            // (e.g., Vec_T should match, but standalone T should not)
            int valid = 1;
            int has_underscore_boundary = 0;

            // Check Prev: Start of string OR Underscore
            if (curr > src)
            {
                if (*(curr - 1) == '_')
                {
                    has_underscore_boundary = 1;
                }
                else if (is_ident_char(*(curr - 1)))
                {
                    valid = 0;
                }
            }

            // Check Next: End of string OR Underscore
            if (valid && curr[plen] != 0 && curr[plen] != '_' && is_ident_char(curr[plen]))
            {
                // Allow Ptr suffix, but not other alphanumeric characters that would make it a
                // different identifier
                if (strncmp(curr + plen, "Ptr", 3) != 0)
                {
                    valid = 0;
                }
            }
            if (valid && curr[plen] == '_')
            {
                has_underscore_boundary = 1;
            }

            // Only replace if there's at least one underscore boundary
            if (valid && !has_underscore_boundary)
            {
                valid = 0;
            }

            if (valid)
            {
                strcpy(out, concrete);
                out += strlen(concrete);
                curr += plen;
                continue;
            }
        }
        *out++ = *curr++;
    }
    *out = 0;
    return xstrdup(result);
}

ASTNode *copy_ast_replacing(ASTNode *n, const char *p, const char *c, const char *os,
                            const char *ns)
{
    if (!n)
    {
        return NULL;
    }

    ASTNode *new_node = xmalloc(sizeof(ASTNode));
    *new_node = *n;

    if (n->resolved_type)
    {
        new_node->resolved_type = replace_type_str(n->resolved_type, p, c, os, ns);
    }
    new_node->type_info = replace_type_formal(n->type_info, p, c, os, ns);

    new_node->next = copy_ast_replacing(n->next, p, c, os, ns);

    switch (n->type)
    {
    case NODE_FUNCTION:
        new_node->func.name = xstrdup(n->func.name);
        new_node->func.ret_type = replace_type_str(n->func.ret_type, p, c, os, ns);

        char *tmp_args = replace_in_string(n->func.args, p, c);
        if (os && ns)
        {
            char *tmp2 = replace_in_string(tmp_args, os, ns);
            free(tmp_args);
            tmp_args = tmp2;
        }
        if (p && c)
        {
            char *clean_c = sanitize_mangled_name(c);
            char *tmp3 = replace_mangled_part(tmp_args, p, clean_c);
            free(clean_c);
            free(tmp_args);
            tmp_args = tmp3;
        }
        new_node->func.args = tmp_args;

        new_node->func.ret_type_info = replace_type_formal(n->func.ret_type_info, p, c, os, ns);

        // Deep copy default values AST if present
        if (n->func.default_values && n->func.arg_count > 0)
        {
            new_node->func.default_values = xmalloc(sizeof(ASTNode *) * n->func.arg_count);
            // We also need to regenerate the string defaults array based on the substituted ASTs
            // This ensures potential generic params in default values (T{}) are updated (i32{})
            // in the string representation used by codegen.
            char **new_defaults_strs = xmalloc(sizeof(char *) * n->func.arg_count);

            for (int i = 0; i < n->func.arg_count; i++)
            {
                if (n->func.default_values[i])
                {
                    new_node->func.default_values[i] =
                        copy_ast_replacing(n->func.default_values[i], p, c, os, ns);
                    new_defaults_strs[i] = ast_to_string(new_node->func.default_values[i]);
                }
                else
                {
                    new_node->func.default_values[i] = NULL;
                    new_defaults_strs[i] = NULL;
                }
            }
            // Replace the old string-based defaults with our regenerated ones
            // Note: We leak the old 'tmp_args' calculated above, but that's just a single string
            // for valid args The 'defaults' array in func struct is what matters for function
            // definition. Wait, NODE_FUNCTION has char *args (legacy) AND char **defaults (array).
            // parse_and_convert_args populated both.
            // We need to update new_node->func.defaults.
            new_node->func.defaults = new_defaults_strs;
        }

        if (n->func.arg_types)
        {
            new_node->func.arg_types = xmalloc(sizeof(Type *) * n->func.arg_count);
            for (int i = 0; i < n->func.arg_count; i++)
            {
                new_node->func.arg_types[i] =
                    replace_type_formal(n->func.arg_types[i], p, c, os, ns);
            }
        }

        new_node->func.body = copy_ast_replacing(n->func.body, p, c, os, ns);
        break;
    case NODE_BLOCK:
        new_node->block.statements = copy_ast_replacing(n->block.statements, p, c, os, ns);
        break;
    case NODE_RAW_STMT:
    {
        char *s1 = replace_in_string(n->raw_stmt.content, p, c);
        if (os && ns)
        {
            char *s2 = replace_in_string(s1, os, ns);
            free(s1);
            s1 = s2;
        }

        if (p && c)
        {
            char *clean_c = sanitize_mangled_name(c);
            char *s3 = replace_mangled_part(s1, p, clean_c);
            free(clean_c);
            free(s1);
            s1 = s3;
        }

        new_node->raw_stmt.content = s1;
    }
    break;
    case NODE_VAR_DECL:
        new_node->var_decl.name = xstrdup(n->var_decl.name);
        new_node->var_decl.type_str = replace_type_str(n->var_decl.type_str, p, c, os, ns);
        new_node->var_decl.init_expr = copy_ast_replacing(n->var_decl.init_expr, p, c, os, ns);
        break;
    case NODE_RETURN:
        new_node->ret.value = copy_ast_replacing(n->ret.value, p, c, os, ns);
        break;
    case NODE_EXPR_BINARY:
        new_node->binary.left = copy_ast_replacing(n->binary.left, p, c, os, ns);
        new_node->binary.right = copy_ast_replacing(n->binary.right, p, c, os, ns);
        new_node->binary.op = xstrdup(n->binary.op);
        break;
    case NODE_EXPR_UNARY:
        new_node->unary.op = xstrdup(n->unary.op);
        new_node->unary.operand = copy_ast_replacing(n->unary.operand, p, c, os, ns);
        break;
    case NODE_EXPR_CALL:
        new_node->call.callee = copy_ast_replacing(n->call.callee, p, c, os, ns);
        new_node->call.args = copy_ast_replacing(n->call.args, p, c, os, ns);
        new_node->call.arg_names = n->call.arg_names; // Share pointer (shallow copy)
        new_node->call.arg_count = n->call.arg_count;
        break;
    case NODE_EXPR_VAR:
    {
        char *n1 = xstrdup(n->var_ref.name);
        if (p && c)
        {
            char *tmp = replace_in_string(n1, p, c);
            free(n1);
            n1 = tmp;

            char *clean_c = sanitize_mangled_name(c);
            char *n2 = replace_mangled_part(n1, p, clean_c);
            free(clean_c);
            free(n1);
            n1 = n2;
        }
        if (os && ns)
        {
            int os_len = strlen(os);
            if (strncmp(n1, os, os_len) == 0 && n1[os_len] == '_' && n1[os_len + 1] == '_')
            {
                char *suffix = n1 + os_len;
                char *n3 = xmalloc(strlen(ns) + strlen(suffix) + 1);
                sprintf(n3, "%s%s", ns, suffix);
                free(n1);
                n1 = n3;
            }
        }
        new_node->var_ref.name = n1;
    }
    break;
    case NODE_FIELD:
        new_node->field.name = xstrdup(n->field.name);
        new_node->field.type = replace_type_str(n->field.type, p, c, os, ns);
        break;
    case NODE_EXPR_LITERAL:
        if (n->literal.type_kind == LITERAL_STRING)
        {
            new_node->literal.string_val = xstrdup(n->literal.string_val);
        }
        break;
    case NODE_EXPR_MEMBER:
        new_node->member.target = copy_ast_replacing(n->member.target, p, c, os, ns);
        new_node->member.field = xstrdup(n->member.field);
        break;
    case NODE_EXPR_INDEX:
        new_node->index.array = copy_ast_replacing(n->index.array, p, c, os, ns);
        new_node->index.index = copy_ast_replacing(n->index.index, p, c, os, ns);
        break;
    case NODE_EXPR_CAST:
        new_node->cast.target_type = replace_type_str(n->cast.target_type, p, c, os, ns);
        new_node->cast.expr = copy_ast_replacing(n->cast.expr, p, c, os, ns);
        break;
    case NODE_EXPR_STRUCT_INIT:
    {
        char *new_name = replace_type_str(n->struct_init.struct_name, p, c, os, ns);

        int is_ptr = 0;
        size_t len = strlen(new_name);
        if (len > 0 && new_name[len - 1] == '*')
        {
            is_ptr = 1;
        }

        int is_primitive = is_primitive_type_name(new_name);

        if ((is_ptr || is_primitive) && !n->struct_init.fields)
        {
            new_node->type = NODE_EXPR_LITERAL;
            new_node->literal.type_kind = LITERAL_INT;
            new_node->literal.int_val = 0;
            free(new_name);
        }
        else
        {
            new_node->struct_init.struct_name = new_name;
            ASTNode *h = NULL, *t = NULL, *curr = n->struct_init.fields;
            while (curr)
            {
                ASTNode *cp = copy_ast_replacing(curr, p, c, os, ns);
                cp->next = NULL;
                if (!h)
                {
                    h = cp;
                }
                else
                {
                    t->next = cp;
                }
                t = cp;
                curr = curr->next;
            }
            new_node->struct_init.fields = h;
        }
        break;
    }
    case NODE_IF:
        new_node->if_stmt.condition = copy_ast_replacing(n->if_stmt.condition, p, c, os, ns);
        new_node->if_stmt.then_body = copy_ast_replacing(n->if_stmt.then_body, p, c, os, ns);
        new_node->if_stmt.else_body = copy_ast_replacing(n->if_stmt.else_body, p, c, os, ns);
        break;
    case NODE_WHILE:
        new_node->while_stmt.condition = copy_ast_replacing(n->while_stmt.condition, p, c, os, ns);
        new_node->while_stmt.body = copy_ast_replacing(n->while_stmt.body, p, c, os, ns);
        break;
    case NODE_FOR:
        new_node->for_stmt.init = copy_ast_replacing(n->for_stmt.init, p, c, os, ns);
        new_node->for_stmt.condition = copy_ast_replacing(n->for_stmt.condition, p, c, os, ns);
        new_node->for_stmt.step = copy_ast_replacing(n->for_stmt.step, p, c, os, ns);
        new_node->for_stmt.body = copy_ast_replacing(n->for_stmt.body, p, c, os, ns);
        break;
    case NODE_FOR_RANGE:
        new_node->for_range.start = copy_ast_replacing(n->for_range.start, p, c, os, ns);
        new_node->for_range.end = copy_ast_replacing(n->for_range.end, p, c, os, ns);
        new_node->for_range.body = copy_ast_replacing(n->for_range.body, p, c, os, ns);
        break;

    case NODE_MATCH_CASE:
        if (n->match_case.pattern)
        {
            char *s1 = replace_in_string(n->match_case.pattern, p, c);
            if (os && ns)
            {
                char *s2 = replace_in_string(s1, os, ns);
                free(s1);
                s1 = s2;
                char *colons = strstr(s1, "::");
                if (colons)
                {
                    colons[0] = '_';
                    memmove(colons + 1, colons + 2, strlen(colons + 2) + 1);
                }
            }
            new_node->match_case.pattern = s1;
        }
        new_node->match_case.body = copy_ast_replacing(n->match_case.body, p, c, os, ns);
        if (n->match_case.guard)
        {
            new_node->match_case.guard = copy_ast_replacing(n->match_case.guard, p, c, os, ns);
        }
        break;

    case NODE_IMPL:
        new_node->impl.struct_name = replace_type_str(n->impl.struct_name, p, c, os, ns);
        new_node->impl.methods = copy_ast_replacing(n->impl.methods, p, c, os, ns);
        break;
    case NODE_IMPL_TRAIT:
        new_node->impl_trait.trait_name = xstrdup(n->impl_trait.trait_name);
        new_node->impl_trait.target_type =
            replace_type_str(n->impl_trait.target_type, p, c, os, ns);
        new_node->impl_trait.methods = copy_ast_replacing(n->impl_trait.methods, p, c, os, ns);
        break;
    case NODE_TYPEOF:
    case NODE_EXPR_SIZEOF:
        if (n->size_of.target_type)
        {
            char *replaced = replace_type_str(n->size_of.target_type, p, c, os, ns);
            if (replaced && strchr(replaced, '<'))
            {
                char *mangled = sanitize_mangled_name(replaced);
                free(replaced);
                replaced = mangled;
            }
            new_node->size_of.target_type = replaced;
            if (replaced && strcmp(replaced, n->size_of.target_type) != 0)
            {
                new_node->size_of.target_type_info = NULL;
            }
            else
            {
                new_node->size_of.target_type_info = n->size_of.target_type_info;
            }
        }
        else
        {
            new_node->size_of.target_type_info = n->size_of.target_type_info;
        }
        new_node->size_of.is_type = n->size_of.is_type;
        new_node->size_of.expr = copy_ast_replacing(n->size_of.expr, p, c, os, ns);
        break;
    default:
        break;
    }
    return new_node;
}

// Helper to sanitize type names for mangling (e.g. "int*" -> "intPtr")
char *sanitize_mangled_name(const char *s)
{
    char *buf = xmalloc(strlen(s) * 4 + 1);

    // Skip "struct " prefix if present to avoid "struct_" in mangled names
    if (strncmp(s, "struct ", 7) == 0)
    {
        s += 7;
    }

    char *p = buf;
    while (*s)
    {
        if (*s == '*')
        {
            strcpy(p, "Ptr");
            p += 3;
        }
        else if (*s == '<' || *s == ',' || *s == ' ')
        {
            *p++ = '_';
        }
        else if (*s == '>' || *s == '&')
        {
            // Skip > and & (often used in references) to keep names clean
        }
        else if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
                 *s == '_')
        {
            *p++ = *s;
        }
        else
        {
            *p++ = '_';
        }
        s++;
    }
    *p = 0;
    return buf;
}

// Helper to unmangle Ptr suffix back to pointer type ("intPtr" -> "int*")
char *unmangle_ptr_suffix(const char *s)
{
    if (!s)
    {
        return NULL;
    }

    size_t len = strlen(s);
    if (len <= 3 || strcmp(s + len - 3, "Ptr") != 0)
    {
        return xstrdup(s); // No Ptr suffix, return as-is
    }

    // Extract base type (everything before "Ptr")
    char *base = xmalloc(len - 2);
    strncpy(base, s, len - 3);
    base[len - 3] = '\0';

    char *result = xmalloc(strlen(base) + 16);

    // Check if base is a primitive type
    if (is_primitive_type_name(base))
    {
        sprintf(result, "%s*", base);
    }
    else
    {
        // Don't unmangle non-primitives ending in Ptr (like Vec_intPtr)
        strcpy(result, s);
    }

    free(base);
    return result;
}

FuncSig *find_func(ParserContext *ctx, const char *name)
{
    ZenSymbol *sym = symbol_lookup_kind(ctx->current_scope, name, SYM_FUNCTION);
    if (sym)
    {
        return sym->data.sig;
    }

    FuncSig *c = ctx->func_registry;
    while (c)
    {
        if (strcmp(c->name, name) == 0)
        {
            return c;
        }
        c = c->next;
    }

    // Fallback: Check current_impl_methods (siblings in the same impl block)
    if (ctx && ctx->current_impl_methods)
    {
        ASTNode *n = ctx->current_impl_methods;
        while (n)
        {
            if (n->type == NODE_FUNCTION && strcmp(n->func.name, name) == 0)
            {
                // Found sibling method. Construct a temporary FuncSig.
                FuncSig *sig = xmalloc(sizeof(FuncSig));
                sig->name = n->func.name;
                sig->decl_token = n->token;
                sig->total_args = n->func.arg_count;
                sig->defaults = n->func.defaults;
                sig->arg_types = n->func.arg_types;
                sig->ret_type = n->func.ret_type_info;
                sig->is_varargs = n->func.is_varargs;
                sig->is_async = n->func.is_async;
                sig->required = 0;
                sig->next = NULL;
                return sig;
            }
            n = n->next;
        }
    }

    return NULL;
}

// Helper function to recursively scan AST for sizeof types AND generic calls to trigger
// instantiation
static void trigger_instantiations(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    // Process current node
    if (node->type == NODE_EXPR_SIZEOF && node->size_of.target_type)
    {
        const char *type_str = node->size_of.target_type;
        if (strchr(type_str, '_'))
        {
            // Remove trailing '*' or 'Ptr' if present
            char *type_copy = xstrdup(type_str);
            char *star = strchr(type_copy, '*');
            if (star)
            {
                *star = '\0';
            }
            else
            {
                // Check for "Ptr" suffix and remove it
                size_t len = strlen(type_copy);
                if (len > 3 && strcmp(type_copy + len - 3, "Ptr") == 0)
                {
                    type_copy[len - 3] = '\0';
                }
            }

            char *underscore = strrchr(type_copy, '_');
            if (underscore && underscore > type_copy)
            {
                *underscore = '\0';
                char *template_name = type_copy;
                char *concrete_arg = underscore + 1;

                // Check if this is a known generic template
                GenericTemplate *gt = ctx->templates;
                int found = 0;
                while (gt)
                {
                    if (strcmp(gt->name, template_name) == 0)
                    {
                        found = 1;
                        break;
                    }
                    gt = gt->next;
                }

                if (found)
                {
                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    Token dummy_tok = {0};
                    instantiate_generic(ctx, template_name, concrete_arg, unmangled, dummy_tok);
                    free(unmangled);
                }
            }
            free(type_copy);
        }
    }
    else if (node->type == NODE_EXPR_VAR)
    {
        const char *name = node->var_ref.name;
        if (strchr(name, '_'))
        {
            GenericFuncTemplate *t = ctx->func_templates;
            while (t)
            {
                size_t tlen = strlen(t->name);
                if (strncmp(name, t->name, tlen) == 0 && name[tlen] == '_')
                {
                    char *template_name = t->name;
                    char *concrete_arg = (char *)name + tlen + 1; // cast to avoid warning

                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    instantiate_function_template(ctx, template_name, concrete_arg, unmangled);
                    free(unmangled);
                    break; // Found match, stop searching
                }
                t = t->next;
            }
        }
    }

    switch (node->type)
    {
    case NODE_FUNCTION:
        trigger_instantiations(ctx, node->func.body);
        break;
    case NODE_BLOCK:
        trigger_instantiations(ctx, node->block.statements);
        break;
    case NODE_VAR_DECL:
        trigger_instantiations(ctx, node->var_decl.init_expr);
        break;
    case NODE_RETURN:
        trigger_instantiations(ctx, node->ret.value);
        break;
    case NODE_EXPR_BINARY:
        trigger_instantiations(ctx, node->binary.left);
        trigger_instantiations(ctx, node->binary.right);
        break;
    case NODE_EXPR_UNARY:
        trigger_instantiations(ctx, node->unary.operand);
        break;
    case NODE_EXPR_CALL:
        trigger_instantiations(ctx, node->call.callee);
        trigger_instantiations(ctx, node->call.args);
        break;
    case NODE_EXPR_MEMBER:
        trigger_instantiations(ctx, node->member.target);
        break;
    case NODE_EXPR_INDEX:
        trigger_instantiations(ctx, node->index.array);
        trigger_instantiations(ctx, node->index.index);
        break;
    case NODE_EXPR_CAST:
        trigger_instantiations(ctx, node->cast.expr);
        break;
    case NODE_IF:
        trigger_instantiations(ctx, node->if_stmt.condition);
        trigger_instantiations(ctx, node->if_stmt.then_body);
        trigger_instantiations(ctx, node->if_stmt.else_body);
        break;
    case NODE_WHILE:
        trigger_instantiations(ctx, node->while_stmt.condition);
        trigger_instantiations(ctx, node->while_stmt.body);
        break;
    case NODE_FOR:
        trigger_instantiations(ctx, node->for_stmt.init);
        trigger_instantiations(ctx, node->for_stmt.condition);
        trigger_instantiations(ctx, node->for_stmt.step);
        trigger_instantiations(ctx, node->for_stmt.body);
        break;
    case NODE_FOR_RANGE:
        trigger_instantiations(ctx, node->for_range.start);
        trigger_instantiations(ctx, node->for_range.end);
        trigger_instantiations(ctx, node->for_range.body);
        break;
    case NODE_EXPR_STRUCT_INIT:
        trigger_instantiations(ctx, node->struct_init.fields);
        break;
    case NODE_MATCH:
        trigger_instantiations(ctx, node->match_stmt.expr);
        trigger_instantiations(ctx, node->match_stmt.cases);
        break;
    case NODE_MATCH_CASE:
        trigger_instantiations(ctx, node->match_case.guard);
        trigger_instantiations(ctx, node->match_case.body);
        break;
    default:
        break;
    }

    // Visit next sibling
    trigger_instantiations(ctx, node->next);
}

char *instantiate_function_template(ParserContext *ctx, const char *name, const char *concrete_type,
                                    const char *unmangled_type)
{
    GenericFuncTemplate *tpl = find_func_template(ctx, name);
    if (!tpl)
    {
        return NULL;
    }

    char *clean_type = sanitize_mangled_name(concrete_type);

    int is_still_generic = 0;
    if (strlen(clean_type) == 1 && isupper(clean_type[0]))
    {
        is_still_generic = 1;
    }

    if (is_known_generic(ctx, clean_type))
    {
        is_still_generic = 1;
    }

    char *mangled = xmalloc(strlen(name) + strlen(clean_type) + 2);
    sprintf(mangled, "%s_%s", name, clean_type);
    free(clean_type);

    if (is_still_generic)
    {
        return mangled;
    }

    if (find_func(ctx, mangled))
    {
        return mangled;
    }

    const char *subst_arg = unmangled_type ? unmangled_type : concrete_type;

    // Scan the original return type for generic struct patterns like "Triple_X_Y_Z"
    // and instantiate them with the concrete types
    if (tpl->func_node && tpl->func_node->func.ret_type)
    {
        const char *ret = tpl->func_node->func.ret_type;

        // Build the param suffix (e.g., for "X,Y,Z" -> "_X_Y_Z")
        char param_suffix[256];
        param_suffix[0] = 0;
        char *tmp = xstrdup(tpl->generic_param);
        char *tokp = strtok(tmp, ",");
        while (tokp)
        {
            strcat(param_suffix, "_");
            strcat(param_suffix, tokp);
            tokp = strtok(NULL, ",");
        }
        free(tmp);

        // Check if ret_type ends with param_suffix (e.g., "Triple_X_Y_Z" ends with "_X_Y_Z")
        size_t ret_len = strlen(ret);
        size_t suffix_len = strlen(param_suffix);
        if (ret_len > suffix_len && strcmp(ret + ret_len - suffix_len, param_suffix) == 0)
        {
            // Extract base struct name (e.g., "Triple" from "Triple_X_Y_Z")
            size_t base_len = ret_len - suffix_len;
            char *struct_base = xmalloc(base_len + 1);
            strncpy(struct_base, ret, base_len);
            struct_base[base_len] = 0;

            // Check if it's a known generic template
            GenericTemplate *gt = ctx->templates;
            while (gt && strcmp(gt->name, struct_base) != 0)
            {
                gt = gt->next;
            }
            if (gt)
            {
                // Parse the concrete types from unmangled_type or concrete_type
                const char *types_src = unmangled_type ? unmangled_type : concrete_type;

                // Count params in template
                int template_param_count = 1;
                for (const char *p = tpl->generic_param; *p; p++)
                {
                    if (*p == ',')
                    {
                        template_param_count++;
                    }
                }

                // Split concrete types
                char **args = xmalloc(sizeof(char *) * template_param_count);
                int arg_count = 0;
                char *types_copy = xstrdup(types_src);
                char *tok = strtok(types_copy, ",");
                while (tok && arg_count < template_param_count)
                {
                    args[arg_count++] = xstrdup(tok);
                    tok = strtok(NULL, ",");
                }
                free(types_copy);

                // Now instantiate the struct with these args
                Token dummy_tok = {0};
                if (arg_count == 1)
                {
                    // Unmangle Ptr suffix if needed (e.g., intPtr -> int*)
                    char *unmangled = xstrdup(args[0]);
                    size_t alen = strlen(args[0]);
                    if (alen > 3 && strcmp(args[0] + alen - 3, "Ptr") == 0)
                    {
                        char *base = xstrdup(args[0]);
                        base[alen - 3] = '\0';
                        free(unmangled);
                        unmangled = xmalloc(strlen(base) + 16);
                        if (is_unmangle_primitive(base))
                        {
                            sprintf(unmangled, "%s*", base);
                        }
                        else
                        {
                            sprintf(unmangled, "struct %s*", base);
                        }
                        free(base);
                    }
                    instantiate_generic(ctx, struct_base, args[0], unmangled, dummy_tok);
                    free(unmangled);
                }
                else if (arg_count > 1)
                {
                    instantiate_generic_multi(ctx, struct_base, args, arg_count, dummy_tok);
                }

                // Cleanup
                for (int i = 0; i < arg_count; i++)
                {
                    free(args[i]);
                }
                free(args);
            }
            free(struct_base);
        }
    }

    ASTNode *new_fn = copy_ast_replacing(tpl->func_node, tpl->generic_param, subst_arg, NULL, NULL);
    if (!new_fn || new_fn->type != NODE_FUNCTION)
    {
        return NULL;
    }

    trigger_instantiations(ctx, new_fn->func.body);

    if (new_fn->func.arg_types)
    {
        for (int i = 0; i < new_fn->func.arg_count; i++)
        {
            Type *at = new_fn->func.arg_types[i];
            if (at && at->kind == TYPE_ARRAY && at->array_size == 0 && at->inner)
            {
                char *inner_str = type_to_string(at->inner);
                register_slice(ctx, inner_str);
                free(inner_str);
            }
        }
    }

    free(new_fn->func.name);
    new_fn->func.name = xstrdup(mangled);
    new_fn->func.generic_params = NULL;

    register_func(ctx, ctx->global_scope, mangled, new_fn->func.arg_count, new_fn->func.defaults,
                  new_fn->func.arg_types, new_fn->func.ret_type_info, new_fn->func.is_varargs, 0,
                  new_fn->func.pure, new_fn->token);

    add_instantiated_func(ctx, new_fn);
    return mangled;
}

char *process_fstring(ParserContext *ctx, const char *content, char ***used_syms, int *count)
{
    (void)used_syms;
    (void)count;
    char *gen = xmalloc(8192); // Increased buffer size

    strcpy(gen, "({ static char _b[4096]; _b[0]=0; char _t[1024]; ");

    char *s = xstrdup(content);
    char *cur = s;

    while (*cur)
    {
        char *brace = cur;
        while (*brace && *brace != '{')
        {
            brace++;
        }

        if (brace > cur)
        {
            char tmp = *brace;
            *brace = 0;
            strcat(gen, "strcat(_b, \"");
            strcat(gen, cur);
            strcat(gen, "\"); ");
            *brace = tmp;
        }

        if (*brace == 0)
        {
            break;
        }

        char *p = brace + 1;
        char *colon = NULL;
        int depth = 1;

        while (*p && depth > 0)
        {
            if (*p == '{')
            {
                depth++;
            }
            if (*p == '}')
            {
                depth--;
            }
            if (depth == 1 && *p == ':' && !colon)
            {
                colon = p;
            }
            if (depth == 0)
            {
                break;
            }
            p++;
        }

        *p = 0;
        char *expr_str = brace + 1;
        char *fmt = NULL;
        if (colon)
        {
            *colon = 0;
            fmt = colon + 1;
        }

        // Parse expression fully to handle default arguments etc.
        Lexer expr_lex;
        lexer_init(&expr_lex, expr_str);
        ASTNode *expr_node = parse_expression(ctx, &expr_lex);

        // Codegen expression to temporary buffer
        char *code_buffer = NULL;
        size_t code_len = 0;
        FILE *mem_stream = tmpfile();
        if (mem_stream)
        {
            codegen_expression(ctx, expr_node, mem_stream);
            code_len = ftell(mem_stream);
            code_buffer = xmalloc(code_len + 1);
            fseek(mem_stream, 0, SEEK_SET);
            fread(code_buffer, 1, code_len, mem_stream);
            code_buffer[code_len] = 0;
            fclose(mem_stream);
        }

        if (fmt)
        {
            strcat(gen, "sprintf(_t, \"%");
            strcat(gen, fmt);
            strcat(gen, "\", ");
            if (code_buffer)
            {
                strcat(gen, code_buffer);
            }
            else
            {
                strcat(gen, expr_str); // Fallback
            }
            strcat(gen, "); strcat(_b, _t); ");
        }
        else
        {
            strcat(gen, "sprintf(_t, _z_str(");
            if (code_buffer)
            {
                strcat(gen, code_buffer);
            }
            else
            {
                strcat(gen, expr_str);
            }
            strcat(gen, "), ");
            if (code_buffer)
            {
                strcat(gen, code_buffer);
            }
            else
            {
                strcat(gen, expr_str);
            }
            strcat(gen, "); strcat(_b, _t); ");
        }

        if (code_buffer)
        {
            free(code_buffer);
        }

        cur = p + 1;
    }

    strcat(gen, "_b; })");
    free(s);
    return gen;
}

void register_impl(ParserContext *ctx, const char *trait, const char *strct)
{
    ImplReg *r = xmalloc(sizeof(ImplReg));
    r->trait = xstrdup(trait);
    r->strct = xstrdup(strct);
    r->next = ctx->registered_impls;
    ctx->registered_impls = r;
}

int check_impl(ParserContext *ctx, const char *trait, const char *strct)
{
    ImplReg *r = ctx->registered_impls;
    while (r)
    {
        if (strcmp(r->trait, trait) == 0 && strcmp(r->strct, strct) == 0)
        {
            return 1;
        }
        r = r->next;
    }

    r = ctx->registered_impls;
    while (r)
    {
        char *base_reg = xstrdup(r->strct);
        char *ptr2 = strchr(base_reg, '<');
        if (ptr2)
        {
            *ptr2 = 0;
            size_t blen = strlen(base_reg);
            if (strncmp(strct, base_reg, blen) == 0 && strct[blen] == '_')
            {
                if (strcmp(r->trait, trait) == 0)
                {
                    free(base_reg);
                    return 1;
                }
            }
        }
        free(base_reg);
        r = r->next;
    }

    return 0;
}

static int is_unmangle_primitive(const char *base)
{
    return (strcmp(base, "int") == 0 || strcmp(base, "uint") == 0 || strcmp(base, "char") == 0 ||
            strcmp(base, "bool") == 0 || strcmp(base, "void") == 0 || strcmp(base, "byte") == 0 ||
            strcmp(base, "rune") == 0 || strcmp(base, "float") == 0 ||
            strcmp(base, "double") == 0 || strcmp(base, "f32") == 0 || strcmp(base, "f64") == 0 ||
            strcmp(base, "size_t") == 0 || strcmp(base, "usize") == 0 ||
            strcmp(base, "isize") == 0 || strcmp(base, "ptrdiff_t") == 0 ||
            strncmp(base, "i8", 2) == 0 || strncmp(base, "u8", 2) == 0 ||
            strncmp(base, "int8", 4) == 0 || strncmp(base, "int16", 5) == 0 ||
            strncmp(base, "int32", 5) == 0 || strncmp(base, "int64", 5) == 0 ||
            strncmp(base, "uint8", 5) == 0 || strncmp(base, "uint16", 6) == 0 ||
            strncmp(base, "uint32", 6) == 0 || strncmp(base, "uint64", 6) == 0);
}

void register_template(ParserContext *ctx, const char *name, ASTNode *node)
{
    GenericTemplate *t = xmalloc(sizeof(GenericTemplate));
    t->name = xstrdup(name);
    t->struct_node = node;
    t->next = ctx->templates;
    ctx->templates = t;
}

ASTNode *copy_fields_replacing(ParserContext *ctx, ASTNode *fields, const char *param,
                               const char *concrete)
{
    if (!fields)
    {
        return NULL;
    }
    ASTNode *n = ast_create(NODE_FIELD);
    n->field.name = xstrdup(fields->field.name);

    // Replace strings
    n->field.type = replace_type_str(fields->field.type, param, concrete, NULL, NULL);

    // Replace formal types (Deep Copy)
    n->type_info = replace_type_formal(fields->type_info, param, concrete, NULL, NULL);

    if (n->field.type && strchr(n->field.type, '_'))
    {
        // Parse potential generic: e.g. "MapEntry_int" -> instantiate("MapEntry",
        // "int")
        char *underscore = strrchr(n->field.type, '_');
        if (underscore && underscore > n->field.type)
        {
            // Remove trailing '*' if present
            char *type_copy = xstrdup(n->field.type);
            char *star = strchr(type_copy, '*');
            if (star)
            {
                *star = '\0';
            }

            underscore = strrchr(type_copy, '_');
            if (underscore)
            {
                *underscore = '\0';
                char *template_name = type_copy;
                char *concrete_arg = underscore + 1;

                // Check if this is actually a known generic template
                GenericTemplate *gt = ctx->templates;
                int found = 0;
                while (gt)
                {
                    if (strcmp(gt->name, template_name) == 0)
                    {
                        found = 1;
                        break;
                    }
                    gt = gt->next;
                }

                if (found)
                {
                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    instantiate_generic(ctx, template_name, concrete_arg, unmangled, fields->token);
                    free(unmangled);
                }
            }
            free(type_copy);
        }
    }

    // Additional check: if type_info is a pointer to a struct with a mangled name,
    // instantiate that struct as well (fixes cases like RcInner<T>* where the
    // string check above might not catch it)
    if (n->type_info && n->type_info->kind == TYPE_POINTER && n->type_info->inner)
    {
        Type *inner = n->type_info->inner;
        if (inner->kind == TYPE_STRUCT && inner->name && strchr(inner->name, '_'))
        {
            // Extract template name by checking against known templates
            // We can't use strrchr because types like "Inner_int32_t" have multiple underscores
            char *template_name = NULL;
            char *concrete_arg = NULL;

            // Try each known template to see if the type name starts with it
            GenericTemplate *gt = ctx->templates;
            while (gt)
            {
                size_t tlen = strlen(gt->name);
                // Check if name starts with template name followed by underscore
                if (strncmp(inner->name, gt->name, tlen) == 0 && inner->name[tlen] == '_')
                {
                    template_name = gt->name;
                    concrete_arg = inner->name + tlen + 1; // Skip template name and underscore
                    break;
                }
                gt = gt->next;
            }

            if (template_name && concrete_arg)
            {
                char *unmangled = unmangle_ptr_suffix(concrete_arg);
                instantiate_generic(ctx, template_name, concrete_arg, unmangled, fields->token);
                free(unmangled);
            }
        }
    }

    n->next = copy_fields_replacing(ctx, fields->next, param, concrete);
    return n;
}

void instantiate_methods(ParserContext *ctx, GenericImplTemplate *it,
                         const char *mangled_struct_name, const char *arg,
                         const char *unmangled_arg)
{
    if (check_impl(ctx, "Methods", mangled_struct_name))
    {
        return; // Simple dedupe check
    }

    ASTNode *backup_next = it->impl_node->next;
    it->impl_node->next = NULL; // Break link to isolate node

    // Use unmangled_arg if provided, otherwise arg
    char *raw = (char *)(unmangled_arg ? unmangled_arg : arg);
    char *subst_arg = unmangle_ptr_suffix(raw);

    ASTNode *new_impl = copy_ast_replacing(it->impl_node, it->generic_param, subst_arg,
                                           it->struct_name, mangled_struct_name);
    free(subst_arg);
    it->impl_node->next = backup_next; // Restore

    ASTNode *meth = NULL;

    if (new_impl->type == NODE_IMPL)
    {
        new_impl->impl.struct_name = xstrdup(mangled_struct_name);
        meth = new_impl->impl.methods;
    }
    else if (new_impl->type == NODE_IMPL_TRAIT)
    {
        new_impl->impl_trait.target_type = xstrdup(mangled_struct_name);
        meth = new_impl->impl_trait.methods;
    }

    while (meth)
    {
        char *suffix = meth->func.name + strlen(it->struct_name);
        if (suffix && *suffix)
        {
            char *new_name = xmalloc(strlen(mangled_struct_name) + strlen(suffix) + 1);
            sprintf(new_name, "%s%s", mangled_struct_name, suffix);
            free(meth->func.name);
            meth->func.name = new_name;
            register_func(ctx, ctx->global_scope, new_name, meth->func.arg_count,
                          meth->func.defaults, meth->func.arg_types, meth->func.ret_type_info,
                          meth->func.is_varargs, 0, meth->func.pure, meth->token);
        }

        // Handle generic return types in methods (e.g., Option<T> -> Option_int)
        if (meth->func.ret_type &&
            (strchr(meth->func.ret_type, '_') || strchr(meth->func.ret_type, '<')))
        {
            GenericTemplate *gt = ctx->templates;

            while (gt)
            {
                size_t tlen = strlen(gt->name);
                char delim = meth->func.ret_type[tlen];
                if (strncmp(meth->func.ret_type, gt->name, tlen) == 0 &&
                    (delim == '_' || delim == '<'))
                {
                    // Found matching template prefix
                    const char *type_arg = meth->func.ret_type + tlen + 1;

                    // Simple approach: instantiate 'Template' with 'Arg'.
                    // If delimited by <, we need to extract the inside.
                    char *clean_arg = xstrdup(type_arg);
                    if (delim == '<')
                    {
                        char *closer = strrchr(clean_arg, '>');
                        if (closer)
                        {
                            *closer = 0;
                        }
                    }

                    // Unmangle Ptr suffix if present (e.g., intPtr -> int*)
                    char *inner_unmangled_arg = xstrdup(clean_arg);
                    size_t alen = strlen(clean_arg);
                    if (alen > 3 && strcmp(clean_arg + alen - 3, "Ptr") == 0)
                    {
                        char *base = xstrdup(clean_arg);
                        base[alen - 3] = '\0';
                        free(inner_unmangled_arg);
                        inner_unmangled_arg = xmalloc(strlen(base) + 16);
                        // Check if base is a primitive type
                        if (is_unmangle_primitive(base))
                        {
                            sprintf(inner_unmangled_arg, "%s*", base);
                        }
                        else
                        {
                            sprintf(inner_unmangled_arg, "struct %s*", base);
                        }
                        free(base);
                    }

                    instantiate_generic(ctx, gt->name, clean_arg, inner_unmangled_arg, meth->token);
                    free(unmangled_arg);
                    free(clean_arg);
                }
                gt = gt->next;
            }
        }

        trigger_instantiations(ctx, meth->func.body);

        meth = meth->next;
    }
    add_instantiated_func(ctx, new_impl);
}

void instantiate_generic(ParserContext *ctx, const char *tpl, const char *arg,
                         const char *unmangled_arg, Token token)
{

    // Ignore generic placeholders
    if (strlen(arg) == 1 && isupper(arg[0]))
    {
        return;
    }
    if (strcmp(arg, "T") == 0)
    {
        return;
    }

    char *clean_arg = sanitize_mangled_name(arg);
    char m[256];
    sprintf(m, "%s_%s", tpl, clean_arg);
    free(clean_arg);

    Instantiation *c = ctx->instantiations;
    while (c)
    {
        if (strcmp(c->name, m) == 0)
        {
            return; // Already instantiated, DO NOTHING.
        }
        c = c->next;
    }

    GenericTemplate *t = ctx->templates;
    while (t)
    {
        if (strcmp(t->name, tpl) == 0)
        {
            break;
        }
        t = t->next;
    }
    if (!t)
    {
        zpanic_at(token, "Unknown generic: %s", tpl);
    }

    Instantiation *ni = xmalloc(sizeof(Instantiation));
    ni->name = xstrdup(m);
    ni->template_name = xstrdup(tpl);
    ni->concrete_arg = xstrdup(arg);
    ni->unmangled_arg = unmangled_arg ? xstrdup(unmangled_arg)
                                      : xstrdup(arg); // Fallback to arg if unmangled is generic
    ni->struct_node = NULL;                           // Placeholder to break cycles
    ni->next = ctx->instantiations;

    ctx->instantiations = ni;

    ASTNode *struct_node_copy = NULL;

    if (t->struct_node->type == NODE_STRUCT)
    {
        ASTNode *i = ast_create(NODE_STRUCT);
        i->strct.name = xstrdup(m);
        i->strct.is_template = 0;

        // Copy type attributes (e.g. has_drop)
        i->type_info = type_new(TYPE_STRUCT);
        i->type_info->name = xstrdup(m);
        if (t->struct_node->type_info)
        {
            i->type_info->traits = t->struct_node->type_info->traits;
            i->type_info->is_restrict = t->struct_node->type_info->is_restrict;
        }
        i->strct.is_packed = t->struct_node->strct.is_packed;
        i->strct.is_union = t->struct_node->strct.is_union;
        i->strct.align = t->struct_node->strct.align;
        if (t->struct_node->strct.parent)
        {
            i->strct.parent = xstrdup(t->struct_node->strct.parent);
        }
        const char *gp = (t->struct_node->strct.generic_param_count > 0)
                             ? t->struct_node->strct.generic_params[0]
                             : "T";
        const char *subst_arg = unmangled_arg ? unmangled_arg : arg;
        i->strct.fields = copy_fields_replacing(ctx, t->struct_node->strct.fields, gp, subst_arg);
        struct_node_copy = i;
        register_struct_def(ctx, m, i);

        // Register slice types used in the instantiated struct's fields
        ASTNode *fld = i->strct.fields;
        while (fld)
        {
            if (fld->type == NODE_FIELD && fld->field.type &&
                strncmp(fld->field.type, "Slice_", 6) == 0)
            {
                register_slice(ctx, fld->field.type + 6);
            }
            fld = fld->next;
        }
    }
    else if (t->struct_node->type == NODE_ENUM)
    {
        ASTNode *i = ast_create(NODE_ENUM);
        i->enm.name = xstrdup(m);
        i->enm.is_template = 0;

        // Copy type attributes (e.g. has_drop)
        i->type_info = type_new(TYPE_ENUM);
        i->type_info->name = xstrdup(m);
        if (t->struct_node->type_info)
        {
            i->type_info->traits = t->struct_node->type_info->traits;
        }

        ASTNode *h = 0, *tl = 0;
        ASTNode *v = t->struct_node->enm.variants;
        while (v)
        {
            ASTNode *nv = ast_create(NODE_ENUM_VARIANT);
            nv->variant.name = xstrdup(v->variant.name);
            nv->variant.tag_id = v->variant.tag_id;
            const char *subst_arg = unmangled_arg ? unmangled_arg : arg;
            nv->variant.payload = replace_type_formal(
                v->variant.payload, t->struct_node->enm.generic_param, subst_arg, NULL, NULL);
            size_t mangled_var_sz = strlen(m) + strlen(nv->variant.name) + 3;
            char *mangled_var = xmalloc(mangled_var_sz);
            snprintf(mangled_var, mangled_var_sz, "%s__%s", m, nv->variant.name);
            register_enum_variant(ctx, m, mangled_var, nv->variant.tag_id);

            // Register Constructor Function Signature for the instantiated variant
            if (nv->variant.payload)
            {
                Type **at = xmalloc(sizeof(Type *));
                at[0] = nv->variant.payload;
                Type *ret_t = type_new(TYPE_ENUM);
                ret_t->name = xstrdup(m);

                register_func(ctx, ctx->global_scope, mangled_var, 1, NULL, at, ret_t, 0, 0, 0,
                              token);
            }
            else
            {
                // For variants without payload, we still need to register it as a zero-arg function
                // so that MyOption::None() works and is consistent.
                Type *ret_t = type_new(TYPE_ENUM);
                ret_t->name = xstrdup(m);
                register_func(ctx, ctx->global_scope, mangled_var, 0, NULL, NULL, ret_t, 0, 0, 0,
                              token);
            }

            free(mangled_var);
            if (!h)
            {
                h = nv;
            }
            else
            {
                tl->next = nv;
            }
            tl = nv;
            v = v->next;
        }
        i->enm.variants = h;
        struct_node_copy = i;
    }

    ni->struct_node = struct_node_copy;

    if (struct_node_copy)
    {
        struct_node_copy->next = ctx->instantiated_structs;
        ctx->instantiated_structs = struct_node_copy;
    }

    GenericImplTemplate *it = ctx->impl_templates;
    while (it)
    {
        if (strcmp(it->struct_name, tpl) == 0)
        {
            const char *subst_arg = unmangled_arg ? unmangled_arg : arg;
            instantiate_methods(ctx, it, m, arg, subst_arg);
        }
        it = it->next;
    }
}

static void free_field_list(ASTNode *fields)
{
    while (fields)
    {
        ASTNode *next = fields->next;
        if (fields->field.name)
        {
            free(fields->field.name);
        }
        if (fields->field.type)
        {
            free(fields->field.type);
        }
        free(fields);
        fields = next;
    }
}

void instantiate_generic_multi(ParserContext *ctx, const char *tpl, char **args, int arg_count,
                               Token token)
{
    // Build mangled name from all args
    char m[256];
    strcpy(m, tpl);
    for (int i = 0; i < arg_count; i++)
    {
        char *clean = sanitize_mangled_name(args[i]);
        strcat(m, "_");
        strcat(m, clean);
        free(clean);
    }
    fprintf(stderr, "DEBUG: Generic instantiation name for '%s' is '%s'\n", tpl, m);

    // Check if already instantiated
    Instantiation *c = ctx->instantiations;
    while (c)
    {
        if (strcmp(c->name, m) == 0)
        {
            return; // Already done
        }
        c = c->next;
    }

    // Find the template
    GenericTemplate *t = ctx->templates;
    while (t)
    {
        if (strcmp(t->name, tpl) == 0)
        {
            break;
        }
        t = t->next;
    }
    if (!t)
    {
        zpanic_at(token, "Unknown generic: %s", tpl);
    }

    // Register instantiation first (to break cycles)
    Instantiation *ni = xmalloc(sizeof(Instantiation));
    ni->name = xstrdup(m);
    ni->template_name = xstrdup(tpl);
    ni->concrete_arg = (arg_count > 0) ? xstrdup(args[0]) : xstrdup("T");
    ni->struct_node = NULL;
    ni->next = ctx->instantiations;
    ctx->instantiations = ni;

    if (t->struct_node->type == NODE_STRUCT)
    {
        ASTNode *i = ast_create(NODE_STRUCT);
        i->strct.name = xstrdup(m);
        i->strct.is_template = 0;

        // Copy struct attributes
        i->strct.is_packed = t->struct_node->strct.is_packed;
        i->strct.is_union = t->struct_node->strct.is_union;
        i->strct.align = t->struct_node->strct.align;
        if (t->struct_node->strct.parent)
        {
            i->strct.parent = xstrdup(t->struct_node->strct.parent);
        }

        // Copy fields with sequential substitutions for each param
        ASTNode *fields = t->struct_node->strct.fields;
        int param_count = t->struct_node->strct.generic_param_count;

        if (param_count > 0 && arg_count > 0)
        {
            // First substitution
            i->strct.fields = copy_fields_replacing(
                ctx, fields, t->struct_node->strct.generic_params[0], args[0]);

            // Subsequent substitutions (for params B, C, etc.)
            for (int j = 1; j < param_count && j < arg_count; j++)
            {
                ASTNode *prev_fields = i->strct.fields;
                ASTNode *tmp = copy_fields_replacing(
                    ctx, prev_fields, t->struct_node->strct.generic_params[j], args[j]);
                free_field_list(prev_fields);
                i->strct.fields = tmp;
            }
        }
        else
        {
            i->strct.fields = copy_fields_replacing(ctx, fields, "T", "int");
        }

        ni->struct_node = i;
        register_struct_def(ctx, m, i);

        i->next = ctx->instantiated_structs;
        ctx->instantiated_structs = i;
    }
    else if (t->struct_node->type == NODE_ENUM)
    {
        ASTNode *i = ast_create(NODE_ENUM);
        i->enm.name = xstrdup(m);
        i->enm.is_template = 0;

        // Copy type attributes
        i->type_info = type_new(TYPE_ENUM);
        i->type_info->name = xstrdup(m);
        if (t->struct_node->type_info)
        {
            i->type_info->traits = t->struct_node->type_info->traits;
        }

        ASTNode *h = 0, *tl = 0;
        ASTNode *v = t->struct_node->enm.variants;
        while (v)
        {
            ASTNode *nv = ast_create(NODE_ENUM_VARIANT);
            nv->variant.name = xstrdup(v->variant.name);
            nv->variant.tag_id = v->variant.tag_id;

            // Use multi-parameter substitution for payload
            Type *payload = v->variant.payload;
            if (payload)
            {
                // We need to apply all substitutions

                // Actually, for multi-param enums, we should check how they are stored.
                // If it's Result<T, E>, generic_param is "T,E".
                // We use replace_type_formal which handles "T,E" as p and "int,float" as c.

                // Construct comma-separated concrete args string
                char c_args[1024] = {0};
                for (int j = 0; j < arg_count; j++)
                {
                    if (j > 0)
                    {
                        strcat(c_args, ",");
                    }
                    strcat(c_args, args[j]);
                }

                nv->variant.payload = replace_type_formal(
                    payload, t->struct_node->enm.generic_param, c_args, NULL, NULL);
            }

            size_t mangled_var_sz = strlen(m) + strlen(nv->variant.name) + 3;
            char *mangled_var = xmalloc(mangled_var_sz);
            snprintf(mangled_var, mangled_var_sz, "%s__%s", m, nv->variant.name);
            register_enum_variant(ctx, m, mangled_var, nv->variant.tag_id);

            // Register Constructor Function Signature for the instantiated variant
            if (nv->variant.payload)
            {
                Type **at = xmalloc(sizeof(Type *));
                at[0] = nv->variant.payload;
                Type *ret_t = type_new(TYPE_ENUM);
                ret_t->name = xstrdup(m);

                register_func(ctx, ctx->global_scope, mangled_var, 1, NULL, at, ret_t, 0, 0, 0,
                              token);
            }
            else
            {
                Type *ret_t = type_new(TYPE_ENUM);
                ret_t->name = xstrdup(m);
                register_func(ctx, ctx->global_scope, mangled_var, 0, NULL, NULL, ret_t, 0, 0, 0,
                              token);
            }

            free(mangled_var);
            if (!h)
            {
                h = nv;
            }
            else
            {
                tl->next = nv;
            }
            tl = nv;
            v = v->next;
        }
        i->enm.variants = h;
        ni->struct_node = i;
        register_struct_def(ctx, m, i);

        i->next = ctx->instantiated_structs;
        ctx->instantiated_structs = i;
    }
}

int is_file_imported(ParserContext *ctx, const char *p)
{
    ImportedFile *c = ctx->imported_files;
    while (c)
    {
        if (strcmp(c->path, p) == 0)
        {
            return 1;
        }
        c = c->next;
    }
    return 0;
}

void mark_file_imported(ParserContext *ctx, const char *p)
{
    ImportedFile *f = xmalloc(sizeof(ImportedFile));
    f->path = xstrdup(p);
    f->next = ctx->imported_files;
    ctx->imported_files = f;
}

char *parse_condition_raw(ParserContext *ctx, Lexer *l)
{
    (void)ctx; // suppress unused parameter warning
    Token t = lexer_peek(l);
    if (t.type == TOK_LPAREN)
    {
        Token op = lexer_next(l);
        const char *s = op.start;
        int d = 1;
        while (d > 0)
        {
            t = lexer_next(l);
            if (t.type == TOK_EOF)
            {
                zpanic_at(t, "Unterminated condition");
            }
            if (t.type == TOK_LPAREN)
            {
                d++;
            }
            if (t.type == TOK_RPAREN)
            {
                d--;
            }
        }
        const char *cs = s + 1;
        int len = t.start - cs;
        char *c = xmalloc(len + 1);
        strncpy(c, cs, len);
        c[len] = 0;
        return c;
    }
    else
    {
        const char *start = l->src + l->pos;
        while (1)
        {
            t = lexer_peek(l);
            if (t.type == TOK_LBRACE || t.type == TOK_EOF)
            {
                break;
            }
            lexer_next(l);
        }
        int len = (l->src + l->pos) - start;
        if (len == 0)
        {
            zpanic_at(lexer_peek(l), "Empty condition or missing body");
        }
        char *c = xmalloc(len + 1);
        strncpy(c, start, len);
        c[len] = 0;
        return c;
    }
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
            char *pc = strchr(base_t, '*');
            int is_ptr_type = (pc != NULL);
            if (pc)
            {
                *pc = 0;
            }

            // Resolve type alias if exists (for example: Vec2f -> Vec2_float)
            const char *resolved_type = find_type_alias(ctx, base_t);
            if (resolved_type)
            {
                free(base_t);
                base_t = xstrdup(resolved_type);
            }

            ASTNode *def = find_struct_def(ctx, base_t);
            int is_field = 0;
            if (def && (def->type == NODE_STRUCT))
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
            free(base_t);

            if (is_field)
            {
                dest -= strlen(acc);
                if (is_ptr_type)
                {
                    dest += sprintf(dest, "(%s)->%s", acc, method);
                }
                else
                {
                    dest += sprintf(dest, "(%s).%s", acc, method);
                }
                continue;
            }

            if (*src == '(')
            {
                dest -= strlen(acc);
                int paren_depth = 0;
                src++;
                paren_depth++;

                char ptr_check[64];
                strcpy(ptr_check, vtype);
                int is_ptr = (strchr(ptr_check, '*') != NULL);
                if (is_ptr)
                {
                    char *p = strchr(ptr_check, '*');
                    if (p)
                    {
                        *p = 0;
                    }
                }

                // Mixin Lookup Logic
                char target_func[256];
                sprintf(target_func, "%s__%s", ptr_check, method);

                char *final_cast = NULL;
                char *final_method = xstrdup(method);
                char *final_struct = xstrdup(ptr_check);

                // Check if method exists on primary struct
                if (!find_func(ctx, target_func))
                {
                    // Not found, check mixins
                    ASTNode *mixin_def = find_struct_def(ctx, ptr_check);
                    if (mixin_def && mixin_def->type == NODE_STRUCT &&
                        mixin_def->strct.used_structs)
                    {
                        for (int k = 0; k < mixin_def->strct.used_struct_count; k++)
                        {
                            char mixin_func[128];
                            sprintf(mixin_func, "%s__%s", mixin_def->strct.used_structs[k], method);
                            if (find_func(ctx, mixin_func))
                            {
                                // Found in mixin!
                                free(final_struct);
                                final_struct = xstrdup(mixin_def->strct.used_structs[k]);

                                // Create cast string: (Mixin*) or (Mixin*)&
                                char cast_buf[128];
                                if (is_ptr)
                                {
                                    sprintf(cast_buf, "(%s*)", final_struct);
                                }
                                else
                                {
                                    sprintf(cast_buf, "(%s*)&", final_struct);
                                }
                                final_cast = xstrdup(cast_buf);
                                break;
                            }
                        }
                    }
                }

                if (final_cast)
                {
                    // Mixin call: Foo__method((Foo*)&obj
                    dest +=
                        sprintf(dest, "%s__%s(%s%s", final_struct, final_method, final_cast, acc);
                    free(final_cast);
                }
                else
                {
                    // Standard call
                    dest += sprintf(dest, "%s__%s(%s%s", final_struct, final_method,
                                    is_ptr ? "" : "&", acc);
                }
                free(final_struct);
                free(final_method);

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
                char ptr_check[64];
                strcpy(ptr_check, vtype);
                int is_ptr = (strchr(ptr_check, '*') != NULL);
                if (is_ptr)
                {
                    char *p = strchr(ptr_check, '*');
                    if (p)
                    {
                        *p = 0;
                    }
                }
                // Mixin Lookup Logic (No Parens)
                char target_func[256];
                sprintf(target_func, "%s__%s", ptr_check, method);

                char *final_cast = NULL;
                char *final_method = xstrdup(method);
                char *final_struct = xstrdup(ptr_check);

                // Check if method exists on primary struct
                if (!find_func(ctx, target_func))
                {
                    // Not found, check mixins
                    ASTNode *mixin_def = find_struct_def(ctx, ptr_check);
                    if (mixin_def && mixin_def->type == NODE_STRUCT &&
                        mixin_def->strct.used_structs)
                    {
                        for (int k = 0; k < mixin_def->strct.used_struct_count; k++)
                        {
                            char mixin_func[128];
                            sprintf(mixin_func, "%s__%s", mixin_def->strct.used_structs[k], method);
                            if (find_func(ctx, mixin_func))
                            {
                                // Found in mixin!
                                free(final_struct);
                                final_struct = xstrdup(mixin_def->strct.used_structs[k]);

                                // Create cast string: (Mixin*) or (Mixin*)&
                                char cast_buf[128];
                                if (is_ptr)
                                {
                                    sprintf(cast_buf, "(%s*)", final_struct);
                                }
                                else
                                {
                                    sprintf(cast_buf, "(%s*)&", final_struct);
                                }
                                final_cast = xstrdup(cast_buf);
                                break;
                            }
                        }
                    }
                }

                if (final_cast)
                {
                    dest +=
                        sprintf(dest, "%s__%s(%s%s)", final_struct, final_method, final_cast, acc);
                    free(final_cast);
                }
                else
                {
                    dest += sprintf(dest, "%s__%s(%s%s)", final_struct, final_method,
                                    is_ptr ? "" : "&", acc);
                }
                free(final_struct);
                free(final_method);
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
                dest += sprintf(dest, "%s", field);
            }
            else
            {
                ASTNode *sdef = find_struct_def(ctx, acc);
                if (sdef && sdef->type == NODE_ENUM)
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
                        dest += sprintf(dest, "%s_%s", acc, field);
                    }
                    else
                    {
                        // Static method on Enum
                        dest += sprintf(dest, "%s__%s", acc, field);
                    }
                }
                else if (sdef || !mod)
                {
                    // Struct static method, or Type static method
                    dest += sprintf(dest, "%s__%s", acc, field);
                }
                else
                {
                    // Module function
                    dest += sprintf(dest, "%s_%s", acc, field);
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

                    char mangled[256];

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
                            snprintf(mangled, sizeof(mangled), "%s_%s", mod->base_name, method);
                        }
                    }
                    else
                    {
                        ASTNode *sdef = find_struct_def(ctx, use_name);
                        if (sdef)
                        {
                            snprintf(mangled, sizeof(mangled), "%s__%s", use_name, method);
                        }
                        else
                        {
                            snprintf(mangled, sizeof(mangled), "%s_%s", use_name, method);
                        }
                    }

                    if (*src == ')')
                    {
                        dest += sprintf(dest, "%s()", mangled);
                        src++;
                    }
                    else
                    {
                        FuncSig *sig = find_func(ctx, func_name);
                        if (sig)
                        {
                            dest += sprintf(dest, "%s(&(%s){0}", mangled, func_name);
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
                            dest += sprintf(dest, "%s(", mangled);
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

char *consume_and_rewrite(ParserContext *ctx, Lexer *l)
{
    char *r = consume_until_semicolon(l);
    char *rw = rewrite_expr_methods(ctx, r);
    free(r);
    return rw;
}

char *parse_and_convert_args(ParserContext *ctx, Lexer *l, char ***defaults_out,
                             ASTNode ***default_values_out, int *count_out, Type ***types_out,
                             char ***names_out, int *is_varargs_out, char ***ctype_overrides_out)
{
    Token t = lexer_next(l);
    if (t.type != TOK_LPAREN)
    {
        zpanic_at(t, "Expected '(' in function args");
    }

    size_t buf_size = 8192;
    char *buf = xmalloc(buf_size);
    buf[0] = 0;
    int count = 0;
    char **defaults = xmalloc(sizeof(char *) * 16);
    ASTNode **default_values = xmalloc(sizeof(ASTNode *) * 16);
    Type **types = xmalloc(sizeof(Type *) * 16);
    char **names = xmalloc(sizeof(char *) * 16);
    char **ctype_overrides = xmalloc(sizeof(char *) * 16);

    for (int i = 0; i < 16; i++)
    {
        defaults[i] = NULL;
        default_values[i] = NULL;
        types[i] = NULL;
        names[i] = NULL;
        ctype_overrides[i] = NULL;
    }

    if (lexer_peek(l).type != TOK_RPAREN)
    {
        while (1)
        {
            // Check for @ctype("...") before parameter
            char *ctype_override = NULL;
            if (lexer_peek(l).type == TOK_AT)
            {
                lexer_next(l); // eat @
                Token attr = lexer_next(l);
                if (attr.type == TOK_IDENT && attr.len == 5 && strncmp(attr.start, "ctype", 5) == 0)
                {
                    if (lexer_next(l).type != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after @ctype");
                    }
                    Token ctype_tok = lexer_next(l);
                    if (ctype_tok.type != TOK_STRING)
                    {
                        zpanic_at(ctype_tok, "@ctype requires a string argument");
                    }
                    // Extract string content (strip quotes)
                    ctype_override = xmalloc(ctype_tok.len - 1);
                    strncpy(ctype_override, ctype_tok.start + 1, ctype_tok.len - 2);
                    ctype_override[ctype_tok.len - 2] = 0;
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after @ctype string");
                    }
                }
                else
                {
                    zpanic_at(attr, "Unknown parameter attribute @%.*s", attr.len, attr.start);
                }
            }

            Token param_tok = lexer_next(l);
            // Handle 'self'
            if (param_tok.type == TOK_IDENT && strncmp(param_tok.start, "self", 4) == 0 &&
                param_tok.len == 4)
            {
                names[count] = xstrdup("self");
                if (ctx->current_impl_struct)
                {
                    char *buf_type = xmalloc(strlen(ctx->current_impl_struct) + 2);
                    sprintf(buf_type, "%s*", ctx->current_impl_struct);

                    if (is_primitive_type_name(ctx->current_impl_struct))
                    {
                        // Primitives: self is a pointer in signature and body
                        TypeKind pk = get_primitive_type_kind(ctx->current_impl_struct);
                        Type *bt = type_new(pk);
                        if (pk == TYPE_STRUCT)
                        { // Fallback if get_primitive_type_kind failed for some reason
                            bt->name = xstrdup(ctx->current_impl_struct);
                        }
                        Type *ptr = type_new_ptr(bt);

                        add_symbol(ctx, "self", buf_type, ptr);
                        types[count] = ptr;
                    }
                    else
                    {
                        // Structs: self is a pointer in signature and body
                        Type *st = type_new(TYPE_STRUCT);
                        st->name = xstrdup(ctx->current_impl_struct);
                        Type *ptr = type_new_ptr(st);

                        add_symbol(ctx, "self", buf_type, ptr);
                        types[count] = ptr;
                    }
                    free(buf_type);
                    strcat(buf, "void* self");
                }
                else
                {
                    strcat(buf, "void* self");
                    types[count] = type_new_ptr(type_new(TYPE_VOID));
                    add_symbol(ctx, "self", "void*", types[count]);
                }
                ctype_overrides[count] = ctype_override;
                count++;
            }
            else
            {
                if (param_tok.type != TOK_IDENT)
                {
                    zpanic_at(lexer_peek(l), "Expected arg name");
                }
                check_identifier(ctx, param_tok);
                char *name = token_strdup(param_tok);
                names[count] = name; // Store name
                if (lexer_next(l).type != TOK_COLON)
                {
                    zpanic_at(lexer_peek(l), "Expected ':'");
                }

                Type *arg_type = parse_type_formal(ctx, l);
                char *type_str = type_to_string(arg_type);

                add_symbol(ctx, name, type_str, arg_type);
                types[count] = arg_type;

                if (strlen(buf) > 0)
                {
                    strcat(buf, ", ");
                }

                char *fn_ptr = strstr(type_str, "(*)");
                if (get_inner_type(arg_type)->kind == TYPE_FUNCTION)
                {
                    strcat(buf, "z_closure_T ");
                    strcat(buf, name);
                }
                else if (fn_ptr)
                {
                    // Inject name into function pointer: int (*)(int) -> int (*name)(int)
                    int prefix_len = fn_ptr - type_str;
                    strncat(buf, type_str, prefix_len);
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

                if (lexer_peek(l).type == TOK_OP && is_token(lexer_peek(l), "="))
                {
                    lexer_next(l); // consume =

                    // Parse the expression into an AST node
                    ASTNode *def_node = parse_expression(ctx, l);

                    // Store both the AST node and the reconstructed string for legacy support
                    default_values[count - 1] = def_node;
                    defaults[count - 1] = ast_to_string(def_node);
                }
            }
            if (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
                // Check if next is ...
                if (lexer_peek(l).type == TOK_ELLIPSIS)
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
    if (lexer_next(l).type != TOK_RPAREN)
    {
        zpanic_at(lexer_peek(l), "Expected ')' after args");
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

static const char *get_closest_type_hint(ParserContext *ctx, const char *name)
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
        if (er->node && er->node->type == NODE_ENUM)
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

void register_plugin(ParserContext *ctx, const char *name, const char *alias)
{
    // Try to find existing (built-in) or already loaded plugin
    ZPlugin *plugin = zptr_find_plugin(name);

    // If not found, try to load it dynamically
    if (!plugin)
    {
        plugin = zptr_load_plugin(name);

        if (!plugin)
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s%s", name, z_get_plugin_ext());
            plugin = zptr_load_plugin(path);
        }

        if (!plugin && !strchr(name, '/'))
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s%s%s", z_get_run_prefix(), name, z_get_plugin_ext());
            plugin = zptr_load_plugin(path);
        }
    }

    if (!plugin)
    {
        fprintf(stderr,
                COLOR_RED "Error:" COLOR_RESET " Could not load plugin '%s'\n"
                          "       Tried built-ins and dynamic loading (.so)\n",
                name);
        if (g_config.mode_lsp)
        {
            return;
        }
        exit(1);
    }

    ImportedPlugin *p = xmalloc(sizeof(ImportedPlugin));
    p->name = xstrdup(plugin->name); // Use the plugin's internal name
    p->alias = alias ? xstrdup(alias) : NULL;
    p->next = ctx->imported_plugins;
    ctx->imported_plugins = p;
}

const char *resolve_plugin(ParserContext *ctx, const char *name_or_alias)
{
    for (ImportedPlugin *p = ctx->imported_plugins; p; p = p->next)
    {
        // Check if it matches the alias
        if (p->alias && strcmp(p->alias, name_or_alias) == 0)
        {
            return p->name;
        }
        // Check if it matches the name
        if (strcmp(p->name, name_or_alias) == 0)
        {
            return p->name;
        }
    }
    return NULL; // Plugin not found
}

// Type Validation
void register_type_usage(ParserContext *ctx, const char *name, Token t)
{
    if (ctx->is_speculative)
    {
        return;
    }

    TypeUsage *u = xmalloc(sizeof(TypeUsage));
    u->name = xstrdup(name);
    u->location = t;
    u->next = ctx->pending_type_validations;
    ctx->pending_type_validations = u;
}

int validate_types(ParserContext *ctx)
{
    int errors = 0;
    TypeUsage *u = ctx->pending_type_validations;
    while (u)
    {
        ASTNode *def = find_struct_def(ctx, u->name);
        if (!def)
        {
            const char *alias = find_type_alias(ctx, u->name);
            if (!alias)
            {
                SelectiveImport *si = find_selective_import(ctx, u->name);
                if (!si && !is_extern_symbol(ctx, u->name))
                {
                    if (!is_trait(u->name) && TYPE_UNKNOWN == find_primitive_kind(u->name))
                    {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Unknown type '%s' (assuming external C struct)",
                                 u->name);
                        const char *hint = get_closest_type_hint(ctx, u->name);
                        if (hint)
                        {
                            char help[512];
                            snprintf(help, sizeof(help), "Did you mean '%s'?", hint);
                            zwarn_with_suggestion(u->location, msg, help);
                        }
                        else
                        {
                            zwarn_at(u->location, "%s", msg);
                        }
                    }
                }
            }
        }
        u = u->next;
    }
    return errors == 0;
}

void propagate_vector_inner_types(ParserContext *ctx)
{
    StructRef *ref = ctx->parsed_structs_list;
    while (ref)
    {
        ASTNode *strct = ref->node;
        if (strct && strct->type == NODE_STRUCT && strct->type_info &&
            strct->type_info->kind == TYPE_VECTOR && !strct->type_info->inner)
        {
            if (strct->strct.fields && strct->strct.fields->type_info)
            {
                strct->type_info->inner = strct->strct.fields->type_info;
            }
        }
        ref = ref->next;
    }
}

void propagate_drop_traits(ParserContext *ctx)
{
    int changed = 1;
    while (changed)
    {
        changed = 0;

        // Process regular structs
        StructRef *ref = ctx->parsed_structs_list;
        while (ref)
        {
            ASTNode *strct = ref->node;
            if (strct && strct->type == NODE_STRUCT && strct->type_info &&
                !strct->type_info->traits.has_drop)
            {
                ASTNode *field = strct->strct.fields;
                while (field)
                {
                    Type *ft = field->type_info;
                    if (ft)
                    {
                        if (ft->kind == TYPE_VECTOR)
                        {
                            strct->type_info->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_FUNCTION && ft->traits.has_drop && !ft->is_raw)
                        {
                            strct->type_info->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_STRUCT && ft->name)
                        {
                            ASTNode *fdef = find_struct_def(ctx, ft->name);
                            if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                            {
                                strct->type_info->traits.has_drop = 1;
                                changed = 1;
                                break;
                            }
                        }
                    }
                    field = field->next;
                }
            }
            ref = ref->next;
        }

        // Process instantiated templates
        ASTNode *ins = ctx->instantiated_structs;
        while (ins)
        {
            if (ins->type == NODE_STRUCT && ins->type_info && !ins->type_info->traits.has_drop)
            {
                ASTNode *field = ins->strct.fields;
                while (field)
                {
                    Type *ft = field->type_info;
                    if (ft)
                    {
                        if (ft->kind == TYPE_VECTOR)
                        {
                            ins->type_info->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_FUNCTION && ft->traits.has_drop && !ft->is_raw)
                        {
                            ins->type_info->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_STRUCT && ft->name)
                        {
                            ASTNode *fdef = find_struct_def(ctx, ft->name);
                            if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                            {
                                ins->type_info->traits.has_drop = 1;
                                changed = 1;
                                break;
                            }
                        }
                    }
                    field = field->next;
                }
            }
            ins = ins->next;
        }
    }
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
    // Lexer-level keywords
    switch (t.type)
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

    if (t.type == TOK_IDENT)
    {
        static const char *pseudo_keywords[] = {
            "let",   "var",      "static", "const",  "return", "if",    "else",   "while",
            "break", "continue", "loop",   "repeat", "unless", "guard", "launch", "do",
            "goto",  "plugin",   "fn",     "struct", "enum",   "self",  NULL};

        for (int i = 0; pseudo_keywords[i] != NULL; i++)
        {
            if (t.len == (int)strlen(pseudo_keywords[i]) &&
                strncmp(t.start, pseudo_keywords[i], t.len) == 0)
            {
                return 1;
            }
        }
    }

    return 0;
}

void check_identifier(ParserContext *ctx, Token t)
{
    (void)ctx;
    if (is_reserved_keyword(t))
    {
        char buf[256];
        char name[64];
        int len = t.len < 63 ? t.len : 63;
        strncpy(name, t.start, len);
        name[len] = 0;
        snprintf(buf, sizeof(buf), "Cannot use reserved keyword '%s' as an identifier", name);
        zpanic_at(t, "%s", buf);
    }
}

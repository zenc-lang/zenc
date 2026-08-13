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

static unsigned int struct_name_hash(const char *name)
{
    unsigned int h = 2166136261u;
    while (*name)
    {
        h ^= (unsigned char)*name++;
        h *= 16777619u;
    }
    return h;
}

void struct_hash_insert(ParserContext *ctx, const char *name, ASTNode *node)
{
    unsigned int idx = struct_name_hash(name) & (STRUCT_HASH_SIZE - 1);
    for (int i = 0; i < STRUCT_HASH_SIZE; i++)
    {
        unsigned int slot =
            ((unsigned)(idx) + (unsigned)(i) + STRUCT_HASH_SIZE) & (STRUCT_HASH_SIZE - 1U);
        if (!ctx->struct_hash[slot].name[0] || (ctx->struct_hash[slot].name[0] == name[0] &&
                                                strcmp(ctx->struct_hash[slot].name, name) == 0))
        {
            strncpy(ctx->struct_hash[slot].name, name, sizeof(ctx->struct_hash[slot].name) - 1);
            ctx->struct_hash[slot].name[sizeof(ctx->struct_hash[slot].name) - 1] = '\0';
            ctx->struct_hash[slot].node = node;
            return;
        }
    }
}

static ASTNode *struct_hash_lookup(ParserContext *ctx, const char *name)
{
    unsigned int idx = struct_name_hash(name) & (STRUCT_HASH_SIZE - 1);
    for (int i = 0; i < STRUCT_HASH_SIZE; i++)
    {
        unsigned int slot =
            ((unsigned)(idx) + (unsigned)(i) + STRUCT_HASH_SIZE) & (STRUCT_HASH_SIZE - 1U);
        if (!ctx->struct_hash[slot].name[0])
        {
            return NULL;
        }
        if (ctx->struct_hash[slot].name[0] == name[0] &&
            strcmp(ctx->struct_hash[slot].name, name) == 0)
        {
            return ctx->struct_hash[slot].node;
        }
    }
    return NULL;
}

#define CACHE_RESULT(node)                                                                         \
    do                                                                                             \
    {                                                                                              \
        if (node)                                                                                  \
        {                                                                                          \
            struct_hash_insert(ctx, name, node);                                                   \
            return node;                                                                           \
        }                                                                                          \
    } while (0)

void add_to_struct_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_structs_list;
    ctx->parsed_structs_list = r;

    if (node->type == NODE_STRUCT && node->strct.name)
    {
        struct_hash_insert(ctx, node->strct.name, node);
    }
    else if (node->type == NODE_ENUM && node->enm.name)
    {
        struct_hash_insert(ctx, node->enm.name, node);
    }
}

void register_type_alias(ParserContext *ctx, const char *alias, const char *original,
                         Type *type_info, int is_opaque, const char *defined_in_file, Token tok,
                         int is_export)
{
    if (ctx->config->mode_lsp)
    {
        TypeAlias *existing = find_type_alias_node(ctx, alias);
        if (existing)
        {
            existing->original_type = xstrdup(original);
            existing->type_info = type_info;
            existing->is_opaque = is_opaque;
            existing->defined_in_file = defined_in_file ? xstrdup(defined_in_file) : NULL;
        }
    }

    TypeAlias *ta = NULL;
    if (ctx->config->mode_lsp)
    {
        ta = find_type_alias_node(ctx, alias);
    }

    if (!ta)
    {
        ta = xmalloc(sizeof(TypeAlias));
        ta->alias = xstrdup(alias);
        ta->next = ctx->type_aliases;
        ctx->type_aliases = ta;
    }

    ta->original_type = xstrdup(original);
    ta->type_info = type_info;
    ta->is_opaque = is_opaque;
    ta->defined_in_file = defined_in_file ? xstrdup(defined_in_file) : NULL;

    audit_section_5(ctx, ctx->current_scope, alias, NULL, tok);
    ZenSymbol *sym = symbol_lookup_local(ctx->current_scope, alias);
    if (!sym)
    {
        sym = symbol_add(ctx->current_scope, alias, SYM_ALIAS);
    }
    else
    {
        sym->kind = SYM_ALIAS;
    }
    sym->decl_token = tok;
    sym->is_export = is_export;
    sym->data.alias.original_type = xstrdup(original);
    sym->type_info = type_info;
    register_symbol_to_lsp(ctx, sym);
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
        if (ta->alias[0] == alias[0] && strcmp(ta->alias, alias) == 0)
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
    StructRef *curr = ctx->parsed_funcs_list;
    while (curr)
    {
        if (curr->node == node)
        {
            return;
        }
        curr = curr->next;
    }
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_funcs_list;
    ctx->parsed_funcs_list = r;
}

void add_to_impl_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *curr = ctx->parsed_impls_list;
    while (curr)
    {
        if (curr->node == node)
        {
            return;
        }
        curr = curr->next;
    }
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_impls_list;
    ctx->parsed_impls_list = r;
}

void add_to_global_list(ParserContext *ctx, ASTNode *node)
{
    StructRef *curr = ctx->parsed_globals_list;
    while (curr)
    {
        if (curr->node == node)
        {
            return;
        }
        curr = curr->next;
    }
    StructRef *r = xmalloc(sizeof(StructRef));
    r->node = node;
    r->next = ctx->parsed_globals_list;
    ctx->parsed_globals_list = r;
}

void register_slice(ParserContext *ctx, const char *type)
{
    if (is_known_generic(ctx, (char *)type))
    {
        return;
    }

    SliceType *c = ctx->used_slices;
    while (c)
    {
        if (c->name[0] == type[0] && strcmp(c->name, type) == 0)
        {
            return;
        }
        c = c->next;
    }
    SliceType *n = xmalloc(sizeof(SliceType));
    n->name = xstrdup(type);
    n->next = ctx->used_slices;
    ctx->used_slices = n;

    char slice_name[MAX_TYPE_NAME_LEN];
    snprintf(slice_name, sizeof(slice_name), "Slice__%s", type);

    ASTNode *len_f = ast_create(NODE_FIELD);
    len_f->field.name = xstrdup("len");
    len_f->field.type = xstrdup("int");
    ASTNode *cap_f = ast_create(NODE_FIELD);
    cap_f->field.name = xstrdup("cap");
    cap_f->field.type = xstrdup("int");
    ASTNode *data_f = ast_create(NODE_FIELD);
    data_f->field.name = xstrdup("data");
    char ptr_type[MAX_TYPE_NAME_LEN];
    snprintf(ptr_type, sizeof(ptr_type), "%s*", type);
    data_f->field.type = xstrdup(ptr_type);

    data_f->next = len_f;
    len_f->next = cap_f;

    ASTNode *def = ast_create(NODE_STRUCT);
    def->strct.name = xstrdup(slice_name);
    def->strct.fields = data_f;

    register_struct_def(ctx, slice_name, def);

    char legacy_name[MAX_VAR_NAME_LEN];
    snprintf(legacy_name, sizeof(legacy_name), "Slice_%s", type);
    if (slice_name[0] != legacy_name[0] || strcmp(slice_name, legacy_name) != 0)
    {
        register_type_alias(ctx, legacy_name, slice_name, NULL, 0, NULL, TOKEN_UNKNOWN, 0);
    }
}

void register_tuple_with_types(ParserContext *ctx, const char *sig, const char **types, int count)
{
    TupleType *c = ctx->used_tuples;
    while (c)
    {
        if (c->sig[0] == sig[0] && strcmp(c->sig, sig) == 0)
        {
            return;
        }
        c = c->next;
    }
    TupleType *n = xmalloc(sizeof(TupleType));
    n->sig = xstrdup(sig);
    n->types = xmalloc(sizeof(char *) * (size_t)count);
    for (int i = 0; i < count; i++)
    {
        n->types[i] = xstrdup(types[i]);
    }
    n->count = count;
    n->next = ctx->used_tuples;
    ctx->used_tuples = n;

    char struct_name[MAX_ERROR_MSG_LEN];
    char *clean_sig = sanitize_mangled_name(sig);
    snprintf(struct_name, sizeof(struct_name), "Tuple__%s", clean_sig);
    zfree(clean_sig);

    ASTNode *s_def = ast_create(NODE_STRUCT);
    s_def->strct.name = xstrdup(struct_name);
    ASTNode *head = NULL, *tail = NULL;
    for (int fi = 0; fi < count; fi++)
    {
        ASTNode *f = ast_create(NODE_FIELD);
        char fname[32];
        snprintf(fname, sizeof(fname), "v%d", fi);
        f->field.name = xstrdup(fname);
        f->field.type = xstrdup(types[fi]);
        if (!head)
        {
            head = f;
        }
        else
        {
            tail->next = f;
        }
        tail = f;
    }
    s_def->strct.fields = head;
    register_struct_def(ctx, struct_name, s_def);
}

void register_struct_def(ParserContext *ctx, const char *name, ASTNode *node)
{
    if (ctx->config->mode_lsp)
    {
        StructDef *existing = NULL;
        StructDef *curr = ctx->struct_defs;
        while (curr)
        {
            if (curr->name[0] == name[0] && strcmp(curr->name, name) == 0)
            {
                existing = curr;
                break;
            }
            curr = curr->next;
        }
        if (existing)
        {
            existing->node = node;
        }
    }

    StructDef *d = NULL;
    if (ctx->config->mode_lsp)
    {
        StructDef *curr = ctx->struct_defs;
        while (curr)
        {
            if (curr->name[0] == name[0] && strcmp(curr->name, name) == 0)
            {
                d = curr;
                break;
            }
            curr = curr->next;
        }
    }

    if (!d)
    {
        d = xmalloc(sizeof(StructDef));
        d->name = xstrdup(name);
        d->next = ctx->struct_defs;
        ctx->struct_defs = d;
    }

    d->node = node;
    struct_hash_insert(ctx, name, node);

    if (ctx->config->misra_mode)
    {
        ZenSymbol *all = ctx->all_symbols;
        while (all)
        {
            if ((all->kind == SYM_STRUCT || all->kind == SYM_ENUM) && all->name[0] == name[0] &&
                strcmp(all->name, name) == 0)
            {
                zerror_at(node ? node->token : TOKEN_UNKNOWN, "MISRA Rule 5.7");
                break;
            }
            all = all->next;
        }
    }

    ZenSymbol *sym_existing = symbol_lookup_local(ctx->global_scope, name);
    ZenSymbol *sym = NULL;
    if (!sym_existing)
    {
        sym = symbol_add(ctx->global_scope, name,
                         (node && node->type == NODE_ENUM) ? SYM_ENUM : SYM_STRUCT);
    }
    else
    {
        sym = sym_existing;
        sym->kind = (node && node->type == NODE_ENUM) ? SYM_ENUM : SYM_STRUCT;
    }

    sym->data.node = node;
    sym->link_name = node ? node->link_name : NULL;
    if (node)
    {
        sym->decl_token = node->token;
        if (node->type == NODE_STRUCT)
        {
            sym->is_export = node->strct.is_export;
        }
        else if (node->type == NODE_ENUM)
        {
            sym->is_export = node->enm.is_export;
        }
    }
    sym->type_info = node ? node->type_info : NULL;

    audit_section_5(ctx, ctx->global_scope, name, sym->link_name, sym->decl_token);

    register_symbol_to_lsp(ctx, sym);
}

ASTNode *find_struct_def(ParserContext *ctx, const char *name)
{
    if (ctx)
    {
        ASTNode *cached = struct_hash_lookup(ctx, name);
        if (cached)
        {
            return cached;
        }
    }

    if (!ctx)
    {
        return NULL;
    }

    ZenSymbol *sym = symbol_lookup_kind(ctx->current_scope, name, SYM_STRUCT);
    if (!sym)
    {
        sym = symbol_lookup_kind(ctx->current_scope, name, SYM_ENUM);
    }
    if (sym && sym->data.node)
    {
        CACHE_RESULT(sym->data.node);
    }

    if (ctx->cg.global_user_structs)
    {
        ASTNode *s = ctx->cg.global_user_structs;
        while (s)
        {
            if ((s->type == NODE_STRUCT || s->type == NODE_ENUM) &&
                (s->type == NODE_STRUCT ? s->strct.name : s->enm.name)[0] == name[0] &&
                strcmp((s->type == NODE_STRUCT ? s->strct.name : s->enm.name), name) == 0)
            {
                if (!(s->type == NODE_STRUCT && s->strct.is_incomplete))
                {
                    CACHE_RESULT(s);
                }
            }
            s = s->next;
        }
    }

    Instantiation *i = ctx->instantiations;
    while (i)
    {
        if (i->name[0] == name[0] && strcmp(i->name, name) == 0)
        {
            CACHE_RESULT(i->struct_node);
        }
        i = i->next;
    }

    ASTNode *s = ctx->instantiated_structs;
    while (s)
    {
        if ((s->type == NODE_STRUCT || s->type == NODE_ENUM) &&
            (s->type == NODE_STRUCT ? s->strct.name : s->enm.name)[0] == name[0] &&
            strcmp((s->type == NODE_STRUCT ? s->strct.name : s->enm.name), name) == 0)
        {
            CACHE_RESULT(s);
        }
        s = s->next;
    }

    StructRef *r = ctx->parsed_structs_list;
    while (r)
    {
        if (r->node->type == NODE_STRUCT && r->node->strct.name[0] == name[0] &&
            strcmp(r->node->strct.name, name) == 0)
        {
            CACHE_RESULT(r->node);
        }
        if (r->node->type == NODE_ENUM && r->node->enm.name[0] == name[0] &&
            strcmp(r->node->enm.name, name) == 0)
        {
            CACHE_RESULT(r->node);
        }
        r = r->next;
    }

    ZenSymbol *all = ctx->all_symbols;
    while (all)
    {
        if ((all->kind == SYM_STRUCT || all->kind == SYM_ENUM) && all->name[0] == name[0] &&
            strcmp(all->name, name) == 0 && all->data.node)
        {
            CACHE_RESULT(all->data.node);
        }
        all = all->next;
    }

    StructDef *d = ctx->struct_defs;
    while (d)
    {
        if (d->name[0] == name[0] && strcmp(d->name, name) == 0)
        {
            CACHE_RESULT(d->node);
        }
        d = d->next;
    }

    StructRef *e = ctx->parsed_enums_list;
    while (e)
    {
        if (e->node->type == NODE_ENUM && e->node->enm.name[0] == name[0] &&
            strcmp(e->node->enm.name, name) == 0)
        {
            CACHE_RESULT(e->node);
        }
        e = e->next;
    }

    return NULL;
}

#undef CACHE_RESULT

ASTNode *find_trait_def(ParserContext *ctx, const char *name)
{
    if (!ctx || !name)
    {
        return NULL;
    }

    StructRef *r = ctx->parsed_globals_list;
    while (r)
    {
        if (r->node && r->node->type == NODE_TRAIT && r->node->trait.name[0] == name[0] &&
            strcmp(r->node->trait.name, name) == 0)
        {
            return r->node;
        }
        r = r->next;
    }
    return NULL;
}

ASTNode *find_concrete_struct_def(ParserContext *ctx, const char *name)
{
    Instantiation *i = ctx->instantiations;
    while (i)
    {
        if (i->name[0] == name[0] && strcmp(i->name, name) == 0 && i->struct_node &&
            i->struct_node->type == NODE_STRUCT && !i->struct_node->strct.is_template)
        {
            return i->struct_node;
        }
        i = i->next;
    }

    ASTNode *s = ctx->instantiated_structs;
    while (s)
    {
        if (s->type == NODE_STRUCT && !s->strct.is_template && s->strct.name[0] == name[0] &&
            strcmp(s->strct.name, name) == 0)
        {
            return s;
        }
        s = s->next;
    }

    StructRef *r = ctx->parsed_structs_list;
    while (r)
    {
        if (r->node->type == NODE_STRUCT && !r->node->strct.is_template &&
            r->node->strct.name[0] == name[0] && strcmp(r->node->strct.name, name) == 0)
        {
            return r->node;
        }
        r = r->next;
    }

    StructDef *d = ctx->struct_defs;
    while (d)
    {
        if (d->node && d->node->type == NODE_STRUCT && !d->node->strct.is_template &&
            d->name[0] == name[0] && strcmp(d->name, name) == 0)
        {
            return d->node;
        }
        d = d->next;
    }

    return NULL;
}

Module *find_module(ParserContext *ctx, const char *alias)
{
    Module **mod_ptr = zmap_get(&ctx->imports.modules, alias);
    return mod_ptr ? *mod_ptr : NULL;
}

void register_selective_import(ParserContext *ctx, const char *symbol, const char *alias,
                               const char *source_module)
{
    SelectiveImport *si = xmalloc(sizeof(SelectiveImport));
    si->symbol = xstrdup(symbol);
    si->alias = alias ? xstrdup(alias) : NULL;
    si->source_module = xstrdup(source_module);
    const char *key = alias ? alias : symbol;
    zmap_put(&ctx->imports.selective_imports, key, si);
}

SelectiveImport *find_selective_import(ParserContext *ctx, const char *name)
{
    SelectiveImport **si_ptr = zmap_get(&ctx->imports.selective_imports, name);
    return si_ptr ? *si_ptr : NULL;
}

void re_export_propagated(ParserContext *ctx, const char *alias, const char *parent_prefix,
                          const char *base_name)
{
    if (!parent_prefix || !alias)
    {
        return;
    }
    char combined[MAX_PATH_LEN];
    snprintf(combined, sizeof(combined), "%s__%s", parent_prefix, alias);

    if (!zmap_get(&ctx->imports.modules, combined))
    {
        Module *new_mod = xmalloc(sizeof(Module));
        new_mod->alias = xstrdup(combined);
        new_mod->path = NULL;
        new_mod->base_name = xstrdup(base_name ? base_name : alias);
        new_mod->is_c_header = 0;
        new_mod->is_re_export = 1;
        zmap_put(&ctx->imports.modules, xstrdup(combined), new_mod);
    }
}

void re_export_wildcard_symbols(ParserContext *ctx, const char *module_base)
{
    if (!ctx->global_scope || !module_base)
    {
        return;
    }
    size_t prefix_len = strlen(module_base) + 2;
    char *prefix = xmalloc(prefix_len + 1);
    snprintf(prefix, prefix_len + 1, "%s__", module_base);

    ZenSymbol *sym = ctx->global_scope->symbols;
    while (sym)
    {
        if (sym->name && strncmp(sym->name, prefix, (size_t)(prefix_len)) == 0)
        {
            const char *bare_name = sym->name + prefix_len;
            if (!*bare_name)
            {
                sym = sym->next;
                continue;
            }

            if (zmap_get(&ctx->imports.selective_imports, bare_name))
            {
                sym = sym->next;
                continue;
            }

            register_selective_import(ctx, bare_name, NULL, module_base);
        }
        sym = sym->next;
    }
    zfree(prefix);
}

char *extract_module_name(const char *path)
{
    const char *slash = (char *)strrchr(path, '/');
    const char *backslash = (char *)strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash))
    {
        slash = backslash;
    }

    const char *base = slash ? slash + 1 : path;
    const char *dot = (char *)strrchr(base, '.');
    int len = dot ? (int)(dot - base) : (int)strlen(base);
    char *name = xmalloc((size_t)(len + 1));
    strncpy(name, base, (size_t)(len));
    name[len] = 0;

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

int is_file_imported(ParserContext *ctx, const char *path)
{
    return zmap_get(&ctx->imports.imported_files, path) != NULL;
}

void mark_file_imported(ParserContext *ctx, const char *path)
{
    zmap_put(&ctx->imports.imported_files, path, path);
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
        if (r->trait[0] == trait[0] && strcmp(r->trait, trait) == 0 && r->strct[0] == strct[0] &&
            strcmp(r->strct, strct) == 0)
        {
            return 1;
        }
        r = r->next;
    }

    r = ctx->registered_impls;
    while (r)
    {
        char *base_reg = xstrdup(r->strct);
        char *ptr2 = (char *)strchr(base_reg, '<');
        if (ptr2)
        {
            *ptr2 = 0;
            size_t blen = strlen(base_reg);
            if (strncmp(strct, base_reg, (size_t)(blen)) == 0 && strct[blen] == '_')
            {
                if (r->trait[0] == trait[0] && strcmp(r->trait, trait) == 0)
                {
                    zfree(base_reg);
                    return 1;
                }
            }
        }
        zfree(base_reg);
        r = r->next;
    }

    return 0;
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
        if (c->name[0] == name[0] && strcmp(c->name, name) == 0)
        {
            return c;
        }
        c = c->next;
    }

    if (ctx->current_impl_methods)
    {
        ASTNode *n = ctx->current_impl_methods;
        while (n)
        {
            if (n->type == NODE_FUNCTION && n->func.name[0] == name[0] &&
                strcmp(n->func.name, name) == 0)
            {
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

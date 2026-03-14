
#include "../ast/ast.h"
#include "../zprep.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper: Check if a struct depends on another struct/enum by-value.
static int struct_depends_on(ParserContext *ctx, ASTNode *s1, const char *target_name)
{
    if (!s1)
    {
        return 0;
    }

    // Check structs
    if (s1->type == NODE_STRUCT)
    {
        ASTNode *field = s1->strct.fields;
        while (field)
        {
            if (field->type == NODE_FIELD && field->field.type)
            {
                char *type_str = field->field.type;

                // Skip pointers - they don't create ordering dependency.
                if (strchr(type_str, '*'))
                {
                    field = field->next;
                    continue;
                }

                // Clean type string (remove struct/enum prefixes)
                const char *clean = type_str;
                if (strncmp(clean, "struct ", 7) == 0)
                {
                    clean += 7;
                }
                else if (strncmp(clean, "enum ", 5) == 0)
                {
                    clean += 5;
                }
                else if (strncmp(clean, "union ", 6) == 0)
                {
                    clean += 6;
                }

                if (ctx)
                {
                    const char *alias = find_type_alias(ctx, clean);
                    if (alias)
                    {
                        clean = alias;
                    }
                }

                char *mangled_clean = replace_string_type(clean);

                // Check for match
                size_t len = strlen(target_name);
                int is_match = (strncmp(mangled_clean, target_name, len) == 0);
                free(mangled_clean);

                if (is_match)
                {
                    return 1;
                }
            }
            field = field->next;
        }
    }
    // Check enums (ADTs)
    else if (s1->type == NODE_ENUM)
    {
        ASTNode *variant = s1->enm.variants;
        while (variant)
        {
            if (variant->type == NODE_ENUM_VARIANT && variant->variant.payload)
            {
                char *type_str = codegen_type_to_string(variant->variant.payload);
                if (type_str)
                {
                    if (strchr(type_str, '*'))
                    {
                        free(type_str);
                        variant = variant->next;
                        continue;
                    }

                    const char *clean = type_str;
                    if (strncmp(clean, "struct ", 7) == 0)
                    {
                        clean += 7;
                    }
                    else if (strncmp(clean, "enum ", 5) == 0)
                    {
                        clean += 5;
                    }
                    else if (strncmp(clean, "union ", 6) == 0)
                    {
                        clean += 6;
                    }

                    if (ctx)
                    {
                        const char *alias = find_type_alias(ctx, clean);
                        if (alias)
                        {
                            clean = alias;
                        }
                    }
                    char *mangled_clean = replace_string_type(clean);

                    // Check for match
                    size_t len = strlen(target_name);
                    int is_match = (strncmp(mangled_clean, target_name, len) == 0);
                    free(mangled_clean);

                    if (is_match)
                    {
                        char next = clean[len];
                        if (next == 0 || next == '[' || isspace(next))
                        {
                            free(type_str);
                            return 1;
                        }
                    }
                    free(type_str);
                }
            }
            variant = variant->next;
        }
    }

    return 0;
}

// Topologically sort a list of struct/enum nodes.
static ASTNode *topo_sort_structs(ParserContext *ctx, ASTNode *head)
{
    if (!head)
    {
        return NULL;
    }

    // Count all nodes (structs + enums + traits).
    int count = 0;
    ASTNode *n = head;
    while (n)
    {
        if (n->type == NODE_STRUCT || n->type == NODE_ENUM || n->type == NODE_TRAIT)
        {
            count++;
        }
        n = n->next;
    }
    if (count == 0)
    {
        return head;
    }

    // Build array of all nodes.
    ASTNode **nodes = malloc(count * sizeof(ASTNode *));
    int *emitted = calloc(count, sizeof(int));
    n = head;
    int idx = 0;
    while (n)
    {
        if (n->type == NODE_STRUCT || n->type == NODE_ENUM || n->type == NODE_TRAIT)
        {
            nodes[idx++] = n;
        }
        n = n->next;
    }

    // Build order array (indices in emission order).
    int *order = malloc(count * sizeof(int));
    int order_idx = 0;

    int changed = 1;
    int max_iterations = count * count;
    int iterations = 0;

    while (changed && iterations < max_iterations)
    {
        changed = 0;
        iterations++;

        for (int i = 0; i < count; i++)
        {
            if (emitted[i])
            {
                continue;
            }

            // Traits have no dependencies, emit first.
            if (nodes[i]->type == NODE_TRAIT)
            {
                order[order_idx++] = i;
                emitted[i] = 1;
                changed = 1;
                continue;
            }

            // For structs/enums, check if all dependencies are emitted.
            int can_emit = 1;
            for (int j = 0; j < count; j++)
            {
                if (i == j || emitted[j])
                {
                    continue;
                }

                // Get the name of the potential dependency.
                const char *dep_name = NULL;
                if (nodes[j]->type == NODE_STRUCT)
                {
                    dep_name = nodes[j]->strct.name;
                }
                else if (nodes[j]->type == NODE_ENUM)
                {
                    dep_name = nodes[j]->enm.name;
                }

                if (dep_name && struct_depends_on(ctx, nodes[i], dep_name))
                {
                    can_emit = 0;
                    break;
                }
            }

            if (can_emit)
            {
                order[order_idx++] = i;
                emitted[i] = 1;
                changed = 1;
            }
        }
    }

    // Add any remaining nodes (cycles).
    for (int i = 0; i < count; i++)
    {
        if (!emitted[i])
        {
            order[order_idx++] = i;
        }
    }

    // Now build the linked list in the correct order.
    ASTNode *result = NULL;
    ASTNode *result_tail = NULL;

    for (int i = 0; i < order_idx; i++)
    {
        ASTNode *node = nodes[order[i]];
        if (!result)
        {
            result = node;
            result_tail = node;
        }
        else
        {
            result_tail->next = node;
            result_tail = node;
        }
    }
    if (result_tail)
    {
        result_tail->next = NULL;
    }

    free(nodes);
    free(emitted);
    free(order);
    return result;
}

// Helper structure for tracking emitted content to prevent duplicates
typedef struct EmittedContent
{
    char *content;
    struct EmittedContent *next;
} EmittedContent;

// Check if content has already been emitted
static int is_content_emitted(EmittedContent *list, const char *content)
{
    while (list)
    {
        if (strcmp(list->content, content) == 0)
        {
            return 1;
        }
        list = list->next;
    }
    return 0;
}

// Mark content as emitted
static void mark_content_emitted(EmittedContent **list, const char *content)
{
    EmittedContent *node = xmalloc(sizeof(EmittedContent));
    node->content = xstrdup(content);
    node->next = *list;
    *list = node;
}

// Free emitted content list
static void free_emitted_list(EmittedContent *list)
{
    while (list)
    {
        EmittedContent *next = list->next;
        free(list->content);
        free(list);
        list = next;
    }
}

static void emit_auto_drop_glues(ParserContext *ctx, ASTNode *structs, FILE *out)
{
    ASTNode *s = structs;
    while (s)
    {
        if (s->type == NODE_STRUCT && s->type_info && s->type_info->traits.has_drop &&
            !s->strct.is_template)
        {
            if (s->cfg_condition)
            {
                fprintf(out, "#if %s\n", s->cfg_condition);
            }

            char *sname = s->strct.name;
            fprintf(out, "// Auto-Generated RAII Glue for %s\n", sname);
            fprintf(out, "void %s__Drop_glue(%s *self) {\n", sname, sname);

            int has_manual_drop = check_impl(ctx, "Drop", sname);
            if (has_manual_drop)
            {
                fprintf(out, "    %s__Drop_drop(self);\n", sname);
            }

            ASTNode *field = s->strct.fields;
            while (field)
            {
                Type *ft = field->type_info;
                if (ft && ft->kind == TYPE_STRUCT && ft->name)
                {
                    ASTNode *fdef = find_struct_def_codegen(ctx, ft->name);
                    if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                    {
                        fprintf(out, "    %s__Drop_glue(&self->%s);\n", ft->name,
                                field->field.name);
                    }
                }
                field = field->next;
            }

            fprintf(out, "}\n");
            if (s->cfg_condition)
            {
                fprintf(out, "#endif\n");
            }
            fprintf(out, "\n");
        }
        s = s->next;
    }
}

// Main entry point for code generation.
void codegen_node(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (node->type == NODE_ROOT)
    {
        ASTNode *kids = node->root.children;
        while (kids && kids->type == NODE_ROOT)
        {
            kids = kids->root.children;
        }

        g_current_func_ret_type = NULL;
        g_current_lambda = NULL;
        global_user_structs = kids;

        if (!ctx->skip_preamble)
        {
            emit_preamble(ctx, out);
            fflush(out);
        }

        for (int i = 0; i < g_config.cfg_define_count; i++)
        {
            fprintf(out, "#ifndef ZC_CFG_%s\n#define ZC_CFG_%s 1\n#endif\n",
                    g_config.cfg_defines[i], g_config.cfg_defines[i]);
        }

        emit_includes_and_aliases(kids, out);
        if (g_config.use_cpp)
        {
            fprintf(out, "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n");
        }

        if (ctx->hoist_out)
        {
            long pos = ftell(ctx->hoist_out);
            rewind(ctx->hoist_out);
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), ctx->hoist_out)) > 0)
            {
                fwrite(buf, 1, n, out);
            }
            fseek(ctx->hoist_out, pos, SEEK_SET);
        }

        ASTNode *merged = NULL;
        ASTNode *merged_tail = NULL;

        ASTNode *s = ctx->instantiated_structs;
        while (s)
        {
            ASTNode *copy = xmalloc(sizeof(ASTNode));
            *copy = *s;
            copy->next = NULL;
            if (!merged)
            {
                merged = copy;
                merged_tail = copy;
            }
            else
            {
                merged_tail->next = copy;
                merged_tail = copy;
            }
            s = s->next;
        }

        StructRef *sr = ctx->parsed_structs_list;
        while (sr)
        {
            if (sr->node)
            {
                ASTNode *copy = xmalloc(sizeof(ASTNode));
                *copy = *sr->node;
                copy->next = NULL;
                if (!merged)
                {
                    merged = copy;
                    merged_tail = copy;
                }
                else
                {
                    merged_tail->next = copy;
                    merged_tail = copy;
                }
            }
            sr = sr->next;
        }

        StructRef *er = ctx->parsed_enums_list;
        while (er)
        {
            if (er->node)
            {
                ASTNode *copy = xmalloc(sizeof(ASTNode));
                *copy = *er->node;
                copy->next = NULL;
                if (!merged)
                {
                    merged = copy;
                    merged_tail = copy;
                }
                else
                {
                    merged_tail->next = copy;
                    merged_tail = copy;
                }
            }
            er = er->next;
        }

        ASTNode *k = kids;
        while (k)
        {
            if (k->type == NODE_STRUCT || k->type == NODE_ENUM)
            {
                int found = 0;
                ASTNode *chk = merged;
                while (chk)
                {
                    if (chk->type == k->type)
                    {
                        const char *n1 = (k->type == NODE_STRUCT) ? k->strct.name : k->enm.name;
                        const char *n2 =
                            (chk->type == NODE_STRUCT) ? chk->strct.name : chk->enm.name;
                        if (n1 && n2 && strcmp(n1, n2) == 0)
                        {
                            found = 1;
                            break;
                        }
                    }
                    chk = chk->next;
                }

                if (!found)
                {
                    ASTNode *copy = xmalloc(sizeof(ASTNode));
                    *copy = *k;
                    copy->next = NULL;
                    if (!merged)
                    {
                        merged = copy;
                        merged_tail = copy;
                    }
                    else
                    {
                        merged_tail->next = copy;
                        merged_tail = copy;
                    }
                }
            }
            k = k->next;
        }

        // Topologically sort.
        ASTNode *sorted = topo_sort_structs(ctx, merged);

        print_type_defs(ctx, out, sorted);
        if (!g_config.use_cpp)
        {
            emit_enum_protos(sorted, out);
        }
        emit_global_aliases(ctx, out);
        emit_type_aliases(kids, out);
        emit_trait_defs(kids, out);

        StructRef *trait_ref = ctx->parsed_globals_list;
        while (trait_ref)
        {
            if (trait_ref->node && trait_ref->node->type == NODE_TRAIT)
            {
                // Check if this trait was already in kids (explicitly imported)
                int already_in_kids = 0;
                ASTNode *k_inner = kids;
                while (k_inner)
                {
                    if (k_inner->type == NODE_TRAIT && k_inner->trait.name &&
                        trait_ref->node->trait.name &&
                        strcmp(k_inner->trait.name, trait_ref->node->trait.name) == 0)
                    {
                        already_in_kids = 1;
                        break;
                    }
                    k_inner = k_inner->next;
                }

                if (!already_in_kids)
                {
                    // Create a temporary single-node list for emit_trait_defs
                    ASTNode *saved_next = trait_ref->node->next;
                    trait_ref->node->next = NULL;
                    emit_trait_defs(trait_ref->node, out);
                    trait_ref->node->next = saved_next;
                }
            }
            trait_ref = trait_ref->next;
        }

        // Track emitted raw statements to prevent duplicates
        EmittedContent *emitted_raw = NULL;

        // First pass: emit ONLY preprocessor directives before struct defs
        ASTNode *raw_iter = kids;
        while (raw_iter)
        {
            if (raw_iter->type == NODE_RAW_STMT && raw_iter->raw_stmt.content)
            {
                const char *content = raw_iter->raw_stmt.content;
                // Skip leading whitespace
                while (*content == ' ' || *content == '\t' || *content == '\n')
                {
                    content++;
                }
                // Emit only if it's a preprocessor directive and not already emitted
                if (*content == '#')
                {
                    if (!is_content_emitted(emitted_raw, raw_iter->raw_stmt.content))
                    {
                        fprintf(out, "%s\n", raw_iter->raw_stmt.content);
                        mark_content_emitted(&emitted_raw, raw_iter->raw_stmt.content);
                    }
                }
            }
            raw_iter = raw_iter->next;
        }

        if (sorted)
        {
            emit_struct_defs(ctx, sorted, out);
        }

        // Second pass: emit non-preprocessor raw statements after struct defs
        raw_iter = kids;
        while (raw_iter)
        {
            if (raw_iter->type == NODE_RAW_STMT && raw_iter->raw_stmt.content)
            {
                const char *content = raw_iter->raw_stmt.content;
                while (*content == ' ' || *content == '\t' || *content == '\n')
                {
                    content++;
                }
                if (*content != '#')
                {
                    if (!is_content_emitted(emitted_raw, raw_iter->raw_stmt.content))
                    {
                        fprintf(out, "%s\n", raw_iter->raw_stmt.content);
                        mark_content_emitted(&emitted_raw, raw_iter->raw_stmt.content);
                    }
                }
            }
            raw_iter = raw_iter->next;
        }

        // Emit type aliases was here (moved up)

        ASTNode *merged_globals = NULL; // Head

        if (ctx->parsed_globals_list)
        {
            StructRef *struct_ref = ctx->parsed_globals_list;
            while (struct_ref)
            {
                // Check if this global is already in the merged list (by name)
                int is_duplicate = 0;
                if (struct_ref->node && (struct_ref->node->type == NODE_VAR_DECL ||
                                         struct_ref->node->type == NODE_CONST))
                {
                    const char *var_name = struct_ref->node->var_decl.name;
                    ASTNode *check = merged_globals;
                    while (check)
                    {
                        if ((check->type == NODE_VAR_DECL || check->type == NODE_CONST) &&
                            check->var_decl.name && strcmp(check->var_decl.name, var_name) == 0)
                        {
                            is_duplicate = 1;
                            break;
                        }
                        check = check->next;
                    }
                }

                if (!is_duplicate)
                {
                    ASTNode *copy = xmalloc(sizeof(ASTNode));
                    *copy = *struct_ref->node;
                    copy->next = merged_globals;
                    merged_globals = copy;
                }

                struct_ref = struct_ref->next;
            }
        }

        emit_globals(ctx, merged_globals, out);

        ASTNode *merged_funcs = NULL;
        ASTNode *merged_funcs_tail = NULL;

        if (ctx->instantiated_funcs)
        {
            ASTNode *fn_node = ctx->instantiated_funcs;
            while (fn_node)
            {
                ASTNode *copy = xmalloc(sizeof(ASTNode));
                *copy = *fn_node;
                copy->next = NULL;
                if (!merged_funcs)
                {
                    merged_funcs = copy;
                    merged_funcs_tail = copy;
                }
                else
                {
                    merged_funcs_tail->next = copy;
                    merged_funcs_tail = copy;
                }
                fn_node = fn_node->next;
            }
        }

        if (ctx->parsed_funcs_list)
        {
            StructRef *fn_ref = ctx->parsed_funcs_list;
            while (fn_ref)
            {
                ASTNode *copy = xmalloc(sizeof(ASTNode));
                *copy = *fn_ref->node;
                copy->next = NULL;
                if (!merged_funcs)
                {
                    merged_funcs = copy;
                    merged_funcs_tail = copy;
                }
                else
                {
                    merged_funcs_tail->next = copy;
                    merged_funcs_tail = copy;
                }
                fn_ref = fn_ref->next;
            }
        }

        if (ctx->parsed_impls_list)
        {
            StructRef *impl_ref = ctx->parsed_impls_list;
            while (impl_ref)
            {
                ASTNode *copy = xmalloc(sizeof(ASTNode));
                *copy = *impl_ref->node;
                copy->next = NULL;
                if (!merged_funcs)
                {
                    merged_funcs = copy;
                    merged_funcs_tail = copy;
                }
                else
                {
                    merged_funcs_tail->next = copy;
                    merged_funcs_tail = copy;
                }
                impl_ref = impl_ref->next;
            }
        }

        emit_protos(ctx, merged_funcs, out);

        emit_impl_vtables(ctx, out);
        emit_auto_drop_glues(ctx, sorted, out);

        emit_lambda_defs(ctx, out);

        int test_count = emit_tests_and_runner(ctx, kids, out);

        ASTNode *iter = merged_funcs;
        while (iter)
        {
            if (iter->type == NODE_IMPL)
            {
                char *sname = iter->impl.struct_name;
                if (!sname)
                {
                    iter = iter->next;
                    continue;
                }

                // Resolve opaque alias
                const char *resolved = find_type_alias(ctx, sname);

                char *mangled = replace_string_type(sname);
                ASTNode *def = find_struct_def_codegen(ctx, mangled);
                if (!def && resolved)
                {
                    free(mangled);
                    mangled = replace_string_type(resolved);
                    def = find_struct_def_codegen(ctx, mangled);
                }
                int skip = 0;
                if (def)
                {
                    if (def->type == NODE_STRUCT && def->strct.is_template)
                    {
                        skip = 1;
                    }
                    else if (def->type == NODE_ENUM && def->enm.is_template)
                    {
                        skip = 1;
                    }
                }
                else
                {
                    char *buf = strip_template_suffix(sname);
                    if (buf)
                    {
                        def = find_struct_def_codegen(ctx, buf);
                        if (def && def->strct.is_template)
                        {
                            skip = 1;
                        }
                        free(buf);
                    }
                }
                if (mangled)
                {
                    free(mangled);
                }
                if (skip)
                {
                    iter = iter->next;
                    continue;
                }
            }
            if (iter->type == NODE_IMPL_TRAIT)
            {
                char *sname = iter->impl_trait.target_type;
                if (!sname)
                {
                    iter = iter->next;
                    continue;
                }

                char *mangled = replace_string_type(sname);
                ASTNode *def = find_struct_def_codegen(ctx, mangled);
                int skip = 0;
                if (def)
                {
                    if (def->strct.is_template)
                    {
                        skip = 1;
                    }
                }
                else
                {
                    char *buf = strip_template_suffix(sname);
                    if (buf)
                    {
                        def = find_struct_def_codegen(ctx, buf);
                        if (def && def->strct.is_template)
                        {
                            skip = 1;
                        }
                        free(buf);
                    }
                }
                if (mangled)
                {
                    free(mangled);
                }
                if (skip)
                {
                    iter = iter->next;
                    continue;
                }
            }
            if (iter->cfg_condition)
            {
                fprintf(out, "#if %s\n", iter->cfg_condition);
            }
            codegen_node_single(ctx, iter, out);
            if (iter->cfg_condition)
            {
                fprintf(out, "#endif\n");
            }
            iter = iter->next;
        }

        int has_user_main = 0;
        ASTNode *chk = merged_funcs;
        while (chk)
        {
            if (chk->type == NODE_FUNCTION && strcmp(chk->func.name, "main") == 0)
            {
                has_user_main = 1;
                break;
            }
            chk = chk->next;
        }

        if (!has_user_main && test_count > 0)
        {
            fprintf(out, "\nint main() { _z_run_tests(); return 0; }\n");
        }

        if (g_config.use_cpp)
        {
            fprintf(out, "\n#ifdef __cplusplus\n}\n#endif\n");
        }

        // Clean up emitted content tracking list
        free_emitted_list(emitted_raw);
    }
}

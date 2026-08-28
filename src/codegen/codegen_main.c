// SPDX-License-Identifier: MIT
#include "../parser/parser.h"

#include "../ast/ast.h"
#include "../constants.h"
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
    if (s1->kind == NODE_STRUCT)
    {
        ASTNode *field = s1->strct.fields;
        while (field)
        {
            if (field->kind == NODE_FIELD && field->field.type)
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
                int is_match = (strncmp(mangled_clean, target_name, (size_t)(len)) == 0);
                zfree(mangled_clean);

                if (is_match)
                {
                    return 1;
                }
            }
            field = field->next;
        }
    }
    // Check enums (ADTs)
    else if (s1->kind == NODE_ENUM)
    {
        ASTNode *variant = s1->enm.variants;
        while (variant)
        {
            if (variant->kind == NODE_ENUM_VARIANT && variant->variant.payload)
            {
                char *type_str = type_to_c_string(variant->variant.payload);
                if (type_str)
                {
                    if (strchr(type_str, '*'))
                    {
                        zfree(type_str);
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
                    int is_match = (strncmp(mangled_clean, target_name, (size_t)(len)) == 0);
                    zfree(mangled_clean);

                    if (is_match)
                    {
                        char next = clean[len];
                        if (next == 0 || next == '[' || isspace(next))
                        {
                            zfree(type_str);
                            return 1;
                        }
                    }
                    zfree(type_str);
                }
            }
            variant = variant->next;
        }
    }

    return 0;
}

typedef struct VisitedModules
{
    const char *path;
    struct VisitedModules *next;
} VisitedModules;

static int is_module_visited(VisitedModules *visited, const char *path)
{
    while (visited)
    {
        if (strcmp(visited->path, path) == 0)
        {
            return 1;
        }
        visited = visited->next;
    }
    return 0;
}

static void mark_module_visited(VisitedModules **visited, const char *path)
{
    VisitedModules *node = xmalloc(sizeof(VisitedModules));
    node->path = path; // path is from import_stmt which persists
    node->next = *visited;
    *visited = node;
}

static void free_visited_modules(VisitedModules *visited)
{
    // NO-OP: We use arena allocation (xmalloc) for VisitedModules.
    // Arena is reset globally, single nodes must not be zfree()d.
    (void)visited;
}

static int count_sortable_nodes_internal(ASTNode *head, VisitedModules **visited, int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in count_sortable_nodes (circular imports?)");
    }
    int count = 0;
    ASTNode *n = head;
    while (n)
    {
        if (n->kind == NODE_STRUCT || n->kind == NODE_ENUM || n->kind == NODE_TRAIT)
        {
            count++;
        }
        else if (n->kind == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, n->import_stmt.path))
            {
                mark_module_visited(visited, n->import_stmt.path);
                count +=
                    count_sortable_nodes_internal(n->import_stmt.module_root, visited, depth + 1);
            }
        }
        else if (n->kind == NODE_ROOT)
        {
            // Avoid same-root recursion if children == head
            if (n->root.children != head)
            {
                count += count_sortable_nodes_internal(n->root.children, visited, depth + 1);
            }
        }
        n = n->next;
    }
    return count;
}

static int count_sortable_nodes(ASTNode *head)
{
    VisitedModules *visited = NULL;
    int count = count_sortable_nodes_internal(head, &visited, 0);
    free_visited_modules(visited);
    return count;
}

static void collect_sortable_nodes_internal(ASTNode *head, ASTNode **nodes, int *idx,
                                            VisitedModules **visited, int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in collect_sortable_nodes (circular imports?)");
    }
    ASTNode *n = head;
    while (n)
    {
        if (n->kind == NODE_STRUCT || n->kind == NODE_ENUM || n->kind == NODE_TRAIT)
        {
            nodes[(*idx)++] = n;
        }
        else if (n->kind == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, n->import_stmt.path))
            {
                mark_module_visited(visited, n->import_stmt.path);
                collect_sortable_nodes_internal(n->import_stmt.module_root, nodes, idx, visited,
                                                depth + 1);
            }
        }
        else if (n->kind == NODE_ROOT)
        {
            // Avoid same-root recursion if children == head
            if (n->root.children != head)
            {
                collect_sortable_nodes_internal(n->root.children, nodes, idx, visited, depth + 1);
            }
        }
        n = n->next;
    }
}

static void collect_sortable_nodes(ASTNode *head, ASTNode **nodes, int *idx)
{
    VisitedModules *visited = NULL;
    collect_sortable_nodes_internal(head, nodes, idx, &visited, 0);
    free_visited_modules(visited);
}

// Topologically sort a list of struct/enum nodes.
static ASTNode *topo_sort_structs(ParserContext *ctx, ASTNode *head)
{
    if (!head)
    {
        return NULL;
    }

    // Count all nodes (structs + enums + traits).
    int count = count_sortable_nodes(head);
    if (count == 0)
    {
        return head;
    }

    // Build array of all nodes.
    ASTNode **nodes = malloc((size_t)count * sizeof(ASTNode *));
    int *emitted = calloc((size_t)count, sizeof(int));
    int idx = 0;
    collect_sortable_nodes(head, nodes, &idx);

    // Build order array (indices in emission order).
    int *order = malloc((size_t)count * sizeof(int));
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
            if (nodes[i]->kind == NODE_TRAIT)
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
                if (nodes[j]->kind == NODE_STRUCT)
                {
                    dep_name = nodes[j]->strct.name;
                }
                else if (nodes[j]->kind == NODE_ENUM)
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

    zfree(nodes);
    zfree(emitted);
    zfree(order);
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
        zfree(list->content);
        zfree(list);
        list = next;
    }
}

static void emit_raw_statements_internal(ParserContext *ctx, ASTNode *node,
                                         VisitedModules **visited, int depth, int preproc_only,
                                         EmittedContent **emitted_raw)
{
    if (depth > 1024)
    {
        zfatal(
            "Infinite recursion detected in emit_raw_statements_internal (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->kind == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_raw_statements_internal(ctx, node->import_stmt.module_root, visited, depth + 1,
                                             preproc_only, emitted_raw);
            }
            node = node->next;
            continue;
        }
        else if (node->kind == NODE_ROOT)
        {
            emit_raw_statements_internal(ctx, node->root.children, visited, depth + 1, preproc_only,
                                         emitted_raw);
            node = node->next;
            continue;
        }

        if ((node->kind == NODE_RAW_STMT || node->kind == NODE_PREPROC_DIRECTIVE) &&
            node->raw_stmt.content)
        {
            const char *content = node->raw_stmt.content;
            while (*content == ' ' || *content == '\t' || *content == '\n')
            {
                content++;
            }

            int is_preproc = (*content == '#');

            if ((preproc_only && is_preproc) || (!preproc_only && !is_preproc))
            {
                if (!is_content_emitted(*emitted_raw, node->raw_stmt.content))
                {
                    EMIT(ctx, "%s\n", node->raw_stmt.content);
                    mark_content_emitted(emitted_raw, node->raw_stmt.content);
                }
            }
        }
        node = node->next;
    }
}

static void emit_auto_drop_glues(ParserContext *ctx, ASTNode *structs)
{
    ASTNode *s = structs;
    while (s)
    {
        if (s->kind == NODE_STRUCT && s->type_info && s->type_info->traits.has_drop &&
            !s->strct.is_template)
        {
            if (s->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", s->cfg_condition);
            }

            char *sname = s->strct.name;
            EMIT(ctx, "// Auto-Generated RAII Glue for %s\n", sname);
            EMIT(ctx, "void %s__Drop__glue(%s *self) {\n", sname, sname);

            char glue_mangled[MAX_MANGLED_NAME_LEN];
            snprintf(glue_mangled, sizeof(glue_mangled), "%s__Drop__drop", sname);
            if (find_func(ctx, glue_mangled))
            {
                EMIT(ctx, "    %s__Drop__drop(self);\n", sname);
            }

            ASTNode *field = s->strct.fields;
            while (field)
            {
                Type *ft = field->type_info;
                if (ft && ft->kind == TYPE_STRUCT && ft->name)
                {
                    ASTNode *fdef = find_struct_def(ctx, ft->name);
                    if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                    {
                        EMIT(ctx, "    %s__Drop__glue(&self->%s);\n", ft->name, field->field.name);
                    }
                }
                field = field->next;
            }
            EMIT(ctx, "}\n\n");
            if (s->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
            EMIT(ctx, "\n");
        }
        s = s->next;
    }
}

static void emit_generic_drop_macro(ParserContext *ctx, ASTNode *structs)
{
    (void)ctx;
    if (ctx->config->use_cpp && !ctx->config->use_cuda)
    {
        EMIT(ctx, "// Global Generic Drop Dispatch (C++ Overloads)\n");
        EMIT(ctx, "#ifdef __cplusplus\n");
        EMIT(ctx, "} // end extern \"C\"\n");
        EMIT(ctx, "template<typename T> inline void _z_drop(T& x) { (void)x; }\n");
        EMIT(ctx, "template<typename T> inline void _z_drop(T* x) { (void)x; }\n");

        ASTNode *s = structs;
        while (s)
        {
            if (s->kind == NODE_STRUCT && s->type_info && s->type_info->traits.has_drop &&
                !s->strct.is_template)
            {
                if (s->cfg_condition)
                {
                    EMIT(ctx, "#if %s\n", s->cfg_condition);
                }
                char *sname = s->strct.name;
                EMIT(ctx, "inline void _z_drop(%s& x) { %s__Drop__glue(&x); }\n", sname, sname);
                EMIT(ctx, "inline void _z_drop(%s* x) { if(x) %s__Drop__glue(x); }\n", sname,
                     sname);
                if (s->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
            }
            s = s->next;
        }
        EMIT(ctx, "extern \"C\" {\n");
        EMIT(ctx, "#endif\n\n");
    }
    else
    {
        EMIT(ctx, "// Global Generic Drop Dispatch\n");
        EMIT(ctx, "#define _z_drop(x) _Generic((x)");

        ASTNode *s = structs;
        while (s)
        {
            if (s->kind == NODE_STRUCT && s->type_info && s->type_info->traits.has_drop &&
                !s->strct.is_template)
            {
                char *sname = s->strct.name;
                EMIT(ctx, ", \\\n    %s: %s__Drop__glue((void*)&(x))", sname, sname);
            }
            s = s->next;
        }

        EMIT(ctx, ", \\\n    default: (void)0)\n\n");
    }
}

// Walk the AST and replace NODE_COMPTIME nodes with their generated children.
// This must run before codegen so that emitted declarations (structs, functions)
// appear in the root children list.
static void flatten_comptime_nodes(ASTNode *parent)
{
    if (!parent)
    {
        return;
    }

    // For blocks: walk block.statements
    if (parent->kind == NODE_BLOCK)
    {
        ASTNode **pp = &parent->block.statements;
        while (pp && *pp)
        {
            if ((*pp)->kind == NODE_COMPTIME && (*pp)->comptime.generated)
            {
                ASTNode *gen = (*pp)->comptime.generated;
                ASTNode *gen_last = gen;
                while (gen_last && gen_last->next)
                {
                    gen_last = gen_last->next;
                }
                if ((*pp)->next)
                {
                    if (gen_last)
                    {
                        gen_last->next = (*pp)->next;
                    }
                }
                *pp = gen;
                if (!*pp)
                {
                    break;
                }
            }
            else
            {
                flatten_comptime_nodes(*pp);
                if (*pp)
                {
                    pp = &(*pp)->next;
                }
                else
                {
                    break;
                }
            }
        }
        return;
    }

    // For root: walk root.children
    if (parent->kind == NODE_ROOT)
    {
        ASTNode **pp = &parent->root.children;
        while (pp && *pp)
        {
            if ((*pp)->kind == NODE_COMPTIME && (*pp)->comptime.generated)
            {
                ASTNode *gen = (*pp)->comptime.generated;
                ASTNode *gen_last = gen;
                while (gen_last && gen_last->next)
                {
                    gen_last = gen_last->next;
                }
                if ((*pp)->next)
                {
                    if (gen_last)
                    {
                        gen_last->next = (*pp)->next;
                    }
                }
                *pp = gen;
                if (!*pp)
                {
                    break;
                }
            }
            else
            {
                flatten_comptime_nodes(*pp);
                if (*pp)
                {
                    pp = &(*pp)->next;
                }
                else
                {
                    break;
                }
            }
        }
        return;
    }

    // For if/while/for/etc: recurse into sub-blocks
    if (parent->kind == NODE_IF)
    {
        if (parent->if_stmt.then_body)
        {
            flatten_comptime_nodes(parent->if_stmt.then_body);
        }
        if (parent->if_stmt.else_body)
        {
            flatten_comptime_nodes(parent->if_stmt.else_body);
        }
    }
    if (parent->kind == NODE_WHILE && parent->while_stmt.body)
    {
        flatten_comptime_nodes(parent->while_stmt.body);
    }
    if (parent->kind == NODE_FOR && parent->for_stmt.body)
    {
        flatten_comptime_nodes(parent->for_stmt.body);
    }
    if (parent->kind == NODE_FOR_RANGE && parent->for_range.body)
    {
        flatten_comptime_nodes(parent->for_range.body);
    }
    if (parent->kind == NODE_LOOP && parent->loop_stmt.body)
    {
        flatten_comptime_nodes(parent->loop_stmt.body);
    }
    if (parent->kind == NODE_REPEAT && parent->repeat_stmt.body)
    {
        flatten_comptime_nodes(parent->repeat_stmt.body);
    }
    if (parent->kind == NODE_MATCH)
    {
        ASTNode *c = parent->match_stmt.cases;
        while (c)
        {
            if (c->kind == NODE_MATCH_CASE && c->match_case.body)
            {
                flatten_comptime_nodes(c->match_case.body);
            }
            c = c->next;
        }
    }
    if (parent->kind == NODE_FUNCTION && parent->func.body)
    {
        flatten_comptime_nodes(parent->func.body);
    }
    if (parent->kind == NODE_TEST && parent->test_stmt.body)
    {
        flatten_comptime_nodes(parent->test_stmt.body);
    }
    if (parent->kind == NODE_DEFER && parent->defer_stmt.stmt)
    {
        flatten_comptime_nodes(parent->defer_stmt.stmt);
    }
    if (parent->kind == NODE_GUARD && parent->guard_stmt.body)
    {
        flatten_comptime_nodes(parent->guard_stmt.body);
    }
    if (parent->kind == NODE_UNLESS && parent->unless_stmt.body)
    {
        flatten_comptime_nodes(parent->unless_stmt.body);
    }
    if (parent->kind == NODE_DO_WHILE && parent->do_while_stmt.body)
    {
        flatten_comptime_nodes(parent->do_while_stmt.body);
    }

    // For lambda/impl bodies
    if (parent->kind == NODE_LAMBDA && parent->lambda.body)
    {
        flatten_comptime_nodes(parent->lambda.body);
    }
    if (parent->kind == NODE_IMPL)
    {
        ASTNode *m = parent->impl.methods;
        while (m)
        {
            if (m->kind == NODE_FUNCTION && m->func.body)
            {
                flatten_comptime_nodes(m->func.body);
            }
            m = m->next;
        }
    }

    // For function declaration bodies
    if (parent->kind == NODE_FUNCTION && parent->func.body)
    {
        flatten_comptime_nodes(parent->func.body);
    }
}

// Main entry point for code generation.
void codegen_c_program(ParserContext *ctx, ASTNode *node)
{
    // Flatten any NODE_COMPTIME blocks into their generated AST nodes
    if (node)
    {
        flatten_comptime_nodes(node);
    }

    if (!node)
    {
        return;
    }
    if (node->kind == NODE_ROOT)
    {
        ctx->current_scope = ctx->global_scope;
        ASTNode *kids = node->root.children;
        while (kids && kids->kind == NODE_ROOT)
        {
            kids = kids->root.children;
        }

        ctx->cg.current_func_ret_type = NULL;
        ctx->cg.current_lambda = NULL;
        ctx->cg.global_user_structs = kids;
        VisitedModules *visited = NULL;

        if (!ctx->cg.skip_preamble)
        {
            emit_preamble(ctx);
        }

        for (size_t i = 0; i < ctx->config->cfg_defines.length; i++)
        {
            EMIT(ctx, "#ifndef ZC_CFG_%s\n#define ZC_CFG_%s 1\n#endif\n",
                 ctx->config->cfg_defines.data[i], ctx->config->cfg_defines.data[i]);
        }

        emit_includes_and_aliases(ctx, kids, &visited);
        if (ctx->config->use_cpp && !ctx->config->use_cuda && !ctx->config->use_objc)
        {
            EMIT(ctx, "\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n");
        }

        if (ctx->cg.hoist_out)
        {
            long pos = ftell(ctx->cg.hoist_out);
            if (pos < 0)
            {
                pos = 0;
            }
            rewind(ctx->cg.hoist_out);
            char buf[4096];
            size_t n;
            while (ctx->cg.hoist_out && (n = fread(buf, 1, sizeof(buf), ctx->cg.hoist_out)) > 0)
            {
                emitter_write(&(ctx)->cg.emitter, buf, 1 * n);
            }
            fseek(ctx->cg.hoist_out, pos, SEEK_SET);
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
            if (k->kind == NODE_STRUCT || k->kind == NODE_ENUM)
            {
                int found = 0;
                ASTNode *chk = merged;
                while (chk)
                {
                    if (chk->kind == k->kind)
                    {
                        const char *n1 = (k->kind == NODE_STRUCT) ? k->strct.name : k->enm.name;
                        const char *n2 =
                            (chk->kind == NODE_STRUCT) ? chk->strct.name : chk->enm.name;
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

        print_type_defs(ctx, sorted);
        if (!ctx->config->use_cpp)
        {
            emit_enum_protos(ctx, sorted);
        }
        emit_global_aliases(ctx);

        visited = NULL;
        emit_type_aliases(ctx, kids, &visited);

        visited = NULL;
        emit_trait_defs(ctx, kids, &visited);

        // Track emitted raw statements to prevent duplicates
        EmittedContent *emitted_raw = NULL;

        // First pass: emit ONLY preprocessor directives before struct defs
        VisitedModules *raw_visited_1 = NULL;
        emit_raw_statements_internal(ctx, kids, &raw_visited_1, 0, 1, &emitted_raw);

        if (sorted)
        {
            emit_struct_defs(ctx, sorted, &visited);
        }

        // Second pass: emit non-preprocessor raw statements after struct defs
        VisitedModules *raw_visited_2 = NULL;
        emit_raw_statements_internal(ctx, kids, &raw_visited_2, 0, 0, &emitted_raw);

        // Emit type aliases was here (moved up)

        ASTNode *merged_globals = NULL; // Head

        if (ctx->parsed_globals_list)
        {
            StructRef *struct_ref = ctx->parsed_globals_list;
            while (struct_ref)
            {
                // Check if this global is already in the merged list (by name)
                int is_duplicate = 0;
                if (struct_ref->node && (struct_ref->node->kind == NODE_VAR_DECL ||
                                         struct_ref->node->kind == NODE_CONST))
                {
                    const char *var_name = struct_ref->node->var_decl.name;
                    ASTNode *check = merged_globals;
                    while (check)
                    {
                        if ((check->kind == NODE_VAR_DECL || check->kind == NODE_CONST) &&
                            check->var_decl.name && strcmp(check->var_decl.name, var_name) == 0)
                        {
                            is_duplicate = 1;
                            break;
                        }
                        check = check->next;
                    }
                }

                if (!is_duplicate && struct_ref->node)
                {
                    ASTNode *copy = xmalloc(sizeof(ASTNode));
                    *copy = *struct_ref->node;
                    copy->next = merged_globals;
                    merged_globals = copy;
                }

                struct_ref = struct_ref->next;
            }
        }

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

        visited = NULL;
        emit_trait_wrappers(ctx, kids, &visited);

        visited = NULL;
        emit_protos(ctx, merged_funcs, &visited);

        visited = NULL;
        emit_globals(ctx, merged_globals, &visited);

        emit_impl_vtables(ctx);
        emit_auto_drop_glues(ctx, sorted);
        emit_generic_drop_macro(ctx, sorted);

        emit_lambda_defs(ctx);

        int test_count = emit_tests_and_runner(ctx, kids);

        ASTNode *iter = merged_funcs;
        while (iter)
        {
            if (iter->kind == NODE_IMPL)
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
                ASTNode *def = find_struct_def(ctx, mangled);
                if (!def && resolved)
                {
                    zfree(mangled);
                    mangled = replace_string_type(resolved);
                    def = find_struct_def(ctx, mangled);
                }
                int skip = 0;
                if (def)
                {
                    if (def->kind == NODE_STRUCT && def->strct.is_template)
                    {
                        skip = 1;
                    }
                    else if (def->kind == NODE_ENUM && def->enm.is_template)
                    {
                        skip = 1;
                    }
                }
                else
                {
                    char *buf = strip_template_suffix(sname);
                    if (buf)
                    {
                        def = find_struct_def(ctx, buf);
                        if (def && def->strct.is_template)
                        {
                            skip = 1;
                        }
                        zfree(buf);
                    }
                }
                if (mangled)
                {
                    zfree(mangled);
                }
                if (skip)
                {
                    iter = iter->next;
                    continue;
                }
            }
            if (iter->kind == NODE_IMPL_TRAIT)
            {
                char *sname = iter->impl_trait.target_type;
                if (!sname)
                {
                    iter = iter->next;
                    continue;
                }

                char *mangled = replace_string_type(sname);
                ASTNode *def = find_struct_def(ctx, mangled);
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
                        def = find_struct_def(ctx, buf);
                        if (def && def->strct.is_template)
                        {
                            skip = 1;
                        }
                        zfree(buf);
                    }
                }
                if (mangled)
                {
                    zfree(mangled);
                }
                if (skip)
                {
                    iter = iter->next;
                    continue;
                }
            }
            if (iter->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", iter->cfg_condition);
            }
            codegen_node_single(ctx, iter);
            if (iter->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
            iter = iter->next;
        }

        int has_user_main = 0;
        ASTNode *chk = merged_funcs;
        while (chk)
        {
            if (chk->kind == NODE_FUNCTION && strcmp(chk->func.name, "main") == 0)
            {
                has_user_main = 1;
                break;
            }
            chk = chk->next;
        }

        if (!has_user_main && test_count > 0)
        {
            EMIT(ctx, "\nint main() { return _z_run_tests(); }\n");
        }

        if (ctx->config->use_cpp && !ctx->config->use_cuda && !ctx->config->use_objc)
        {
            EMIT(ctx, "\n#ifdef __cplusplus\n}\n#endif\n");
        }

        // Clean up emitted content tracking list
        free_emitted_list(emitted_raw);
        free_visited_modules(visited);
    }
}

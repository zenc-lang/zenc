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

int is_unmangle_primitive(const char *base);
char *unmangle_ptr_suffix(const char *s);
Type *replace_type_formal(Type *t, const char *p, const char *c, const char *os, const char *ns);
ASTNode *copy_ast_replacing(ParserContext *ctx, ASTNode *n, const char *p, const char *c,
                            const char *os, const char *ns);

// Helper function to recursively scan AST for sizeof types AND generic calls to trigger
// instantiation
static void trigger_type_instantiation(ParserContext *ctx, Type *t)
{
    if (!t)
    {
        return;
    }

    // Handle slices
    if (t->kind == TYPE_ARRAY && t->array_size == 0 && t->inner)
    {
        char *inner_str = type_to_string(t->inner);
        register_slice(ctx, inner_str);
        zfree(inner_str);
    }

    // Handle mangled types (instantiations)
    if (t->name && strchr(t->name, '_'))
    {
        char *type_copy = xstrdup(t->name);
        char *underscore = (char *)strchr(type_copy, '_');
        if (underscore)
        {
            char *concrete_arg = underscore;
            while (*concrete_arg == '_')
            {
                concrete_arg++;
            }
            *underscore = '\0';
            char *template_name = type_copy;

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
                zfree(unmangled);
            }
        }
        zfree(type_copy);
    }

    // Recursive scan
    trigger_type_instantiation(ctx, t->inner);
    if (t->args)
    {
        for (int i = 0; i < t->count; i++)
        {
            trigger_type_instantiation(ctx, t->args[i]);
        }
    }
}

static void trigger_instantiations(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    // Process type information
    if (node->type_info)
    {
        trigger_type_instantiation(ctx, node->type_info);
    }

    // Process current node
    if (node->kind == NODE_EXPR_SIZEOF && node->size_of.target_type)
    {
        const char *type_str = node->size_of.target_type;
        if (strchr(type_str, '_'))
        {
            // Remove trailing '*' or 'Ptr' if present
            char *type_copy = xstrdup(type_str);
            char *star = (char *)strchr(type_copy, '*');
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

            char *underscore = (char *)strchr(type_copy, '_');
            if (underscore)
            {
                char *concrete_arg = underscore;
                while (*concrete_arg == '_')
                {
                    concrete_arg++;
                }
                *underscore = '\0';
                char *template_name = type_copy;

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
                    zfree(unmangled);
                }
            }
            zfree(type_copy);
        }
    }
    else if (node->kind == NODE_EXPR_VAR)
    {
        const char *name = node->var_ref.name;
        if (strchr(name, '_'))
        {
            GenericFuncTemplate *t = ctx->func_templates;
            while (t)
            {
                size_t tlen = strlen(t->name);
                if (strncmp(name, t->name, (size_t)(tlen)) == 0 && name[tlen] == '_' &&
                    name[tlen + 1] == '_')
                {
                    char *template_name = t->name;
                    char *concrete_arg = (char *)name + tlen + 2;

                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    instantiate_function_template(ctx, template_name, concrete_arg, unmangled);
                    zfree(unmangled);
                    break; // Found match, stop searching
                }
                t = t->next;
            }
        }
    }
    else if (node->kind == NODE_EXPR_STRUCT_INIT && node->struct_init.struct_name)
    {
        const char *name = node->struct_init.struct_name;
        if (strchr(name, '_'))
        {
            char *type_copy = xstrdup(name);
            char *underscore = (char *)strchr(type_copy, '_');
            if (underscore)
            {
                char *concrete_arg = underscore;
                while (*concrete_arg == '_')
                {
                    concrete_arg++;
                }
                *underscore = '\0';
                char *template_name = type_copy;

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
                    zfree(unmangled);
                }
            }
            zfree(type_copy);
        }
    }

    switch (node->kind)
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
    case NODE_EXPECT:
    case NODE_ASSERT:
        trigger_instantiations(ctx, node->assert_stmt.condition);
        break;
    case NODE_DEFER:
        trigger_instantiations(ctx, node->defer_stmt.stmt);
        break;
    case NODE_UNLESS:
        trigger_instantiations(ctx, node->unless_stmt.condition);
        trigger_instantiations(ctx, node->unless_stmt.body);
        break;
    case NODE_GUARD:
        trigger_instantiations(ctx, node->guard_stmt.condition);
        trigger_instantiations(ctx, node->guard_stmt.body);
        break;
    case NODE_LOOP:
        trigger_instantiations(ctx, node->loop_stmt.body);
        break;
    case NODE_REPEAT:
        trigger_instantiations(ctx, node->repeat_stmt.body);
        break;
    case NODE_DO_WHILE:
        trigger_instantiations(ctx, node->while_stmt.condition);
        trigger_instantiations(ctx, node->while_stmt.body);
        break;
    case NODE_TERNARY:
        trigger_instantiations(ctx, node->ternary.cond);
        trigger_instantiations(ctx, node->ternary.true_expr);
        trigger_instantiations(ctx, node->ternary.false_expr);
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        trigger_instantiations(ctx, node->array_literal.elements);
        break;
    case NODE_EXPR_TUPLE_LITERAL:
        trigger_instantiations(ctx, node->tuple_literal.elements);
        break;
    case NODE_EXPR_SLICE:
        trigger_instantiations(ctx, node->slice.array);
        trigger_instantiations(ctx, node->slice.start);
        trigger_instantiations(ctx, node->slice.end);
        break;
    case NODE_DESTRUCT_VAR:
        trigger_instantiations(ctx, node->destruct.init_expr);
        trigger_instantiations(ctx, node->destruct.else_block);
        break;
    case NODE_LAMBDA:
        trigger_instantiations(ctx, node->lambda.body);
        break;
    case NODE_TRY:
        trigger_instantiations(ctx, node->try_stmt.expr);
        break;
    case NODE_CUDA_LAUNCH:
        trigger_instantiations(ctx, node->cuda_launch.call);
        trigger_instantiations(ctx, node->cuda_launch.grid);
        trigger_instantiations(ctx, node->cuda_launch.block);
        trigger_instantiations(ctx, node->cuda_launch.shared_mem);
        trigger_instantiations(ctx, node->cuda_launch.stream);
        break;
    case NODE_VA_START:
        trigger_instantiations(ctx, node->va_start_args.ap);
        trigger_instantiations(ctx, node->va_start_args.last_arg);
        break;
    case NODE_VA_END:
        trigger_instantiations(ctx, node->va_end_args.ap);
        break;
    case NODE_VA_COPY:
        trigger_instantiations(ctx, node->va_copy_args.dest);
        trigger_instantiations(ctx, node->va_copy_args.src);
        break;
    case NODE_VA_ARG:
        trigger_instantiations(ctx, node->va_arg_val.ap);
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

    char buf[MAX_ERROR_MSG_LEN];
    snprintf(buf, sizeof(buf), "%s__%s", name, clean_type);
    char *mangled = merge_underscores(buf);
    zfree(clean_type);

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
        size_t suffix_cap = strlen(tpl->generic_param) * 2 + 64;
        char *param_suffix = xmalloc((size_t)(suffix_cap));
        param_suffix[0] = 0;
        const char *p_ptr = tpl->generic_param;
        while (p_ptr && *p_ptr)
        {
            strcat(param_suffix, "__");
            const char *p_next = (char *)strchr(p_ptr, ',');
            int sub_len = p_next ? (int)(p_next - p_ptr) : (int)strlen(p_ptr);
            strncat(param_suffix, p_ptr, (size_t)(sub_len));
            if (p_next)
            {
                p_ptr = p_next + 1;
            }
            else
            {
                break;
            }
        }

        // Check if ret_type ends with param_suffix (e.g., "Triple_X_Y_Z" ends with "_X_Y_Z")
        size_t ret_len = strlen(ret);
        size_t suffix_len = strlen(param_suffix);
        if (ret_len > suffix_len && strcmp(ret + ret_len - suffix_len, param_suffix) == 0)
        {
            // Extract base struct name (e.g., "Triple" from "Triple_X_Y_Z")
            size_t base_len = (size_t)(ret_len - suffix_len);
            char *struct_base = xmalloc((size_t)(base_len + 1));
            strncpy(struct_base, ret, (size_t)(base_len));
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
                char **args = xmalloc(sizeof(char *) * (size_t)(template_param_count));
                int count = 0;
                const char *types_ptr = types_src;
                while (types_ptr && *types_ptr && count < template_param_count)
                {
                    const char *types_next = (char *)strchr(types_ptr, ',');
                    int types_len =
                        types_next ? (int)(types_next - types_ptr) : (int)strlen(types_ptr);

                    args[count] = xmalloc((size_t)(types_len + 1));
                    strncpy(args[count], types_ptr, (size_t)(types_len));
                    args[count][types_len] = 0;
                    count++;

                    if (types_next)
                    {
                        types_ptr = types_next + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                // Now instantiate the struct with these args
                Token dummy_tok = {0};
                if (count == 1)
                {
                    // Unmangle Ptr suffix if needed (e.g., intPtr -> int*)
                    char *unmangled = xstrdup(args[0]);
                    size_t alen = strlen(args[0]);
                    if (alen > 3 && strcmp(args[0] + alen - 3, "Ptr") == 0)
                    {
                        char *base = xstrdup(args[0]);
                        base[alen - 3] = '\0';
                        zfree(unmangled);
                        unmangled = xmalloc(strlen(base) + 16);
                        if (is_unmangle_primitive(base))
                        {
                            sprintf(unmangled, "%s*", base); /* safe */
                        }
                        else
                        {
                            sprintf(unmangled, "struct %s*", base); /* safe */
                        }
                        zfree(base);
                    }
                    instantiate_generic(ctx, struct_base, args[0], unmangled, dummy_tok);
                    zfree(unmangled);
                }
                else if (count > 1)
                {
                    instantiate_generic_multi(ctx, struct_base, args, count, dummy_tok);
                }

                // Cleanup
                for (int i = 0; i < count; i++)
                {
                    zfree(args[i]);
                }
                zfree(args);
            }
            zfree(struct_base);
        }
        zfree(param_suffix);
    }

    ASTNode *new_fn =
        copy_ast_replacing(ctx, tpl->func_node, tpl->generic_param, subst_arg, NULL, NULL);
    if (!new_fn || new_fn->kind != NODE_FUNCTION)
    {
        return NULL;
    }

    zfree(new_fn->func.name);
    new_fn->func.name = xstrdup(mangled);
    new_fn->func.generic_params = NULL;

    add_instantiated_func(ctx, new_fn);

    register_func(ctx, ctx->global_scope, mangled, new_fn->func.count, new_fn->func.defaults,
                  new_fn->func.arg_types, new_fn->func.ret_type_info, new_fn->func.is_varargs, 0,
                  new_fn->func.pure, new_fn->link_name, new_fn->token, new_fn->func.is_export);

    trigger_instantiations(ctx, new_fn->func.body);

    if (new_fn->func.arg_types)
    {
        for (int i = 0; i < new_fn->func.count; i++)
        {
            Type *at = new_fn->func.arg_types[i];
            if (at && at->kind == TYPE_ARRAY && at->array_size == 0 && at->inner)
            {
                char *inner_str = type_to_string(at->inner);
                register_slice(ctx, inner_str);
                zfree(inner_str);
            }
        }
    }

    return mangled;
}

void register_template(ParserContext *ctx, const char *name, ASTNode *node)
{
    GenericTemplate *t = xcalloc(1, sizeof(GenericTemplate));
    t->name = xstrdup(name);
    t->struct_node = node;
    t->next = ctx->templates;
    ctx->templates = t;
}

static ASTNode *copy_fields_replacing(ParserContext *ctx, ASTNode *fields, const char *param,
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
        char *underscore = (char *)strrchr(n->field.type, '_');
        if (underscore && underscore > n->field.type)
        {
            // Remove trailing '*' if present
            char *type_copy = xstrdup(n->field.type);
            char *star = (char *)strchr(type_copy, '*');
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
                    if (concrete)
                    {
                        char *clean_concrete = sanitize_mangled_name(concrete);
                        if (strcmp(concrete_arg, clean_concrete) == 0)
                        {
                            zfree(unmangled);
                            unmangled = xstrdup(concrete);
                        }
                        zfree(clean_concrete);
                    }

                    instantiate_generic(ctx, template_name, concrete_arg, unmangled, fields->token);
                    zfree(unmangled);
                }
            }
            zfree(type_copy);
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
                // Check if name starts with template name followed by double underscore
                if (strncmp(inner->name, gt->name, (size_t)(tlen)) == 0 &&
                    inner->name[tlen] == '_' && inner->name[tlen + 1] == '_')
                {
                    template_name = gt->name;
                    concrete_arg =
                        inner->name + tlen + 2; // Skip template name and double underscore
                    break;
                }
                gt = gt->next;
            }

            if (template_name && concrete_arg)
            {
                char *unmangled = unmangle_ptr_suffix(concrete_arg);
                if (concrete)
                {
                    char *clean_concrete = sanitize_mangled_name(concrete);
                    if (strcmp(concrete_arg, clean_concrete) == 0)
                    {
                        zfree(unmangled);
                        unmangled = xstrdup(concrete);
                    }
                    zfree(clean_concrete);
                }
                instantiate_generic(ctx, template_name, concrete_arg, unmangled, fields->token);
                zfree(unmangled);
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

    ASTNode *new_impl = copy_ast_replacing(ctx, it->impl_node, it->generic_param, subst_arg,
                                           it->struct_name, mangled_struct_name);

    // Also replace mangled template name (both List__G and List_G)
    if (strchr(it->struct_name, '<'))
    {
        char *sanitized = sanitize_mangled_name(it->struct_name);
        if (strcmp(sanitized, it->struct_name) != 0)
        {
            ASTNode *tmp =
                copy_ast_replacing(ctx, new_impl, NULL, NULL, sanitized, mangled_struct_name);
            new_impl = tmp;
        }

        char *old_sanitized = xstrdup(sanitized);
        char *double_underscore = (char *)strstr(old_sanitized, "__");
        if (double_underscore)
        {
            memmove(double_underscore, double_underscore + 1, strlen(double_underscore + 1) + 1);
        }

        if (strcmp(old_sanitized, it->struct_name) != 0 && strcmp(old_sanitized, sanitized) != 0)
        {
            ASTNode *tmp =
                copy_ast_replacing(ctx, new_impl, NULL, NULL, old_sanitized, mangled_struct_name);
            new_impl = tmp;
        }

        zfree(old_sanitized);
        zfree(sanitized);
    }
    zfree(subst_arg);
    it->impl_node->next = backup_next; // Restore

    ASTNode *meth = NULL;

    if (new_impl->kind == NODE_IMPL)
    {
        new_impl->impl.struct_name = xstrdup(mangled_struct_name);
        meth = new_impl->impl.methods;
    }
    else if (new_impl->kind == NODE_IMPL_TRAIT)
    {
        new_impl->impl_trait.target_type = xstrdup(mangled_struct_name);
        meth = new_impl->impl_trait.methods;
    }

    while (meth)
    {
        // Standardize: ensure __ between type and method
        // If it's already correctly mangled (e.g. Vec__int32_t__with_capacity), skip
        size_t mlen = strlen(mangled_struct_name);
        int correctly_mangled =
            (strncmp(meth->func.name, mangled_struct_name, (size_t)(mlen)) == 0 &&
             meth->func.name[mlen] == '_' && meth->func.name[mlen + 1] == '_');

        if (!correctly_mangled)
        {
            // Find the method part in the original name (e.g. "with_capacity" in
            // "Vec_with_capacity")
            char *original_method = meth->func.name;
            if (strncmp(original_method, it->struct_name, strlen(it->struct_name)) == 0)
            {
                original_method += strlen(it->struct_name);
                // Strip the separator (legacy single "_" or the "__" separator),
                // but keep the method's own leading underscores so that e.g.
                // "_private_method" stays distinct from "private_method".
                if (original_method[0] == '_' && original_method[1] == '_')
                {
                    original_method += 2;
                }
                else if (original_method[0] == '_')
                {
                    original_method += 1;
                }
            }

            char *temp = xmalloc(strlen(mangled_struct_name) + strlen(original_method) + 3);
            sprintf(temp, "%s__%s", mangled_struct_name, original_method); /* safe */
            char *new_name = merge_underscores(temp);
            zfree(temp);
            zfree(meth->func.name);
            meth->func.name = new_name;
        }

        register_func(ctx, ctx->global_scope, meth->func.name, meth->func.count,
                      meth->func.defaults, meth->func.arg_types, meth->func.ret_type_info,
                      meth->func.is_varargs, (meth->kind == NODE_FUNCTION && meth->func.is_async),
                      meth->func.pure, meth->link_name, meth->token, meth->func.is_export);

        // Handle generic return types in methods (e.g., Option<T> -> Option_int)
        if (meth->func.ret_type &&
            (strchr(meth->func.ret_type, '_') || strchr(meth->func.ret_type, '<')))
        {
            GenericTemplate *gt = ctx->templates;

            while (gt)
            {
                size_t tlen = strlen(gt->name);
                size_t rlen = strlen(meth->func.ret_type);
                if (tlen > rlen)
                {
                    gt = gt->next;
                    continue;
                }
                char delim = meth->func.ret_type[tlen];
                if (strncmp(meth->func.ret_type, gt->name, (size_t)(tlen)) == 0 &&
                    (delim == '_' || delim == '<'))
                {
                    // Found matching template prefix
                    const char *type_arg = meth->func.ret_type + tlen;
                    while (*type_arg == '_' || *type_arg == '<')
                    {
                        type_arg++;
                    }

                    // Simple approach: instantiate 'Template' with 'Arg'.
                    char *clean_arg = xstrdup(type_arg);
                    if (delim == '<')
                    {
                        char *closer = (char *)strrchr(clean_arg, '>');
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
                        zfree(inner_unmangled_arg);
                        inner_unmangled_arg = xmalloc(strlen(base) + 16);
                        // Check if base is a primitive type
                        if (is_unmangle_primitive(base))
                        {
                            sprintf(inner_unmangled_arg, "%s*", base); /* safe */
                        }
                        else
                        {
                            sprintf(inner_unmangled_arg, "struct %s*", base); /* safe */
                        }
                        zfree(base);
                    }

                    instantiate_generic(ctx, gt->name, clean_arg, inner_unmangled_arg, meth->token);
                    zfree(clean_arg);
                }
                gt = gt->next;
            }
        }

        trigger_instantiations(ctx, meth->func.body);

        meth = meth->next;
    }
    add_instantiated_func(ctx, new_impl);
}

static void register_enum_constructor(ParserContext *ctx, const char *m, const char *var_name,
                                      int tag_id, Type *payload, Token token, int is_export)
{
    size_t mangled_var_sz = strlen(m) + strlen(var_name) + 3;
    char *mangled_var = xmalloc((size_t)(mangled_var_sz));
    snprintf(mangled_var, mangled_var_sz, "%s__%s", m, var_name);
    register_enum_variant(ctx, m, mangled_var, tag_id);

    Type *ret_t = type_new(TYPE_ENUM);
    ret_t->name = xstrdup(m);

    if (payload)
    {
        Type **at = xmalloc(sizeof(Type *));
        at[0] = payload;
        register_func(ctx, ctx->global_scope, mangled_var, 1, NULL, at, ret_t, 0, 0, 0, NULL, token,
                      is_export);
    }
    else
    {
        register_func(ctx, ctx->global_scope, mangled_var, 0, NULL, NULL, ret_t, 0, 0, 0, NULL,
                      token, is_export);
    }
    zfree(mangled_var);
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
    char *m = xmalloc(strlen(tpl) + strlen(clean_arg) + 4);
    strcpy(m, tpl);
    char *m_end = m + strlen(m);
    while (m_end > m && *(m_end - 1) == '_')
    {
        *(--m_end) = '\0';
    }
    strcat(m, "__");
    strcat(m, clean_arg);
    zfree(clean_arg);

    Instantiation *c = ctx->instantiations;
    while (c)
    {
        if (strcmp(c->name, m) == 0)
        {
            zfree(m);
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
        return; // fault tolerance: zpanic_at returned, bail out
    }

    Instantiation *ni = xcalloc(1, sizeof(Instantiation));
    ni->name = xstrdup(m);
    ni->template_name = xstrdup(tpl);
    ni->concrete_arg = xstrdup(arg);
    ni->unmangled_arg = unmangled_arg ? xstrdup(unmangled_arg)
                                      : xstrdup(arg); // Fallback to arg if unmangled is generic
    ni->struct_node = NULL;                           // Placeholder to break cycles
    ni->next = ctx->instantiations;
    ctx->instantiations = ni;

    ASTNode *struct_node_copy = NULL;

    if (t->struct_node->kind == NODE_STRUCT)
    {
        ASTNode *i = ast_create(NODE_STRUCT);
        i->strct.name = xstrdup(m);
        i->strct.is_template = 0;
        i->strct.is_export = t->struct_node->strct.is_export;

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
            if (fld->field.type && strncmp(fld->field.type, "Slice__", 7) == 0)
            {
                register_slice(ctx, fld->field.type + 7);
            }
            fld = fld->next;
        }
    }
    else if (t->struct_node->kind == NODE_ENUM)
    {
        ASTNode *i = ast_create(NODE_ENUM);
        i->enm.name = xstrdup(m);
        i->enm.is_template = 0;
        i->enm.is_export = t->struct_node->enm.is_export;

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

            register_enum_constructor(ctx, m, nv->variant.name, nv->variant.tag_id,
                                      nv->variant.payload, token, i->enm.is_export);

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
        // Cache in hash table for fast lookup.
        if (struct_node_copy->kind == NODE_STRUCT && struct_node_copy->strct.name)
        {
            struct_hash_insert(ctx, struct_node_copy->strct.name, struct_node_copy);
        }
        else if (struct_node_copy->kind == NODE_ENUM && struct_node_copy->enm.name)
        {
            struct_hash_insert(ctx, struct_node_copy->enm.name, struct_node_copy);
        }

        struct_node_copy->next = ctx->instantiated_structs;
        ctx->instantiated_structs = struct_node_copy;
    }

    GenericImplTemplate *it = ctx->impl_templates;
    while (it)
    {
        if (strcmp(it->struct_name, tpl) == 0)
        {
            instantiate_methods(ctx, it, m, arg, unmangled_arg);
        }
        it = it->next;
    }
    zfree(m);
}

static void free_field_list(ASTNode *fields)
{
    while (fields)
    {
        ASTNode *next = fields->next;
        if (fields->field.name)
        {
            zfree(fields->field.name);
        }
        if (fields->field.type)
        {
            zfree(fields->field.type);
        }
        zfree(fields);
        fields = next;
    }
}

void instantiate_generic_multi(ParserContext *ctx, const char *tpl, char **args, int count,
                               Token token)
{
    // Build mangled name from all args
    size_t m_len = strlen(tpl) + 1;
    for (int i = 0; i < count; i++)
    {
        char *clean = sanitize_mangled_name(args[i]);
        m_len += 2 + strlen(clean);
        zfree(clean);
    }
    char *m = xmalloc((size_t)(m_len + 1));
    strcpy(m, tpl);
    char *m_end = m + strlen(m);
    while (m_end > m && *(m_end - 1) == '_')
    {
        *(--m_end) = '\0';
    }
    for (int i = 0; i < count; i++)
    {
        char *clean = sanitize_mangled_name(args[i]);
        strcat(m, "__");
        strcat(m, clean);
        zfree(clean);
    }

    // Check if already instantiated
    Instantiation *c = ctx->instantiations;
    while (c)
    {
        if (strcmp(c->name, m) == 0)
        {
            zfree(m);
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
        return;
    }

    // Register instantiation first (to break cycles)
    Instantiation *ni = xcalloc(1, sizeof(Instantiation));
    ni->name = xstrdup(m);
    ni->template_name = xstrdup(tpl);
    ni->concrete_arg = (count > 0) ? xstrdup(args[0]) : xstrdup("T");

    // For multi-param, build a comma-separated string for unmangled_arg
    size_t u_len = 0;
    for (int i = 0; i < count; i++)
    {
        u_len += strlen(args[i]) + 1;
    }
    char *u_buf = xmalloc((size_t)(u_len + 1));
    u_buf[0] = 0;
    for (int i = 0; i < count; i++)
    {
        if (i > 0)
        {
            strcat(u_buf, ",");
        }
        strcat(u_buf, args[i]);
    }
    ni->unmangled_arg = u_buf;

    ni->struct_node = NULL;
    ni->next = ctx->instantiations;
    ctx->instantiations = ni;

    if (t->struct_node->kind == NODE_STRUCT)
    {
        ASTNode *i = ast_create(NODE_STRUCT);
        i->strct.name = xstrdup(m);
        i->strct.is_template = 0;
        i->strct.is_export = t->struct_node->strct.is_export;

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

        if (param_count > 0 && count > 0)
        {
            // First substitution
            i->strct.fields = copy_fields_replacing(
                ctx, fields, t->struct_node->strct.generic_params[0], args[0]);

            // Subsequent substitutions (for params B, C, etc.)
            for (int j = 1; j < param_count && j < count; j++)
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
    else if (t->struct_node->kind == NODE_ENUM)
    {
        ASTNode *i = ast_create(NODE_ENUM);
        i->enm.name = xstrdup(m);
        i->enm.is_template = 0;
        i->enm.is_export = t->struct_node->enm.is_export;

        // Copy type attributes
        i->type_info = type_new(TYPE_ENUM);
        i->type_info->name = xstrdup(m);
        if (t->struct_node->type_info)
        {
            i->type_info->traits = t->struct_node->type_info->traits;
        }

        ASTNode *h = 0, *tl = 0;
        ASTNode *v = t->struct_node->enm.variants;

        // Construct comma-separated concrete args string
        size_t c_args_len = 1;
        for (int j = 0; j < count; j++)
        {
            c_args_len += strlen(args[j]) + 1;
        }
        char *c_args = xmalloc((size_t)(c_args_len));
        c_args[0] = 0;
        for (int j = 0; j < count; j++)
        {
            if (j > 0)
            {
                strcat(c_args, ",");
            }
            strcat(c_args, args[j]);
        }

        while (v)
        {
            ASTNode *nv = ast_create(NODE_ENUM_VARIANT);
            nv->variant.name = xstrdup(v->variant.name);
            nv->variant.tag_id = v->variant.tag_id;

            // Use multi-parameter substitution for payload
            Type *payload = v->variant.payload;
            nv->variant.payload = NULL;
            if (payload)
            {
                nv->variant.payload = replace_type_formal(
                    payload, t->struct_node->enm.generic_param, c_args, NULL, NULL);
            }

            register_enum_constructor(ctx, m, nv->variant.name, nv->variant.tag_id,
                                      nv->variant.payload, token, i->enm.is_export);

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
        zfree(c_args);
        i->enm.variants = h;
        ni->struct_node = i;
        register_struct_def(ctx, m, i);

        i->next = ctx->instantiated_structs;
        ctx->instantiated_structs = i;
    }
    zfree(m);
}

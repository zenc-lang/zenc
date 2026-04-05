
#include "../ast/ast.h"
#include "../parser/parser.h"
#include "../zprep.h"
#include "codegen.h"
#include "../ast/primitives.h"
#include <ctype.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global state
ASTNode *global_user_structs = NULL;
char *g_current_impl_type = NULL;
int tmp_counter = 0;
ASTNode *defer_stack[MAX_DEFER];
int defer_count = 0;
ASTNode *g_current_lambda = NULL;

int loop_defer_boundary[MAX_LOOP_DEPTH];
int loop_depth = 0;
int func_defer_boundary = 0;

int pending_closure_frees[MAX_PENDING_CLOSURE_FREES];
int pending_closure_free_count = 0;

void emit_pending_closure_frees(FILE *out)
{
    for (int i = 0; i < pending_closure_free_count; i++)
    {
        fprintf(out, "free(_z_closure_ctx_stash[%d]); _z_closure_ctx_stash[%d] = NULL;\n",
                pending_closure_frees[i], pending_closure_frees[i]);
    }
    pending_closure_free_count = 0;
}

// Strip template suffix from a type name (for example, "MyStruct<T>" -> "MyStruct")
// Returns newly allocated string, caller must free.
char *strip_template_suffix(const char *name)
{
    if (!name)
    {
        return NULL;
    }
    char *lt = strchr(name, '<');
    if (lt)
    {
        int len = lt - name;
        char *buf = xmalloc(len + 1);
        strncpy(buf, name, len);
        buf[len] = 0;
        return buf;
    }
    return xstrdup(name);
}

// Helper to emit a mangled name (Type__Method) with standardized underscores.
void emit_mangled_name(FILE *out, const char *base, const char *method)
{
    if (!base || !method)
    {
        return;
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s__%s", base, method);
    char *merged = merge_underscores(buf);
    fprintf(out, "%s", merged);
    free(merged);
}

// Helper to emit C declaration (handle arrays, function pointers correctly)
void emit_c_decl(ParserContext *ctx, FILE *out, const char *type_str, const char *name)
{
    char *bracket = strchr(type_str, '[');
    char *generic = strchr(type_str, '<');
    char *fn_ptr = strstr(type_str, "(*");

    if (fn_ptr)
    {
        char *end_paren = strchr(fn_ptr, ')');
        if (end_paren)
        {
            int prefix_len = end_paren - type_str;
            fprintf(out, "%.*s%s%s", prefix_len, type_str, name, end_paren);
        }
        else
        {
            // Fallback if malformed (shouldn't happen)
            int prefix_len = fn_ptr - type_str + 2;
            fprintf(out, "%.*s%s%s", prefix_len, type_str, name, fn_ptr + 2);
        }
    }
    else if (generic && (!bracket || generic < bracket))
    {
        char mangled_candidate[256];
        char *gt = strchr(generic, '>');
        int success = 0;

        if (gt)
        {
            int base_len = generic - type_str;
            int arg_len = gt - generic - 1;

            // Limit check
            if (base_len + arg_len + 2 < 256)
            {
                snprintf(mangled_candidate, 256, "%.*s__%.*s", base_len, type_str, arg_len,
                         generic + 1);

                if (find_struct_def(ctx, mangled_candidate))
                {
                    fprintf(out, "%s %s", mangled_candidate, name);
                    success = 1;
                }
            }
        }

        if (!success)
        {
            int base_len = generic - type_str;
            fprintf(out, "%.*s %s", base_len, type_str, name);
        }
        else if (gt[1] == '*')
        {
            fprintf(out, "*");
        }

        if (bracket)
        {
            fprintf(out, "%s", bracket);
        }
    }
    else if (bracket)
    {
        int base_len = bracket - type_str;
        fprintf(out, "%.*s %s%s", base_len, type_str, name, bracket);
    }
    else
    {
        fprintf(out, "%s %s", type_str, name);
    }
}

// Helper to emit variable declarations with array types.
void emit_var_decl_type(ParserContext *ctx, FILE *out, const char *type_str, const char *var_name)
{
    emit_c_decl(ctx, out, type_str, var_name);
}

// Get field type from struct.
char *get_field_type_str(ParserContext *ctx, const char *struct_name, const char *field_name)
{
    char clean_name[256];
    strncpy(clean_name, struct_name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = 0;

    char *ptr = strchr(clean_name, '<');
    if (ptr)
    {
        *ptr = 0;
    }

    ASTNode *def = find_struct_def(ctx, clean_name);
    if (!def)
    {
        return NULL;
    }

    ASTNode *f = def->strct.fields;
    while (f)
    {
        if (strcmp(f->field.name, field_name) == 0)
        {
            return f->field.type;
        }
        f = f->next;
    }
    return NULL;
}

// Type inference.
char *infer_type(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return NULL;
    }

    if (node->type_info && node->type_info->kind != TYPE_UNKNOWN)
    {
        char *t = type_to_c_string(node->type_info);
        return t;
    }

    if (node->resolved_type && strcmp(node->resolved_type, "unknown") != 0)
    {
        if (strcmp(node->resolved_type, "c_int") == 0)
        {
            return "int";
        }
        if (strcmp(node->resolved_type, "c_uint") == 0)
        {
            return "unsigned int";
        }
        if (strcmp(node->resolved_type, "c_long") == 0)
        {
            return "long";
        }
        if (strcmp(node->resolved_type, "c_ulong") == 0)
        {
            return "unsigned long";
        }
        if (strcmp(node->resolved_type, "c_long_long") == 0)
        {
            return "long long";
        }
        if (strcmp(node->resolved_type, "c_ulong_long") == 0)
        {
            return "unsigned long long";
        }
        if (strcmp(node->resolved_type, "c_short") == 0)
        {
            return "short";
        }
        if (strcmp(node->resolved_type, "c_ushort") == 0)
        {
            return "unsigned short";
        }
        if (strcmp(node->resolved_type, "c_char") == 0)
        {
            return "char";
        }
        if (strcmp(node->resolved_type, "c_uchar") == 0)
        {
            return "unsigned char";
        }

        return node->resolved_type;
    }

    if (node->type == NODE_EXPR_LITERAL)
    {
        if (node->type_info)
        {
            return type_to_c_string(node->type_info);
        }
        return NULL;
    }

    if (node->type == NODE_EXPR_VAR)
    {
        ZenSymbol *sym = find_symbol_entry(ctx, node->var_ref.name);
        if (sym)
        {
            if (sym->type_name)
            {
                return sym->type_name;
            }
            if (sym->type_info)
            {
                return type_to_c_string(sym->type_info);
            }
        }
    }

    if (node->type == NODE_EXPR_CALL)
    {
        if (node->call.callee->type == NODE_EXPR_VAR)
        {
            FuncSig *sig = find_func(ctx, node->call.callee->var_ref.name);
            if (sig)
            {
                if (sig->is_async)
                {
                    if (sig->ret_type)
                    {
                        char *inner = type_to_c_string(sig->ret_type);
                        if (inner)
                        {
                            char *buf = xmalloc(strlen(inner) + 10);
                            sprintf(buf, "Async<%s>", inner);
                            return buf;
                        }
                    }
                    return "Async";
                }
                if (sig->ret_type)
                {
                    return type_to_c_string(sig->ret_type);
                }
            }

            // Fallback for known stdlib memory/file functions.
            if (strcmp(node->call.callee->var_ref.name, "malloc") == 0 ||
                strcmp(node->call.callee->var_ref.name, "calloc") == 0 ||
                strcmp(node->call.callee->var_ref.name, "realloc") == 0 ||
                strcmp(node->call.callee->var_ref.name, "fopen") == 0 ||
                strcmp(node->call.callee->var_ref.name, "popen") == 0 ||
                strcmp(node->call.callee->var_ref.name, "fdopen") == 0)
            {
                return "void*";
            }
            ASTNode *sdef = find_struct_def(ctx, node->call.callee->var_ref.name);
            if (sdef)
            {
                return node->call.callee->var_ref.name;
            }
        }
        // Method call: target.method() - look up Type_method signature.
        if (node->call.callee->type == NODE_EXPR_MEMBER)
        {
            char *target_type = infer_type(ctx, node->call.callee->member.target);
            if (target_type)
            {
                char clean_type[256];
                snprintf(clean_type, sizeof(clean_type), "%s", target_type);
                char *ptr = strchr(clean_type, '*');
                if (ptr)
                {
                    *ptr = 0;
                }

                char *base = clean_type;
                if (strncmp(base, "struct ", 7) == 0)
                {
                    base += 7;
                }

                char func_base[512];
                sprintf(func_base, "%s__%s", base, node->call.callee->member.field);
                char *func_name = merge_underscores(func_base);

                FuncSig *sig = find_func(ctx, func_name);
                if (sig && sig->ret_type)
                {
                    char *ret = type_to_c_string(sig->ret_type);
                    free(func_name);
                    return ret;
                }
                free(func_name);
            }
        }

        if (node->call.callee->type == NODE_EXPR_VAR)
        {
            ZenSymbol *sym = find_symbol_entry(ctx, node->call.callee->var_ref.name);
            if (sym && sym->type_info && sym->type_info->kind == TYPE_FUNCTION &&
                sym->type_info->inner)
            {
                return type_to_c_string(sym->type_info->inner);
            }
        }
    }

    if (node->type == NODE_TRY)
    {
        char *inner_type = infer_type(ctx, node->try_stmt.expr);
        if (inner_type)
        {
            // Extract T from Result<T> or Option<T>
            char *start = strchr(inner_type, '<');
            if (start)
            {
                start++; // Skip <
                char *end = strrchr(inner_type, '>');
                if (end && end > start)
                {
                    int len = end - start;
                    char *extracted = xmalloc(len + 1);
                    strncpy(extracted, start, len);
                    extracted[len] = 0;
                    return extracted;
                }
            }

            // Find the struct/enum definition and look for "Ok" or "val"
            char *search_name = inner_type;
            if (strncmp(search_name, "struct ", 7) == 0)
            {
                search_name += 7;
            }

            ASTNode *def = find_struct_def(ctx, search_name);
            if (!def)
            {
                // check enums list explicitly if not found in instantiated list
                StructRef *er = ctx->parsed_enums_list;
                while (er)
                {
                    if (er->node && er->node->type == NODE_ENUM &&
                        strcmp(er->node->enm.name, search_name) == 0)
                    {
                        def = er->node;
                        break;
                    }
                    er = er->next;
                }
            }

            if (def)
            {
                if (def->type == NODE_ENUM)
                {
                    // Look for "Ok" variant
                    ASTNode *var = def->enm.variants;
                    while (var)
                    {
                        if (var->variant.name && strcmp(var->variant.name, "Ok") == 0)
                        {
                            if (var->variant.payload)
                            {
                                return type_to_c_string(var->variant.payload);
                            }
                            // Ok with no payload? Then it's void/u0.
                            return "void";
                        }
                        var = var->next;
                    }
                }
                else if (def->type == NODE_STRUCT)
                {
                    // Look for "val" field
                    ASTNode *field = def->strct.fields;
                    while (field)
                    {
                        if (field->field.name && strcmp(field->field.name, "val") == 0)
                        {
                            return xstrdup(field->field.type);
                        }
                        field = field->next;
                    }
                }
            }
        }
    }

    if (node->type == NODE_EXPR_MEMBER)
    {
        char *parent_type = infer_type(ctx, node->member.target);
        if (!parent_type)
        {
            return NULL;
        }

        char clean_name[256];
        snprintf(clean_name, sizeof(clean_name), "%s", parent_type);
        char *ptr = strchr(clean_name, '*');
        if (ptr)
        {
            *ptr = 0;
        }

        return get_field_type_str(ctx, clean_name, node->member.field);
    }

    if (node->type == NODE_EXPR_BINARY)
    {
        if (strcmp(node->binary.op, "??") == 0)
        {
            return infer_type(ctx, node->binary.left);
        }

        const char *op = node->binary.op;
        char *left_type = infer_type(ctx, node->binary.left);
        char *right_type = infer_type(ctx, node->binary.right);

        int is_logical = (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 || strcmp(op, "==") == 0 ||
                          strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                          strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0);

        if (is_logical)
        {
            return xstrdup("int");
        }

        if (left_type && strcmp(left_type, "usize") == 0)
        {
            return "usize";
        }
        if (right_type && strcmp(right_type, "usize") == 0)
        {
            return "usize";
        }
        if (left_type && strcmp(left_type, "double") == 0)
        {
            return "double";
        }

        return left_type ? left_type : right_type;
    }

    if (node->type == NODE_MATCH)
    {
        ASTNode *case_node = node->match_stmt.cases;
        while (case_node)
        {
            char *type = infer_type(ctx, case_node->match_case.body);
            if (type && strcmp(type, "void") != 0 && strcmp(type, "unknown") != 0)
            {
                return type;
            }
            case_node = case_node->next;
        }
        return NULL;
    }

    if (node->type == NODE_EXPR_INDEX)
    {
        char *array_type = infer_type(ctx, node->index.array);
        if (array_type)
        {
            // If T*, returns T. If T[], returns T.
            char *ptr = strrchr(array_type, '*');
            if (ptr)
            {
                int len = ptr - array_type;
                char *buf = xmalloc(len + 1);
                strncpy(buf, array_type, len);
                buf[len] = 0;
                return buf;
            }

            if (strncmp(array_type, "Slice__", 7) == 0)
            {
                return xstrdup(array_type + 7);
            }

            char *search_name = array_type;
            if (strncmp(search_name, "struct ", 7) == 0)
            {
                search_name += 7;
            }

            ASTNode *def = find_struct_def(ctx, search_name);
            if (def && def->type_info && def->type_info->kind == TYPE_VECTOR &&
                def->type_info->inner)
            {
                return type_to_c_string(def->type_info->inner);
            }
        }
        return "int";
    }

    if (node->type == NODE_EXPR_UNARY)
    {
        if (strcmp(node->unary.op, "&") == 0)
        {
            char *inner = infer_type(ctx, node->unary.operand);
            if (inner)
            {
                char *buf = xmalloc(strlen(inner) + 2);
                sprintf(buf, "%s*", inner);
                return buf;
            }
        }
        if (strcmp(node->unary.op, "*") == 0)
        {
            char *inner = infer_type(ctx, node->unary.operand);
            if (inner)
            {
                if (strcmp(inner, "string") == 0)
                {
                    return xstrdup("char");
                }
                char *ptr = strchr(inner, '*');
                if (ptr)
                {
                    // Return base type (naive)
                    int len = ptr - inner;
                    char *dup = xmalloc(len + 1);
                    strncpy(dup, inner, len);
                    dup[len] = 0;
                    return dup;
                }
            }
        }
        return infer_type(ctx, node->unary.operand);
    }

    if (node->type == NODE_AWAIT)
    {
        // Infer underlying type T from await Async<T>
        // Check operand type for Generics <T>
        char *op_type = infer_type(ctx, node->unary.operand);
        if (op_type)
        {
            char *start = strchr(op_type, '<');
            if (start)
            {
                start++; // Skip <
                char *end = strrchr(op_type, '>');
                if (end && end > start)
                {
                    int len = end - start;
                    char *extracted = xmalloc(len + 1);
                    strncpy(extracted, start, len);
                    extracted[len] = 0;
                    return extracted;
                }
            }
        }

        // Fallback: If it's a direct call await foo(), we can lookup signature even if generic
        // syntax wasn't used
        if (node->unary.operand->type == NODE_EXPR_CALL &&
            node->unary.operand->call.callee->type == NODE_EXPR_VAR)
        {
            FuncSig *sig = find_func(ctx, node->unary.operand->call.callee->var_ref.name);
            if (sig && sig->ret_type)
            {
                return type_to_c_string(sig->ret_type);
            }
        }

        return "void*";
    }

    if (node->type == NODE_EXPR_CAST)
    {
        return node->cast.target_type;
    }

    if (node->type == NODE_EXPR_STRUCT_INIT)
    {
        return node->struct_init.struct_name;
    }

    if (node->type == NODE_EXPR_ARRAY_LITERAL)
    {
        if (node->type_info)
        {
            return type_to_c_string(node->type_info);
        }
        return NULL;
    }

    if (node->type == NODE_EXPR_LITERAL)
    {
        if (node->literal.type_kind == LITERAL_STRING)
        {
            return xstrdup("string");
        }
        if (node->literal.type_kind == LITERAL_CHAR)
        {
            return xstrdup("char");
        }
        if (node->literal.type_kind == LITERAL_FLOAT)
        {
            return "double";
        }
        return "int";
    }

    return NULL;
}

// Extract variable names from argument string.
char *extract_call_args(const char *args)
{
    if (!args || strlen(args) == 0)
    {
        return xstrdup("");
    }
    char *out = xmalloc(strlen(args) + 1);
    out[0] = 0;

    char *dup = xstrdup(args);
    char *p = strtok(dup, ",");
    while (p)
    {
        while (*p == ' ')
        {
            p++;
        }
        char *last_space = strrchr(p, ' ');
        char *ptr_star = strrchr(p, '*');

        char *name = p;
        if (last_space)
        {
            name = last_space + 1;
        }
        if (ptr_star && ptr_star > last_space)
        {
            name = ptr_star + 1;
        }

        if (strlen(out) > 0)
        {
            strcat(out, ", ");
        }
        strcat(out, name);

        p = strtok(NULL, ",");
    }
    free(dup);
    return out;
}

// Parse original method name from mangled name.
const char *parse_original_method_name(const char *mangled)
{
    const char *sep = strstr(mangled, "__");
    if (!sep)
    {
        return mangled;
    }

    // Let's iterate to find the last `__`.
    const char *last_double = NULL;
    const char *p = mangled;
    while ((p = strstr(p, "__")))
    {
        last_double = p;
        p += 2;
    }

    return last_double ? last_double + 2 : mangled;
}

// Replace string type in arguments.
char *replace_string_type(const char *args)
{
    if (!args)
    {
        return NULL;
    }
    char *res = xmalloc(strlen(args) * 2 + 1);
    res[0] = 0;
    const char *p = args;
    while (*p)
    {
        const char *match = strstr(p, "string ");
        if (match)
        {
            if (match > args && (isalnum(*(match - 1)) || *(match - 1) == '_'))
            {
                strncat(res, p, match - p + 6);
                p = match + 6;
            }
            else
            {
                strncat(res, p, match - p);
                strcat(res, "const char* ");
                p = match + 7;
            }
        }
        else
        {
            strcat(res, p);
            break;
        }
    }
    return res;
}

// Helper to emit auto type or fallback.
void emit_auto_type(ParserContext *ctx, ASTNode *init_expr, Token t, FILE *out)
{
    (void)t;
    char *inferred = NULL;
    if (init_expr)
    {
        inferred = infer_type(ctx, init_expr);
    }

    if (inferred && strcmp(inferred, "__auto_type") != 0 && strcmp(inferred, "unknown") != 0)
    {
        fprintf(out, "%s", inferred);
    }
    else
    {
        if (strstr(g_config.cc, "tcc") && init_expr)
        {
            fprintf(out, "__typeof__((");
            codegen_expression(ctx, init_expr, out);
            fprintf(out, "))");
        }
        else
        {
            fprintf(out, "ZC_AUTO");
        }
    }
}

// Emit function signature using Type info for correct C codegen
void emit_func_signature(ParserContext *ctx, FILE *out, ASTNode *func, const char *name_override)
{
    if (!func || func->type != NODE_FUNCTION)
    {
        return;
    }

    // Emit CUDA qualifiers (for both forward declarations and definitions)
    if (g_config.use_cuda)
    {
        if (func->func.cuda_global)
        {
            fprintf(out, "__global__ ");
        }
        if (func->func.cuda_device)
        {
            fprintf(out, "__device__ ");
        }
        if (func->func.cuda_host)
        {
            fprintf(out, "__host__ ");
        }
    }

    // Return type
    char *ret_str;
    if (func->func.ret_type_info)
    {
        ret_str = type_to_c_string(func->func.ret_type_info);
    }
    else if (func->func.ret_type)
    {
        ret_str = xstrdup(func->func.ret_type);
    }
    else
    {
        ret_str = xstrdup("void");
    }

    char *ret_suffix = NULL;
    char *fn_ptr = strstr(ret_str, "(*)");

    if (fn_ptr)
    {
        int prefix_len = fn_ptr - ret_str + 2; // Include "(*"
        fprintf(out, "%.*s%s(", prefix_len, ret_str,
                name_override ? name_override : func->func.name);
        ret_suffix = fn_ptr + 2;
    }
    else
    {
        fprintf(out, "%s %s(", ret_str, name_override ? name_override : func->func.name);
    }
    free(ret_str);

    // Args
    if (func->func.arg_count == 0 && !func->func.is_varargs)
    {
        fprintf(out, "void");
    }
    else
    {
        for (int i = 0; i < func->func.arg_count; i++)
        {
            if (i > 0)
            {
                fprintf(out, ", ");
            }

            char *type_str = NULL;
            // Check for @ctype override first
            if (func->func.c_type_overrides && func->func.c_type_overrides[i])
            {
                type_str = xstrdup(func->func.c_type_overrides[i]);
            }
            else if (func->func.arg_types && func->func.arg_types[i])
            {
                type_str = type_to_c_string(func->func.arg_types[i]);
            }
            else
            {
                type_str = xstrdup("void*"); // Fallback
            }

            const char *name = "";

            if (func->func.param_names && func->func.param_names[i])
            {
                name = func->func.param_names[i];
            }

            // check if array type
            emit_c_decl(ctx, out, type_str, name);
            free(type_str);
        }
        if (func->func.is_varargs)
        {
            if (func->func.arg_count > 0)
            {
                fprintf(out, ", ");
            }
            fprintf(out, "...");
        }
    }
    fprintf(out, ")");

    if (ret_suffix)
    {
        fprintf(out, "%s", ret_suffix);
    }
}

// Invalidate a moved-from variable by zeroing it out to prevent double-free
int emit_move_invalidation(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (!node)
    {
        return 0;
    }

    // Check if it's a valid l-value we can memset
    if (node->type != NODE_EXPR_VAR && node->type != NODE_EXPR_MEMBER)
    {
        return 0;
    }

    // Common logic to find type and check Drop
    char *type_name = infer_type(ctx, node);
    ASTNode *def = NULL;
    if (type_name)
    {
        char *clean_type = type_name;
        if (strncmp(clean_type, "struct ", 7) == 0)
        {
            clean_type += 7;
        }
        def = find_struct_def(ctx, clean_type);
    }

    Type *t = node->type_info;
    int has_drop = 0;
    if (t)
    {
        if (t->kind == TYPE_FUNCTION)
        {
            has_drop = t->traits.has_drop && !t->is_raw;
        }
        else if (t->kind == TYPE_STRUCT || t->kind == TYPE_ENUM)
        {
            if (def && def->type_info)
            {
                has_drop = def->type_info->traits.has_drop;
            }
            else
            {
                has_drop = t->traits.has_drop;
            }
        }
    }
    else if (def && def->type_info)
    {
        has_drop = def->type_info->traits.has_drop;
    }

    if (has_drop)
    {
        if (node->type == NODE_EXPR_VAR)
        {
            char *df_prefix = "";
            if (g_current_lambda)
            {
                for (int i = 0; i < g_current_lambda->lambda.num_captures; i++)
                {
                    if (strcmp(node->var_ref.name, g_current_lambda->lambda.captured_vars[i]) == 0)
                    {
                        if (g_current_lambda->lambda.capture_modes &&
                            g_current_lambda->lambda.capture_modes[i] == 0)
                        {
                            df_prefix = "ctx->";
                        }
                        break;
                    }
                }
            }

            if (strcmp(node->var_ref.name, "self") != 0)
            {
                fprintf(out, "%s__z_drop_flag_%s = 0", df_prefix, node->var_ref.name);
                return 1;
            }
            return 0;
        }
        else if (node->type == NODE_EXPR_MEMBER)
        {
            // For members: memset(&foo.bar, 0, sizeof(foo.bar))
            fprintf(out, "memset(&");
            codegen_expression(ctx, node, out);
            fprintf(out, ", 0, sizeof(");
            codegen_expression(ctx, node, out);
            fprintf(out, "))");
            return 1;
        }
    }
    return 0;
}

// Emits expression, wrapping it in a move-invalidation block if it's a consuming variable usage
void codegen_expression_with_move(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (node && (node->type == NODE_EXPR_VAR || node->type == NODE_EXPR_MEMBER))
    {
        // Re-use infer logic to see if we need invalidation
        char *type_name = infer_type(ctx, node);
        ASTNode *def = NULL;
        if (type_name)
        {
            char *clean_type = type_name;
            if (strncmp(clean_type, "struct ", 7) == 0)
            {
                clean_type += 7;
            }
            def = find_struct_def(ctx, clean_type);
        }

        Type *t = node->type_info;
        int has_drop = 0;
        if (t)
        {
            if (t->kind == TYPE_FUNCTION)
            {
                has_drop = t->traits.has_drop && !t->is_raw;
            }
            else if (t->kind == TYPE_STRUCT || t->kind == TYPE_ENUM)
            {
                if (def && def->type_info)
                {
                    has_drop = def->type_info->traits.has_drop;
                }
                else
                {
                    has_drop = t->traits.has_drop;
                }
            }
        }
        else if (def && def->type_info)
        {
            has_drop = def->type_info->traits.has_drop;
        }

        if (has_drop)
        {
            if (node->type == NODE_EXPR_VAR)
            {
                fprintf(out, "({ ");
                emit_move_invalidation(ctx, node, out);
                fprintf(out, "; ");
                codegen_expression(ctx, node, out);
                fprintf(out, "; })");
            }
            else
            {
                fprintf(out, "({ __typeof__(");
                codegen_expression(ctx, node, out);
                fprintf(out, ") _mv = ");
                codegen_expression(ctx, node, out);
                fprintf(out, "; ");
                emit_move_invalidation(ctx, node, out);
                fprintf(out, "; _mv; })");
            }
            return;
        }
    }
    codegen_expression(ctx, node, out);
}

int is_struct_return_type(const char *ret_type)
{
    if (!ret_type)
    {
        return 0;
    }

    // Primitives from table (both Zen and C names)
    if (find_primitive_by_name(ret_type) || find_primitive_by_c_name(ret_type))
    {
        return 0;
    }

    // C types that might be used directly
    if (strcmp(ret_type, "size_t") == 0 || strcmp(ret_type, "ptrdiff_t") == 0 ||
        strcmp(ret_type, "ssize_t") == 0 || strcmp(ret_type, "intptr_t") == 0 ||
        strcmp(ret_type, "uintptr_t") == 0)
    {
        return 0;
    }

    // C23 BitInt Support (i42, u256, etc.)
    if ((ret_type[0] == 'i' || ret_type[0] == 'u') && isdigit(ret_type[1]))
    {
        return 0;
    }

    return 1;
}

int z_is_struct_type(Type *t)
{
    if (!t)
    {
        return 0;
    }
    Type *base = get_inner_type(t);
    return (base->kind == TYPE_STRUCT || base->kind == TYPE_ENUM);
}

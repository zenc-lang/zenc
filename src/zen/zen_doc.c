// SPDX-License-Identifier: MIT
#include "zen_doc.h"
#include "../ast/ast.h"
#include "../parser/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @brief Cleans a doc comment by removing leading markers.
 */
static char *clean_comment_content(const char *content)
{
    if (!content)
    {
        return NULL;
    }

    // Calculate total length (roughly)
    char *result = xmalloc(strlen(content) + 1);
    char *out = result;
    const char *in = content;
    int done = 0;

    while (*in)
    {
        // Skip leading whitespace on each line
        while (*in && isspace(*in))
        {
            in++;
        }

        // Remove markers at start of line
        if (strncmp(in, "*/", 2) == 0)
        {
            in += 2;
            break;
        }
        else if (strncmp(in, "///", 3) == 0)
        {
            in += 3;
        }
        else if (strncmp(in, "//", 2) == 0)
        {
            in += 2;
        }
        else if (strncmp(in, "/**", 3) == 0)
        {
            in += 3;
        }
        else if (strncmp(in, "/*", 2) == 0)
        {
            in += 2;
        }
        else if (*in == '*')
        {
            in++; // Star at start of block comment line
        }

        // Skip spaces after marker
        while (*in == ' ')
        {
            in++;
        }

        // Copy until end of line
        while (*in && *in != '\n' && *in != '\r')
        {
            // Avoid copying block comment end marker
            if (*in == '*' && in[1] == '/')
            {
                in += 2;
                done = 1;
                break;
            }
            *out++ = *in++;
        }

        if (done)
        {
            break;
        }

        if (*in == '\n' || *in == '\r')
        {
            *out++ = '\n';
            if (*in == '\r' && in[1] == '\n')
            {
                in++;
            }
            in++;
        }
    }
    *out = 0;

    // Trim trailing newlines
    while (out > result && isspace(*(out - 1)))
    {
        *(--out) = 0;
    }

    return result;
}

static void print_markdown_doc(const char *comment)
{
    if (!comment)
    {
        return;
    }
    char *cleaned = clean_comment_content(comment);
    if (cleaned && cleaned[0])
    {
        printf("\n%s\n\n", cleaned);
    }
    zfree(cleaned);
}

static const char *unmangle_name(const char *name)
{
    if (!name)
    {
        return NULL;
    }
    const char *last = (char *)strstr(name, "__");
    if (last)
    {
        // Find the LAST __
        const char *next = (char *)strstr(last + 2, "__");
        while (next)
        {
            last = next;
            next = strstr(last + 2, "__");
        }
        return last + 2;
    }
    return name;
}

static void generate_docs_internal(struct ParserContext *ctx, ASTNode *node, int level)
{
    while (node)
    {
        if (node->doc_comment)
        {
            // Associated with this node
        }

        switch (node->kind)
        {
        case NODE_FUNCTION:
        {
            const char *display_name = unmangle_name(node->func.name);
            printf("\n## function `%s`\n", display_name);
            if (node->doc_comment)
            {
                print_markdown_doc(node->doc_comment);
            }

            // Print signature
            printf("```zc\nfn %s(", display_name);
            for (int i = 0; i < node->func.count; i++)
            {
                if (node->func.param_names && node->func.param_names[i])
                {
                    printf("%s: ", node->func.param_names[i]);
                }
                if (node->func.arg_types && node->func.arg_types[i])
                {
                    char *tstr = type_to_string(node->func.arg_types[i]);
                    printf("%s", tstr);
                    zfree(tstr);
                }
                else
                {
                    printf("?");
                }
                if (i < node->func.count - 1)
                {
                    printf(", ");
                }
            }
            printf(")");
            if (node->func.ret_type_info)
            {
                char *rstr = type_to_string(node->func.ret_type_info);
                printf(" -> %s", rstr);
                zfree(rstr);
            }
            else if (node->func.ret_type)
            {
                printf(" -> %s", node->func.ret_type);
            }
            printf(";\n```\n");
            break;
        }

        case NODE_STRUCT:
        {
            const char *display_name = unmangle_name(node->strct.name);
            printf("\n## struct `%s`\n", display_name);
            if (node->doc_comment)
            {
                print_markdown_doc(node->doc_comment);
            }

            printf("```zc\nstruct %s {\n", display_name);
            ASTNode *field = node->strct.fields;
            while (field)
            {
                if (field->kind == NODE_FIELD)
                {
                    printf("    %s: %s,\n", field->field.name,
                           field->field.type ? field->field.type : "?");
                }
                field = field->next;
            }
            printf("}\n```\n");

            // Print field docs if any
            field = node->strct.fields;
            while (field)
            {
                if (field->kind == NODE_FIELD && field->doc_comment)
                {
                    printf("\n### field `%s`\n", field->field.name);
                    print_markdown_doc(field->doc_comment);
                }
                field = field->next;
            }
            break;
        }

        case NODE_ENUM:
        {
            const char *display_name = unmangle_name(node->enm.name);
            printf("\n## enum `%s`\n", display_name);
            if (node->doc_comment)
            {
                print_markdown_doc(node->doc_comment);
            }

            printf("```zc\nenum %s {\n", display_name);
            ASTNode *variant = node->enm.variants;
            while (variant)
            {
                if (variant->kind == NODE_ENUM_VARIANT)
                {
                    printf("    %s,\n", variant->variant.name);
                }
                variant = variant->next;
            }
            printf("}\n```\n");

            // Print variant docs if any
            variant = node->enm.variants;
            while (variant)
            {
                if (variant->kind == NODE_ENUM_VARIANT && variant->doc_comment)
                {
                    printf("\n### variant `%s`\n", variant->variant.name);
                    print_markdown_doc(variant->doc_comment);
                }
                variant = variant->next;
            }
            break;
        }

        case NODE_CONST:
        {
            const char *display_name = unmangle_name(node->var_decl.name);
            printf("\n## const `%s`\n", display_name);
            if (node->doc_comment)
            {
                print_markdown_doc(node->doc_comment);
            }
            printf("```zc\nconst %s: %s;\n```\n", display_name,
                   node->var_decl.type_str ? node->var_decl.type_str : "?");
            break;
        }

        case NODE_TYPE_ALIAS:
        {
            const char *display_name = unmangle_name(node->type_alias.alias);
            printf("\n## alias `%s`\n", display_name);
            if (node->doc_comment)
            {
                print_markdown_doc(node->doc_comment);
            }
            printf("```zc\nalias %s = %s;\n```\n", display_name,
                   node->type_alias.original_type ? node->type_alias.original_type : "?");
            break;
        }

        case NODE_IMPL:
        case NODE_IMPL_TRAIT:
        {
            const char *raw_name =
                (node->kind == NODE_IMPL) ? node->impl.struct_name : node->impl_trait.target_type;
            const char *sname = unmangle_name(raw_name);
            printf("\n## impl for `%s`\n", sname);
            if (node->doc_comment)
            {
                print_markdown_doc(node->doc_comment);
            }

            ASTNode *method =
                (node->kind == NODE_IMPL) ? node->impl.methods : node->impl_trait.methods;
            while (method)
            {
                if (method->kind == NODE_FUNCTION)
                {
                    const char *mname = unmangle_name(method->func.name);
                    printf("\n### method `%s`\n", mname);
                    if (method->doc_comment)
                    {
                        print_markdown_doc(method->doc_comment);
                    }
                    printf("```zc\nfn %s(...);\n```\n", mname);
                }
                method = method->next;
            }
            break;
        }

        case NODE_IMPORT:
        {
            if (ctx->config->recursive_doc)
            {
                printf("\n---\n# Module: %s\n", node->import_stmt.path);
                if (node->doc_comment)
                {
                    print_markdown_doc(node->doc_comment);
                }
                generate_docs_internal(ctx, node->import_stmt.module_root, level + 1);
                printf("\n---\n");
            }
            break;
        }

        default:
            break;
        }

        node = node->next;
    }
}

void generate_docs(struct ParserContext *ctx, ASTNode *root)
{
    if (root->kind != NODE_ROOT)
    {
        return;
    }

    printf("# Module: %s\n", ctx->config->input_file ? ctx->config->input_file : "Unknown");
    if (root->doc_comment)
    {
        print_markdown_doc(root->doc_comment);
    }

    generate_docs_internal(ctx, root->root.children, 0);

    // Generic impl blocks (`impl Box<T>`) are registered as templates instead
    // of top-level AST nodes, so the walk above skips them. Render their
    // methods here (isolating each impl node so the iterator stops cleanly).
    for (GenericImplTemplate *it = ctx->impl_templates; it; it = it->next)
    {
        if (it->impl_node && it->impl_node->kind == NODE_IMPL)
        {
            ASTNode *saved_next = it->impl_node->next;
            it->impl_node->next = NULL;
            generate_docs_internal(ctx, it->impl_node, 0);
            it->impl_node->next = saved_next;
        }
    }
}

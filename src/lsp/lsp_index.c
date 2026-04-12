#include "../constants.h"

#include "lsp_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LSPIndex *lsp_index_new()
{
    return calloc(1, sizeof(LSPIndex));
}

void lsp_index_free(LSPIndex *idx)
{
    if (!idx)
    {
        return;
    }
    LSPRange *c = idx->head;
    while (c)
    {
        LSPRange *n = c->next;
        if (c->hover_text)
        {
            free(c->hover_text);
        }
        free(c);
        c = n;
    }
    free(idx);
}

void lsp_index_add(LSPIndex *idx, LSPRange *r)
{
    if (!idx->head)
    {
        idx->head = r;
        idx->tail = r;
    }
    else
    {
        idx->tail->next = r;
        idx->tail = r;
    }
}

void lsp_index_add_def(LSPIndex *idx, Token t, const char *hover, ASTNode *node)
{
    if (t.line <= 0)
    {
        return;
    }
    LSPRange *r = calloc(1, sizeof(LSPRange));
    r->type = RANGE_DEFINITION;
    r->start_line = t.line - 1;
    r->start_col = t.col - 1;
    r->end_line = t.line - 1;
    r->end_col = t.col - 1 + t.len;
    if (hover)
    {
        r->hover_text = strdup(hover);
    }
    r->node = node;

    lsp_index_add(idx, r);
}
void lsp_index_add_plugin(LSPIndex *idx, ASTNode *node)
{
    if (!node || node->type != NODE_PLUGIN)
    {
        return;
    }
    LSPRange *r = calloc(1, sizeof(LSPRange));
    r->type = RANGE_DEFINITION; // DEFINITION range works for hover
    r->start_line = node->plugin_stmt.start_line - 1;
    r->start_col = node->plugin_stmt.start_col - 1;
    r->end_line = node->plugin_stmt.end_line - 1;
    r->end_col = node->plugin_stmt.end_col - 1;
    r->node = node;

    // Default hover text for the plugin itself (fallback)
    r->hover_text = strdup(node->plugin_stmt.plugin_name);

    lsp_index_add(idx, r);
}

void lsp_index_add_ref(LSPIndex *idx, Token t, Token def_t, ASTNode *node)
{
    if (t.line <= 0 || def_t.line <= 0)
    {
        return;
    }
    LSPRange *r = calloc(1, sizeof(LSPRange));
    r->type = RANGE_REFERENCE;
    r->start_line = t.line - 1;
    r->start_col = t.col - 1;
    r->end_line = t.line - 1;
    r->end_col = t.col - 1 + t.len;

    r->def_line = def_t.line - 1;
    r->def_col = def_t.col - 1;
    r->node = node;

    lsp_index_add(idx, r);
}

LSPRange *lsp_find_at(LSPIndex *idx, int line, int col)
{
    LSPRange *curr = idx->head;
    LSPRange *best = NULL;

    while (curr)
    {
        if (line >= curr->start_line && line <= curr->end_line)
        {
            if (line == curr->start_line && col < curr->start_col)
            {
                curr = curr->next;
                continue;
            }

            if (line == curr->end_line && col > curr->end_col)
            {
                curr = curr->next;
                continue;
            }

            best = curr;
        }
        curr = curr->next;
    }
    return best;
}

// Walker.

static void lsp_walk_node(LSPIndex *idx, ASTNode *node, int depth)
{
    if (depth > 32)
    {
        return;
    }

    while (node)
    {
        // Definition logic.
        if (node->type == NODE_FUNCTION)
        {
            char hover[MAX_SHORT_MSG_LEN];
            const char *name = node->func.name ? node->func.name : "unknown";
            const char *ret = node->func.ret_type ? node->func.ret_type : "void";
            snprintf(hover, sizeof(hover), "fn %s(...) -> %s", name, ret);
            lsp_index_add_def(idx, node->token, hover, node);

            // Recurse body.
            lsp_walk_node(idx, node->func.body, depth + 1);
        }
        else if (node->type == NODE_VAR_DECL)
        {
            char hover[MAX_SHORT_MSG_LEN];
            const char *name = node->var_decl.name ? node->var_decl.name : "unknown";
            snprintf(hover, sizeof(hover), "var %s", name);
            lsp_index_add_def(idx, node->token, hover, node);

            lsp_walk_node(idx, node->var_decl.init_expr, depth + 1);
        }
        else if (node->type == NODE_CONST)
        {
            char hover[MAX_SHORT_MSG_LEN];
            const char *name = node->var_decl.name ? node->var_decl.name : "unknown";
            snprintf(hover, sizeof(hover), "const %s", name);
            lsp_index_add_def(idx, node->token, hover, node);

            lsp_walk_node(idx, node->var_decl.init_expr, depth + 1);
        }
        else if (node->type == NODE_STRUCT)
        {
            char hover[MAX_SHORT_MSG_LEN];
            const char *name = node->strct.name ? node->strct.name : "unknown";
            snprintf(hover, sizeof(hover), "%sstruct %s", node->strct.is_opaque ? "opaque " : "",
                     name);
            lsp_index_add_def(idx, node->token, hover, node);

            ASTNode *field = node->strct.fields;
            while (field)
            {
                if (field->type == NODE_FIELD)
                {
                    char fh[MAX_SHORT_MSG_LEN];
                    const char *fname = field->field.name ? field->field.name : "unknown";
                    const char *ftype = field->field.type ? field->field.type : "unknown";
                    snprintf(fh, sizeof(fh), "field %s: %s", fname, ftype);
                    lsp_index_add_def(idx, field->token, fh, field);
                }
                field = field->next;
            }
        }
        else if (node->type == NODE_ENUM)
        {
            char hover[MAX_SHORT_MSG_LEN];
            const char *name = node->enm.name ? node->enm.name : "unknown";
            snprintf(hover, sizeof(hover), "enum %s", name);
            lsp_index_add_def(idx, node->token, hover, node);

            ASTNode *variant = node->enm.variants;
            while (variant)
            {
                if (variant->type == NODE_ENUM_VARIANT)
                {
                    char vh[MAX_SHORT_MSG_LEN];
                    const char *vname = variant->variant.name ? variant->variant.name : "unknown";
                    snprintf(vh, sizeof(vh), "variant %s", vname);
                    lsp_index_add_def(idx, variant->token, vh, variant);
                }
                variant = variant->next;
            }
        }
        else if (node->type == NODE_TYPE_ALIAS)
        {
            char hover[MAX_SHORT_MSG_LEN];
            const char *alias = node->type_alias.alias ? node->type_alias.alias : "unknown";
            const char *orig =
                node->type_alias.original_type ? node->type_alias.original_type : "unknown";
            snprintf(hover, sizeof(hover), "alias %s = %s", alias, orig);
            lsp_index_add_def(idx, node->token, hover, node);
        }
        else if (node->type == NODE_TRAIT)
        {
            char hover[MAX_SHORT_MSG_LEN];
            const char *name = node->trait.name ? node->trait.name : "unknown";
            snprintf(hover, sizeof(hover), "trait %s", name);
            lsp_index_add_def(idx, node->token, hover, node);
        }
        else if (node->type == NODE_PLUGIN)
        {
            lsp_index_add_plugin(idx, node);
        }

        // Reference logic.
        if (node->definition_token.line > 0)
        {
            lsp_index_add_ref(idx, node->token, node->definition_token, node);
        }

        // General recursion for children.
        switch (node->type)
        {
        case NODE_ROOT:
            lsp_walk_node(idx, node->root.children, depth + 1);
            break;
        case NODE_BLOCK:
            lsp_walk_node(idx, node->block.statements, depth + 1);
            break;
        case NODE_IF:
            lsp_walk_node(idx, node->if_stmt.condition, depth + 1);
            lsp_walk_node(idx, node->if_stmt.then_body, depth + 1);
            lsp_walk_node(idx, node->if_stmt.else_body, depth + 1);
            break;
        case NODE_WHILE:
            lsp_walk_node(idx, node->while_stmt.condition, depth + 1);
            lsp_walk_node(idx, node->while_stmt.body, depth + 1);
            break;
        case NODE_RETURN:
            lsp_walk_node(idx, node->ret.value, depth + 1);
            break;
        case NODE_EXPR_BINARY:
            lsp_walk_node(idx, node->binary.left, depth + 1);
            lsp_walk_node(idx, node->binary.right, depth + 1);
            break;
        case NODE_EXPR_UNARY:
            lsp_walk_node(idx, node->unary.operand, depth + 1);
            break;
        case NODE_EXPR_CAST:
            lsp_walk_node(idx, node->cast.expr, depth + 1);
            break;
        case NODE_EXPR_MEMBER:
            lsp_walk_node(idx, node->member.target, depth + 1);
            break;
        case NODE_EXPR_INDEX:
            lsp_walk_node(idx, node->index.array, depth + 1);
            lsp_walk_node(idx, node->index.index, depth + 1);
            break;
        case NODE_EXPR_CALL:
            lsp_walk_node(idx, node->call.callee, depth + 1);
            lsp_walk_node(idx, node->call.args, depth + 1);
            break;
        case NODE_MATCH:
            lsp_walk_node(idx, node->match_stmt.expr, depth + 1);
            lsp_walk_node(idx, node->match_stmt.cases, depth + 1);
            break;
        case NODE_MATCH_CASE:
            lsp_walk_node(idx, node->match_case.guard, depth + 1);
            lsp_walk_node(idx, node->match_case.body, depth + 1);
            break;
        case NODE_FOR:
            lsp_walk_node(idx, node->for_stmt.init, depth + 1);
            lsp_walk_node(idx, node->for_stmt.condition, depth + 1);
            lsp_walk_node(idx, node->for_stmt.step, depth + 1);
            lsp_walk_node(idx, node->for_stmt.body, depth + 1);
            break;
        case NODE_FOR_RANGE:
            lsp_walk_node(idx, node->for_range.start, depth + 1);
            lsp_walk_node(idx, node->for_range.end, depth + 1);
            lsp_walk_node(idx, node->for_range.body, depth + 1);
            break;
        case NODE_LOOP:
            lsp_walk_node(idx, node->loop_stmt.body, depth + 1);
            break;
        case NODE_DEFER:
            lsp_walk_node(idx, node->defer_stmt.stmt, depth + 1);
            break;
        case NODE_GUARD:
            lsp_walk_node(idx, node->guard_stmt.condition, depth + 1);
            lsp_walk_node(idx, node->guard_stmt.body, depth + 1);
            break;
        case NODE_TEST:
            lsp_walk_node(idx, node->test_stmt.body, depth + 1);
            break;
        case NODE_IMPL:
            lsp_walk_node(idx, node->impl.methods, depth + 1);
            break;
        case NODE_IMPL_TRAIT:
            lsp_walk_node(idx, node->impl_trait.methods, depth + 1);
            break;
        default:
            break;
        }

        // Move to next sibling without recursion.
        node = node->next;
    }
}

void lsp_build_index(LSPIndex *idx, ASTNode *root)
{
    lsp_walk_node(idx, root, 0);
}

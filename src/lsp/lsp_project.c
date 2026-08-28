// SPDX-License-Identifier: MIT
#include "lsp_project.h"
#include "../utils/utils.h"
#include "../constants.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

LSPProject *g_project = NULL;

static void scan_dir(const char *dir_path);
void lsp_default_on_error(void *data, Token t, const char *msg);

// Initialize the project with a root directory

// Perform full project indexing

void lsp_project_init(const char *root_path)
{
    if (g_project)
    {
        return;
    }

    // Close any previous hoist_out before creating a new project context
    if (g_project && g_project->ctx && g_project->ctx->cg.hoist_out)
    {
        fclose(g_project->ctx->cg.hoist_out);
    }

    g_project = xcalloc(1, sizeof(LSPProject));
    g_project->root_path = xstrdup(root_path);

    // Create a persistent global context
    g_project->ctx = xcalloc(1, sizeof(ParserContext));
    g_project->ctx->compiler = &g_compiler;
    g_project->ctx->config = &g_compiler.config;
    g_project->ctx->is_fault_tolerant = 1;
    module_state_init(&g_project->ctx->imports);
    g_project->ctx->cg.hoist_out = tmpfile(); // Support hoisting in LSP
    if (!g_project->ctx->cg.hoist_out)
    {
        fprintf(stderr, "zls: Warning: Failed to create hoist_out temporary file. Hoisting will be "
                        "disabled.\n");
    }

    // Register this persistent context as the parser/token/diagnostic context.
    // Without config the lexer/parser deref ctx->config (e.g. misra_mode) and
    // segfault on the first didOpen, and without the diagnostic context a parse
    // error would fall through to exit(1) and kill the whole server.
    token_set_parser_ctx(g_project->ctx);
    diag_set_parser_ctx(g_project->ctx);

    // Set a default error handler that just logs to stderr (or ignores)
    // to prevent exit(1) during initial scan.
    g_project->ctx->on_error = lsp_default_on_error;

    CompilerConfig *cfg = &g_project->ctx->compiler->config;

    // Add root path and std/ to include paths to resolve 'std.zc' etc.
    // Ensure we don't overflow the include_paths array (limit is 64)
    zvec_push_Str(&cfg->include_paths, xstrdup(root_path));

    // In LSP mode, the workspace root should also be considered the root path for stdlib
    // resolution
    if (root_path)
    {
        cfg->root_path = xstrdup(root_path);
    }

    if (cfg->root_path)
    {
        char std_path[MAX_PATH_LEN];
        snprintf(std_path, sizeof(std_path), "%s/std", cfg->root_path);
        zvec_push_Str(&cfg->include_paths, xstrdup(std_path));
    }
}

void lsp_project_index_workspace(void)
{
    if (!g_project || !g_project->root_path)
    {
        return;
    }

    // Scan workspace
    g_is_indexing = 1;
    scan_dir(g_project->root_path);
    g_is_indexing = 0;
}

// Default error handler for indexing phase
void lsp_default_on_error(void *data, Token t, const char *msg)
{
    (void)data;
    (void)t;
    (void)msg;
    // We can log it if we want, but standard zpanic_at already printed it to stderr.
    // The important thing is that we exist so zpanic_at returns.
    // Maybe we suppress duplicates or just let it pass.
    // Since zpanic_at printed "error: ...", we don't need to print again.
}

static void scan_file(const char *path)
{
    // Skip if not .zc
    const char *ext = (char *)strrchr(path, '.');
    if (!ext || strcmp(ext, ".zc") != 0)
    {
        return;
    }

    char uri[MAX_PATH_LEN + 16];
    snprintf(uri, sizeof(uri), "file://%s", path);

    // Deduplicate indexing
    if (lsp_project_get_file(uri))
    {
        return;
    }

    char *src = load_file(path, g_project->ctx->current_filename);
    if (!src)
    {
        return;
    }

    lsp_project_update_file(uri, src);

    // Free source after update
    zfree(src);
}

static void scan_dir(const char *dir_path)
{
    DIR *d = opendir(dir_path);
    if (!d)
    {
        return;
    }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL)
    {
        if (dir->d_name[0] == '.')
        {
            continue;
        }

        // Project filters: skip known noise directories
        if (strcmp(dir->d_name, "node_modules") == 0 || strcmp(dir->d_name, "obj") == 0 ||
            strcmp(dir->d_name, ".git") == 0 ||
            strcmp(dir->d_name, "vhdl") == 0 || // Often contains large vendor dirs
            strcmp(dir->d_name, "vivado") == 0)
        {
            continue;
        }

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dir_path, dir->d_name);

        struct stat st;
        if (stat(path, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
            {
                scan_dir(path);
            }
            else if (S_ISREG(st.st_mode))
            {
                scan_file(path);
            }
        }
    }
    closedir(d);
}

ProjectFile *lsp_project_get_file(const char *uri)
{
    if (!g_project)
    {
        return NULL;
    }
    ProjectFile *curr = g_project->files;
    while (curr)
    {
        if (strcmp(curr->uri, uri) == 0)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

static ProjectFile *add_project_file(const char *uri)
{
    ProjectFile *f = xcalloc(1, sizeof(ProjectFile));
    f->uri = xstrdup(uri);
    // Simple path extraction from URI (file://...)
    if (strncmp(uri, "file://", 7) == 0)
    {
        f->path = xstrdup(uri + 7);
    }
    else
    {
        f->path = xstrdup(uri);
    }

    f->next = g_project->files;
    g_project->files = f;
    return f;
}

void lsp_project_update_file(const char *uri, const char *src)
{
    if (!g_project)
    {
        return;
    }

    ProjectFile *pf = lsp_project_get_file(uri);
    if (!pf)
    {
        pf = add_project_file(uri);
    }

    // Use the plain path for internal compiler state.
    // This allows z_resolve_path and is_file_imported to work correctly.
    const char *saved_filename = g_project->ctx->current_filename;
    g_project->ctx->current_filename = pf->path;

    if (pf->index)
    {
        lsp_index_free(pf->index);
        pf->index = NULL;
    }

    if (pf->source)
    {
        zfree(pf->source);
    }
    pf->source = xstrdup(src);

    Lexer l;
    lexer_init(&l, src, g_project->ctx->config, g_project->ctx->current_filename);

    // Reset parser context globals only for fresh manual updates.
    // During workspace indexing, we want to accumulate definitions.
    // Initialize built-ins if it's the first time
    if (!g_project->ctx->global_scope)
    {
        register_builtins(g_project->ctx);
    }

    g_project->ctx->had_error = 0;

    if (!is_file_imported(g_project->ctx, pf->path))
    {
        mark_file_imported(g_project->ctx, pf->path);
    }

    ASTNode *root = parse_program(g_project->ctx, &l);
    if (root)
    {
        pf->ast = root;
        pf->index = lsp_index_new();
        lsp_build_index(pf->index, root);

        if (!g_is_indexing)
        {
            validate_types(g_project->ctx);
        }
    }
    else
    {
        pf->ast = NULL;
    }

    g_project->ctx->current_filename = saved_filename;
}

DefinitionResult lsp_project_find_definition(const char *name)
{
    DefinitionResult res = {0};
    if (!g_project)
    {
        return res;
    }

    ProjectFile *pf = g_project->files;
    while (pf)
    {
        if (pf->index)
        {
            LSPRange *r = pf->index->head;
            while (r)
            {
                if (r->kind == RANGE_DEFINITION && r->node)
                {
                    char *found_name = NULL;
                    if (r->node->kind == NODE_FUNCTION)
                    {
                        found_name = r->node->func.name;
                    }
                    else if (r->node->kind == NODE_VAR_DECL)
                    {
                        found_name = r->node->var_decl.name;
                    }
                    else if (r->node->kind == NODE_CONST)
                    {
                        found_name = r->node->var_decl.name;
                    }
                    else if (r->node->kind == NODE_STRUCT)
                    {
                        found_name = r->node->strct.name;
                    }
                    else if (r->node->kind == NODE_ENUM)
                    {
                        found_name = r->node->enm.name;
                    }
                    else if (r->node->kind == NODE_ENUM_VARIANT)
                    {
                        found_name = r->node->variant.name;
                    }
                    else if (r->node->kind == NODE_TRAIT)
                    {
                        found_name = r->node->trait.name;
                    }
                    else if (r->node->kind == NODE_TYPE_ALIAS)
                    {
                        found_name = r->node->type_alias.alias;
                    }

                    if (found_name && strcmp(found_name, name) == 0)
                    {
                        res.uri = pf->uri;
                        res.range = r;
                        return res;
                    }
                }
                r = r->next;
            }
        }
        pf = pf->next;
    }

    return res;
}

// Find all references to a symbol name project-wide

ReferenceResult *lsp_project_find_references(const char *name)
{
    if (!g_project)
    {
        return NULL;
    }
    ReferenceResult *head = NULL;
    ReferenceResult *tail = NULL;

    ProjectFile *pf = g_project->files;
    while (pf)
    {
        if (pf->index)
        {
            LSPRange *r = pf->index->head;
            while (r)
            {
                // We want REFERENCES that match the name
                // Or DEFINITIONS that match the name (include decl)
                char *scan_name = NULL;

                if (r->node)
                {
                    if (r->node->kind == NODE_FUNCTION)
                    {
                        scan_name = r->node->func.name;
                    }
                    else if (r->node->kind == NODE_VAR_DECL)
                    {
                        scan_name = r->node->var_decl.name;
                    }
                    else if (r->node->kind == NODE_CONST)
                    {
                        scan_name = r->node->var_decl.name;
                    }
                    else if (r->node->kind == NODE_STRUCT)
                    {
                        scan_name = r->node->strct.name;
                    }
                    else if (r->node->kind == NODE_ENUM)
                    {
                        scan_name = r->node->enm.name;
                    }
                    else if (r->node->kind == NODE_ENUM_VARIANT)
                    {
                        scan_name = r->node->variant.name;
                    }
                    else if (r->node->kind == NODE_TRAIT)
                    {
                        scan_name = r->node->trait.name;
                    }
                    else if (r->node->kind == NODE_TYPE_ALIAS)
                    {
                        scan_name = r->node->type_alias.alias;
                    }
                    else if (r->node->kind == NODE_EXPR_VAR)
                    {
                        scan_name = r->node->var_ref.name;
                    }
                    else if (r->node->kind == NODE_EXPR_CALL && r->node->call.callee &&
                             r->node->call.callee->kind == NODE_EXPR_VAR)
                    {
                        scan_name = r->node->call.callee->var_ref.name;
                    }
                }

                if (scan_name && strcmp(scan_name, name) == 0)
                {
                    ReferenceResult *new_res = calloc(1, sizeof(ReferenceResult));
                    new_res->uri = pf->uri;
                    new_res->range = r;

                    if (!head)
                    {
                        head = new_res;
                        tail = new_res;
                    }
                    else
                    {
                        tail->next = new_res;
                        tail = new_res;
                    }
                }

                r = r->next;
            }
        }
        pf = pf->next;
    }
    return head;
}

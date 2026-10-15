// SPDX-License-Identifier: MIT
#ifndef LSP_PROJECT_H
#ifndef ZC_ALLOW_INTERNAL
#error "lsp/lsp_project.h is internal to Zen C. Include the appropriate public header instead."
#endif

#define LSP_PROJECT_H

#include "parser.h"
#include "lsp_index.h"
struct cJSON;

/**
 * @brief Represents a tracked file in the LSP project.
 */
typedef struct ProjectFile
{
    char *path;      ///< Absolute file path.
    char *uri;       ///< file:// URI.
    char *source;    ///< Cached source content (in-memory).
    ASTNode *ast;    ///< Cached AST for semantic analysis.
    LSPIndex *index; ///< File-specific symbol index.
    struct ProjectFile *next;
} ProjectFile;

/**
 * @brief Global state for the Language Server Project.
 */
typedef struct
{
    ParserContext *ctx;
    ProjectFile *files;        ///< List of tracked open files.
    char *root_path;           ///< Project root directory.
    zvec_Str exclude_patterns; ///< `exclude` of zenc.server.json, in .gitignore syntax.
    int use_gitignore;         ///< `use_gitignore` of zenc.server.json (default 1).
} LSPProject;

// Global project instance
extern LSPProject *g_project;
extern int g_is_indexing;

// Initialize the project with a root directory
void lsp_project_init(const char *root_path);

// Perform full project indexing
void lsp_project_index_workspace(void);

// Find a file in the project
ProjectFile *lsp_project_get_file(const char *uri);

// Update a file (re-parse and re-index)
void lsp_project_update_file(const char *uri, const char *src);

// Remember the latest text of an open document (outside the arena) so a rebuild can
// re-apply it.
void lsp_project_track_document(const char *uri, const char *src);

// Forget a document the client closed: a rebuild reads it from disk again.
void lsp_project_untrack_document(const char *uri);

// Arena usage right after the last full build (workspace indexing or rebuild).
size_t lsp_project_built_total(void);

// Rewind the arena to `base`, restore the compiler state saved together with it and
// rebuild the project from disk plus the tracked documents. Every pointer into the
// arena taken after `base` is invalid afterwards.
void lsp_project_rebuild(ZarenaMark base, const ZenCompiler *base_compiler);

// Find definition globally
typedef struct
{
    char *uri;
    LSPRange *range;
} DefinitionResult;

DefinitionResult lsp_project_find_definition(const char *name);

typedef struct ReferenceResult
{
    char *uri;
    LSPRange *range;
    struct ReferenceResult *next;
} ReferenceResult;

ReferenceResult *lsp_project_find_references(const char *name);

// Semantic Tokens
char *lsp_semantic_tokens_full(const char *uri);

// LSP analysis functions (declared here for cross-file visibility)
void lsp_on_error(void *data, Token t, const char *msg);
void lsp_on_diagnostic(void *data, Token t, int severity, const char *msg, int diag_id);
void lsp_check_file(const char *uri, const char *src, int id);
void lsp_close_file(const char *uri);
void lsp_goto_definition(const char *uri, int line, int col, int id);
void lsp_hover(const char *uri, int line, int col, int id);
void lsp_completion(const char *uri, int line, int col, int id);
void lsp_document_symbol(const char *uri, int id);
void lsp_workspace_symbol(const char *query, int id);
void lsp_references(const char *uri, int line, int col, int id);
void lsp_signature_help(const char *uri, int line, int col, int id);
void lsp_rename(const char *uri, int line, int col, const char *new_name, int id);
void lsp_code_action(const char *uri, struct cJSON *diagnostics, int id);

#endif

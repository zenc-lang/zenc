// SPDX-License-Identifier: MIT

#ifndef ZC_ALLOW_INTERNAL
#error "parser/parser.h is internal to Zen C. Include the appropriate public header instead."
#endif

#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "compiler.h"
#include "../diagnostics/diagnostics.h"
#include "../utils/emitter.h"

// Operator precedence for expression parsing

/**
 * @brief Operator precedence for expression parsing.
 */
typedef enum
{
    PREC_NONE,       ///< No precedence.
    PREC_ASSIGNMENT, ///< Assignment operators.
    PREC_TERNARY,    ///< Ternary operator.
    PREC_OR,         ///< Logical OR.
    PREC_AND,        ///< Logical AND.
    PREC_EQUALITY,   ///< Equality operators.
    PREC_COMPARISON, ///< Comparison operators.
    PREC_TERM,       ///< Addition and subtraction.
    PREC_FACTOR,     ///< Multiplication and division.
    PREC_POWER,      ///< Exponentiation (**).
    PREC_UNARY,      ///< Unary operators.
    PREC_CALL,       ///< Function calls.
    PREC_PRIMARY     ///< Primary expressions.
} Precedence;

// Main entry points
// Forward declarations
struct ParserContext;
struct MoveState;
typedef struct ParserContext ParserContext;

/**
 * @brief Attributes for a declaration (e.g., @packed, @cfg).
 */
typedef struct DeclarationAttributes
{
    int is_packed;
    int align;
    char *cfg_condition;
    int vector_size;
    int cuda_global;
    int cuda_device;
    int cuda_host;
    int is_pure;
    int is_required;
    int is_deprecated;
    char *deprecated_msg;
    int is_inline;
    int is_noinline;
    int is_noreturn;
    int is_cold;
    int is_hot;
    int is_constructor;
    int is_destructor;
    int is_unused;
    int is_weak;
    int is_export;
    int is_thread_local;
    int is_comptime;
    char *section;
    Attribute *custom_attributes;
    char **derived_traits;
    int derived_count;
    char *link_name;
    char *crepr_c_type; // @crepr("C.kind.name") attribute
    char *link_path;    // @link("path/to/file.c")
} DeclarationAttributes;

/**
 * @brief Parses attributes (prefixed with @).
 */
DeclarationAttributes parse_attributes(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a program.
 */
ASTNode *parse_program(ParserContext *ctx, Lexer *l);

extern char *curr_func_ret;
extern ParserContext *token_parser_ctx;

#include "ast/symbols.h"

/**
 * @brief Registry entry for a function signature.
 *
 * Stores metadata about declared functions for type checking and call validation.
 */
typedef struct FuncSig
{
    char *name;           ///< Function name.
    Token decl_token;     ///< declaration token.
    int total_args;       ///< Total argument count.
    char **defaults;      ///< Default values for arguments (or NULL).
    Type **arg_types;     ///< Argument types.
    Type *ret_type;       ///< Return type.
    int is_varargs;       ///< 1 if variadic.
    int is_async;         ///< 1 if async.
    int required;         ///< 1 if return value must be used.
    int is_pure;          ///< 1 if marked @pure.
    char *link_name;      ///< Overriding C identifier (from @link_name).
    int elide_from_idx;   ///< Index of parameter for lifetime elision (-1 if none)
    struct FuncSig *next; ///< Next function in registry.
} FuncSig;

/**
 * @brief Tracks a lambda (anonymous function) within the parser.
 */
typedef struct LambdaRef
{
    ASTNode *node; ///< The AST node for the lambda.
    struct LambdaRef *next;
} LambdaRef;

/**
 * @brief Template for a generic struct.
 */
typedef struct GenericTemplate
{
    char *name;           ///< Template name.
    ASTNode *struct_node; ///< The struct AST node (containing generic params).
    struct GenericTemplate *next;
} GenericTemplate;

/**
 * @brief Template for a generic function.
 */
typedef struct GenericFuncTemplate
{
    char *name;          ///< Template name.
    char *generic_param; ///< Generic parameters string (legacy).
    ASTNode *func_node;  ///< The function AST node.
    struct GenericFuncTemplate *next;
} GenericFuncTemplate;

/**
 * @brief Template for a generic implementation block.
 */
typedef struct GenericImplTemplate
{
    char *struct_name;   ///< Target struct name.
    char *generic_param; ///< Generic parameters.
    ASTNode *impl_node;  ///< The impl block AST node.
    struct GenericImplTemplate *next;
} GenericImplTemplate;

// ---------------------------------------------------------------------------
// zmap.h allocator overrides & type registration (must precede zmap.h include)
// ---------------------------------------------------------------------------
#define ZMAP_MALLOC(sz) xmalloc(sz)
#define ZMAP_CALLOC(n, sz) xcalloc(n, sz)
#define ZMAP_FREE(p) ((void)(p))

static inline uint32_t zmap_hash_cstr(const char *k, uint32_t seed)
{
    // FNV-1a inline (avoids dependency on ZMAP_HASH_STR being defined yet)
    uint32_t hash = 2166136261u ^ seed;
    const uint8_t *data = (const uint8_t *)k;
    while (*data)
    {
        hash ^= *data++;
        hash *= 16777619;
    }
    return hash;
}
static inline int zmap_cmp_cstr(const char *a, const char *b)
{
    return strcmp(a, b);
}
// ---------------------------------------------------------------------------

/**
 * @brief Tracks a concrete instantiation of a generic template.
 */
typedef struct Instantiation
{
    char *name;           ///< Mangled name of the instantiation (e.g. "Vec_int").
    char *template_name;  ///< Original template name (e.g. "Vec").
    char *concrete_arg;   ///< Concrete type argument string.
    char *unmangled_arg;  ///< Unmangled argument for substitution code.
    ASTNode *struct_node; ///< The AST node of the instantiated struct.
    struct Instantiation *next;
} Instantiation;

/**
 * @brief Reference to a parsed struct (list node).
 */
typedef struct StructRef
{
    ASTNode *node;
    struct StructRef *next;
} StructRef;

/**
 * @brief Definition of a struct (lookup cache).
 */
typedef struct StructDef
{
    char *name;
    ASTNode *node;
    struct StructDef *next;
} StructDef;

// Hash table entry for fast struct/enum lookup (used by find_struct_def).
enum
{
    STRUCT_HASH_SIZE = 2048
};
typedef struct
{
    char name[256]; // empty string = empty slot (avoids dangling stack pointer issues with ASAN)
    ASTNode *node;
} StructHashEntry;

/**
 * @brief Track used slice types for generation.
 */
typedef struct SliceType
{
    char *name;
    struct SliceType *next;
} SliceType;

/**
 * @brief Track used tuple signatures for generation.
 */
typedef struct TupleType
{
    char *sig;    ///< Signature string for dedup (e.g. "int__string").
    char **types; ///< Individual field type names for codegen.
    int count;    ///< Number of fields.
    struct TupleType *next;
} TupleType;

/**
 * @brief Registry of enum variants.
 */
typedef struct EnumVariantReg
{
    char *enum_name;    ///< Name of the enum.
    char *variant_name; ///< Name of the variant.
    char *mangled_name; ///< Mangled name (Enum__Variant).
    int tag_id;         ///< Integration tag value.
    struct EnumVariantReg *next;
} EnumVariantReg;

/**
 * @brief Functions marked as deprecated.
 */
typedef struct DeprecatedFunc
{
    char *name;
    char *reason; ///< Optional reason for deprecation.
    struct DeprecatedFunc *next;
} DeprecatedFunc;

/**
 * @brief Represents a module (namespace/file).
 */
typedef struct Module
{
    char *alias;      ///< Import alias (or default name).
    char *path;       ///< File path.
    char *base_name;  ///< Base name of the module.
    int is_c_header;  ///< 1 if this is a C header import.
    int is_re_export; ///< 1 if this module was re-exported.
} Module;

/**
 * @brief Symbol imported via selective import (import { X }).
 */
typedef struct SelectiveImport
{
    char *symbol;        ///< Symbol name.
    char *alias;         ///< Local alias.
    char *source_module; ///< Origin module.
} SelectiveImport;

/**
 * @brief Registry for trait implementations.
 */
typedef struct ImplReg
{
    char *trait; ///< Trait name.
    char *strct; ///< Implementing struct name.
    struct ImplReg *next;
} ImplReg;

/**
 * @brief Loaded compiler plugin.
 */
typedef struct ImportedPlugin
{
    char *name;  ///< Plugin name (e.g., "brainfuck").
    char *alias; ///< Optional usage alias.
} ImportedPlugin;

/**
 * @brief Type alias definition.
 */
typedef struct TypeAlias
{
    char *alias;         ///< New type name.
    char *original_type; ///< Original type.
    Type *type_info;     ///< Parsed original type.
    struct TypeAlias *next;
    int is_opaque;
    char *defined_in_file;
} TypeAlias;

// zmap type registration (generates type-safe hash maps)
#define REGISTER_ZMAP_TYPES(X)                                                                     \
    X(const char *, Module *, ModMap)                                                              \
    X(const char *, SelectiveImport *, SelMap)                                                     \
    X(const char *, const char *, FileSet)                                                         \
    X(const char *, ImportedPlugin *, PluginMap)

#include "../utils/zmap.h"

/**
 * @brief Module/import state container (extracted from ParserContext god struct).
 */
typedef struct ModuleState
{
    zmap_ModMap modules;           ///< Map: alias → Module*.
    zmap_SelMap selective_imports; ///< Map: symbol → SelectiveImport*.
    char *current_module_prefix;   ///< Prefix for current module (namespacing).
    zmap_FileSet imported_files;   ///< Set: imported file paths (key=value).
    zmap_FileSet
        currently_parsing; ///< Set: files whose import is in progress (for cycle detection).
    zmap_PluginMap imported_plugins; ///< Map: alias → ImportedPlugin*.
    zmap_FileSet wildcard_imports;   ///< Set: module base names imported via `as *`.
} ModuleState;

/* * Initialize all maps in a ModuleState. Call once after zeroing the struct. */
static inline void module_state_init(ModuleState *ms)
{
    ms->modules = zmap_init(ModMap, zmap_hash_cstr, zmap_cmp_cstr);
    ms->selective_imports = zmap_init(SelMap, zmap_hash_cstr, zmap_cmp_cstr);
    ms->imported_files = zmap_init(FileSet, zmap_hash_cstr, zmap_cmp_cstr);
    ms->imported_plugins = zmap_init(PluginMap, zmap_hash_cstr, zmap_cmp_cstr);
    ms->currently_parsing = zmap_init(FileSet, zmap_hash_cstr, zmap_cmp_cstr);
    ms->wildcard_imports = zmap_init(FileSet, zmap_hash_cstr, zmap_cmp_cstr);
}

/**
 * @brief Global compilation state and symbol table.
 *
 * ParserContext maintains the state of the compiler during parsing and analysis.
 * It holds symbol tables, type definitions, function registries, and configuration.
 */
struct ParserContext
{
    ZenCompiler *compiler;  ///< Reference to the unified compiler state.
    CompilerConfig *config; ///< Shortcut to compiler->config.
    int recursion_depth;    ///< Guard against stack overflow.
    Scope *global_scope;    ///< Root of the unified symbol table.
    Scope *current_scope;   ///< Current lexical scope for variable lookup.
    FuncSig *func_registry; ///< Registry of declared function signatures (DEPRECATED: moved to
                            ///< global_scope).

    // Lambdas
    LambdaRef *global_lambdas; ///< List of all lambdas generated during parsing.
    int lambda_counter;        ///< Counter for generating unique lambda IDs.
    int fstring_counter;       ///< Counter for generating unique f-string IDs.

// Generics
#define MAX_KNOWN_GENERICS 1024
    char
        *known_generics[MAX_KNOWN_GENERICS]; ///< Stack of currently active generic type parameters.
    int known_generics_count;                ///< Count of active generic parameters.
    GenericTemplate *templates;              ///< Struct generic templates.
    GenericFuncTemplate *func_templates;     ///< Function generic templates.
    GenericImplTemplate *impl_templates;     ///< Implementation block templates.

    // Instantiations
    Instantiation *instantiations; ///< Cache of instantiated generic types.
    ASTNode *instantiated_structs; ///< List of AST nodes for instantiated structs.
    ASTNode *instantiated_funcs;   ///< List of AST nodes for instantiated functions.

    // Structs/Enums
    StructRef *parsed_structs_list;                ///< List of all parsed struct nodes.
    StructHashEntry struct_hash[STRUCT_HASH_SIZE]; ///< Hash table for fast struct/enum lookup.
    StructRef *parsed_enums_list;                  ///< List of all parsed enum nodes.
    StructRef *parsed_funcs_list;                  ///< List of all parsed function nodes.
    StructRef *parsed_impls_list;                  ///< List of all parsed impl blocks.
    StructRef *parsed_globals_list;                ///< List of all parsed global variables.
    StructDef *struct_defs;        ///< Registry of struct definitions (map name -> node).
    EnumVariantReg *enum_variants; ///< Registry of enum variants for global lookup.
    ImplReg *registered_impls;     ///< Cache of type/trait implementations.

    // Types
    SliceType *used_slices;  ///< Cache of generated slice types.
    TupleType *used_tuples;  ///< Cache of generated tuple types.
    TypeAlias *type_aliases; ///< Defined type aliases.

    // Modules/Imports
    ModuleState imports;
    const char *current_filename; ///< Current file being parsed (for token/file attribution).

    // Config/State
    char *current_impl_struct;     ///< Name of struct currently being implemented (in impl block).
    ASTNode *current_impl_methods; ///< Head of method list for current impl block.
    int in_method_with_self;       ///< 1 if parsing body of method with self parameter.
    int self_is_pointer;           ///< 1 if self is a pointer receiver (self*).

    // Internal tracking
    DeprecatedFunc *deprecated_funcs; ///< Registry of deprecated functions.

    // LSP / Fault Tolerance
    int is_fault_tolerant; ///< 1 if parser should recover from errors (LSP mode).
    int suppress_errors;   ///< 1 to swallow parser errors silently (probe parses).
    int had_error; ///< Set by zpanic_at when fault-tolerant; checked by parser loops to bail out.
    void *error_callback_data;                              ///< User data for error callback.
    void (*on_error)(void *data, Token t, const char *msg); ///< Callback for reporting errors.
    void (*on_diagnostic)(void *data, Token t, int severity, const char *msg,
                          int diag_id); ///< Unified diagnostic callback

    // LSP: Flat symbol list (persists after parsing for LSP queries)
    ZenSymbol *all_symbols; ///< comprehensive list of all symbols seen.

    // External C interop: suppress undefined warnings for external symbols
    int has_external_includes;
    int is_comptime;         // Flag for comptime execution context
    char **extern_symbols;   ///< Explicitly declared extern symbols.
    int extern_symbol_count; ///< Count of external symbols.

    // Codegen context (mutable state during code generation):
    struct
    {
        Emitter emitter;    ///< Emitter for code generation.
        FILE *hoist_out;    ///< File stream for hoisting code (e.g. from plugins).
        int skip_preamble;  ///< If 1, codegen won't emit standard preamble (includes etc).
        int is_repl;        ///< 1 if running in REPL mode.
        int has_async;      ///< 1 if async/await features are used in the program.
        int in_defer_block; ///< 1 if currently parsing inside a defer block.

        ASTNode *global_user_structs; ///< List of user defined structs.
        char *current_impl_type;      ///< Type currently being implemented (in impl block).
        int tmp_counter;              ///< Counter for temporary variables.
        int in_async_body;            ///< 1 if currently generating async poll body.
        ASTNode *defer_stack[1024];   ///< Stack of deferred nodes (max 1024).
        int defer_count;              ///< Counter for defer statements in current scope.
        ASTNode *current_lambda;      ///< Current lambda being generated.
        char *current_func_ret_type;  ///< Return type of current function.
        Type *current_func_ret_type_info;
        char *expected_init_type;      ///< Expected type for initializer (from let declaration).
        int loop_defer_boundary[64];   ///< Defer stack index at start of each loop (max 64).
        int loop_depth;                ///< Current loop nesting depth.
        int func_defer_boundary;       ///< Defer stack index at function entry.
        int pending_closure_frees[64]; ///< Lambda IDs whose ctx needs freeing (max 64).
        int pending_closure_free_count;
    } cg;

    // Type Validation
    struct TypeUsage *pending_type_validations; ///< List of types to validate after parsing.
    int is_speculative;     ///< Flag to suppress side effects during speculative parsing.
    int silent_warnings;    ///< Suppress warnings (e.g., during codegen interpolation).
    char *last_doc_comment; ///< Last seen doc-comment (for association).

    // Flow Analysis (Move Semantics)
    struct MoveState *move_state;

    // Registry of traits (encapsulated)
    TraitReg *registered_traits;

    // Extensibility hooks — function pointers for optional modules.
    // When NULL, the corresponding feature is disabled/no-op.
    // Set by driver.c at startup based on configuration.

    /// Hook: identifier collision check (MISRA Rule 5.1/5.2)
    void (*hook_check_identifier_collision)(Token tok, const char *name1, const char *name2,
                                            int limit);

    /// Hook: preprocessor expression check (MISRA Rule 20.8/20.9)
    void (*hook_check_preprocessor_expr)(struct ParserContext *ctx, Token tok,
                                         const char *expression);

    /// Hook: standard macro name check (MISRA Rule 5.10)
    void (*hook_check_standard_macro_name)(Token tok, const char *name);

    /// Hook: plugin lookup
    void *(*hook_find_plugin)(const char *name);

    /// Hook: plugin API initialization
    void (*hook_plugin_init_api)(void *api, const char *filename, int line,
                                 struct CompilerConfig *cfg);

    /// Hook: zen fact trigger
    int (*hook_zen_trigger)(int t, Token location, struct CompilerConfig *cfg);
};

// Intrusive linked-list iteration utilities
// zlist.h (src/utils/zlist.h) provides full node-based doubly-linked lists.
// These macros are for the compiler's existing intrusive singly-linked lists
// where the `next` pointer is embedded directly in the struct.
#define LIST_FOR_EACH(head, cursor, next_field)                                                    \
    for (cursor = (head); cursor; cursor = cursor->next_field)

#define LIST_FOR_EACH_SAFE(head, cursor, tmp, next_field)                                          \
    for (cursor = (head), tmp = cursor ? cursor->next_field : NULL; cursor;                        \
         cursor = tmp, tmp = cursor ? cursor->next_field : NULL)

// Recursion Safety
enum
{
    MAX_RECURSION_DEPTH = 1024
};

#define RECURSION_GUARD(ctx, l, ret)                                                               \
    if (++((ctx)->recursion_depth) > MAX_RECURSION_DEPTH)                                          \
    {                                                                                              \
        zpanic_at(lexer_peek(l), "Recursion limit exceeded");                                      \
        (ctx)->recursion_depth--;                                                                  \
        return ret;                                                                                \
    }

#define RECURSION_GUARD_TOKEN(ctx, tok, ret)                                                       \
    if (++((ctx)->recursion_depth) > MAX_RECURSION_DEPTH)                                          \
    {                                                                                              \
        zpanic_at(tok, "Recursion limit exceeded");                                                \
        (ctx)->recursion_depth--;                                                                  \
        return ret;                                                                                \
    }

#define RECURSION_EXIT(ctx) ((ctx)->recursion_depth)--

// Break out of a parsing loop when EOF is reached unexpectedly.
// Place after the closing-delimiter checks in every while(1) token loop.
#define TOK_EOF_GUARD(tok)                                                                         \
    if ((tok).kind == TOK_EOF)                                                                     \
    {                                                                                              \
        zpanic_at(tok, "Unexpected end of file");                                                  \
        break;                                                                                     \
    }

#define ATTACH_DOC_COMMENT(ctx, node)                                                              \
    if ((node) && (ctx)->last_doc_comment)                                                         \
    {                                                                                              \
        (node)->doc_comment = (ctx)->last_doc_comment;                                             \
        (ctx)->last_doc_comment = NULL;                                                            \
    }

typedef struct TypeUsage
{
    char *name;
    Token location;
    struct TypeUsage *next;
} TypeUsage;

// Type validation prototypes

/**
 * @brief Registers a type usage.
 */
void register_type_usage(ParserContext *ctx, const char *name, Token t);

/**
 * @brief Validates types.
 */
int validate_types(ParserContext *ctx);

/**
 * @brief Traverses all parsed structs and propagates `has_drop` from fields to their parent
 * structs.
 */
void propagate_drop_traits(ParserContext *ctx);
void fix_type_refs_has_drop(ParserContext *ctx);

/**
 * @brief Propagates inner types for vector types (SIMD).
 */
void propagate_vector_inner_types(ParserContext *ctx);

// Token helpers

/**
 * @brief Duplicates a token.
 */
char *token_strdup(Token t);

/**
 * @brief Checks if a token matches a string.
 */
int is_token(Token t, const char *s);

/**
 * @brief Expects a token of a specific type.
 */
Token z_parse_expect(Lexer *l, ZenTokenType type, const char *msg);

/**
 * @brief Skips comments in the lexer.
 */
void skip_comments(Lexer *l);
void token_set_parser_ctx(ParserContext *ctx);

/**
 * @brief Consumes tokens until a semicolon is found.
 */
char *consume_until_semicolon(Lexer *l);

/**
 * @brief Consumes and rewrites tokens.
 */

// C reserved word warnings

/**
 * @brief Checks if a name is a C reserved word.
 */
int is_c_reserved_word(const char *name);

/**
 * @brief Warns about a C reserved word.
 */
void warn_c_reserved_word(Token t, const char *name);

// ZenSymbol table

/**
 * @brief Enters a new scope (pushes to scope stack).
 */
/**
 * @brief Checks if a character is valid in an identifier (isalnum or underscore).
 */
int is_ident_char(char c);

void enter_scope(ParserContext *ctx);

/**
 * @brief Exits the current scope (pops from scope stack).
 */
void exit_scope(ParserContext *ctx);

/**
 * @brief Adds a symbol to the current scope.
 */
void add_symbol(ParserContext *ctx, const char *n, const char *t, Type *type_info, int is_export);

/**
 * @brief Adds a symbol with definition token location.
 */
void add_symbol_with_token(ParserContext *ctx, const char *n, const char *t, Type *type_info,
                           Token tok, int is_export);

/**
 * @brief Finds a symbol's type information.
 */
Type *find_symbol_type_info(ParserContext *ctx, const char *n);

/**
 * @brief Finds a symbol's type.
 */
char *find_symbol_type(ParserContext *ctx, const char *n);

/**
 * @brief Finds a symbol's entry.
 */
ZenSymbol *find_symbol_entry(ParserContext *ctx, const char *n);

/**
 * @brief Finds a symbol in all scopes.
 */
ZenSymbol *find_symbol_in_all(ParserContext *ctx, const char *n);
char *find_similar_symbol(ParserContext *ctx, const char *name);
char *find_method_owner_type_scoped(ParserContext *ctx, const char *struct_name,
                                    const char *method_name);

/**
 * @brief Normalizes a type name (e.g., "int" -> "int32_t").
 */
const char *normalize_type_name(const char *name);

// Function registry

/**
 * @brief Registers a function.
 */
void register_func(ParserContext *ctx, Scope *scope, const char *name, int count, char **defaults,
                   Type **arg_types, Type *ret_type, int is_varargs, int is_async, int is_pure,
                   const char *link_name, Token decl_token, int is_export);

/**
 * @brief Registers a function template.
 */
void register_func_template(ParserContext *ctx, const char *name, const char *param, ASTNode *node);

/**
 * @brief Finds a function template.
 */
GenericFuncTemplate *find_func_template(ParserContext *ctx, const char *name);

// Generic/template helpers
/**
 * @brief Registers a known generic type parameter.
 */
void register_generic(ParserContext *ctx, char *name);

/**
 * @brief Checks if a name is a known generic parameter.
 */
int is_known_generic(ParserContext *ctx, char *name);

/**
 * @brief Checks if a type name string depends on any known generic parameters.
 * (e.g. "T*" returns 1 if T is a known generic).
 */
int is_generic_dependent_str(ParserContext *ctx, const char *type_str);

/**
 * @brief Checks if a name is a primitive type.
 */
int is_primitive_type_name(const char *name);

/**
 * @brief Maps a primitive type name string to its `TypeKind` enum.
 */
TypeKind get_primitive_type_kind(const char *name);

/**
 * @brief Registers an implementation template.
 */
void register_impl_template(ParserContext *ctx, const char *sname, const char *param,
                            ASTNode *node);
/**
 * @brief Adds a struct to the list.
 */
void add_to_struct_list(ParserContext *ctx, ASTNode *node);

/**
 * @brief Adds an enum to the list.
 */
void add_to_enum_list(ParserContext *ctx, ASTNode *node);

/**
 * @brief Adds a function to the list.
 */
void add_to_func_list(ParserContext *ctx, ASTNode *node);

/**
 * @brief Adds an implementation to the list.
 */
void add_to_impl_list(ParserContext *ctx, ASTNode *node);

/**
 * @brief Adds a global to the list.
 */
void add_to_global_list(ParserContext *ctx, ASTNode *node);

/**
 * @brief Synchronizes linkage overrides across all type references in the AST.
 */
void sync_all_link_names(ParserContext *ctx, ASTNode *root);

/**
 * @brief Registers built-in types and functions.
 */
void register_builtins(ParserContext *ctx);
void register_comptime_builtins(ParserContext *ctx);
char *sanitize_mangled_name(const char *s);
void register_tuple_with_types(ParserContext *ctx, const char *sig, const char **types, int count);
ASTNode *parse_tuple_expression(ParserContext *ctx, Lexer *l, const char *type_name, ASTNode *expr);

/** @brief Parses just the comptime block body, returning the statement list. */
ASTNode *parse_comptime_body(ParserContext *ctx, Lexer *l);

/**
 * @brief Patches self arguments in a function.
 */
char *patch_self_args(const char *args, const char *struct_name);
char *escape_c_string(const char *input);
char *replace_type_str(const char *src, const char *param, const char *concrete,
                       const char *old_struct, const char *new_struct);
char *resolve_struct_name_from_type(ParserContext *ctx, Type *t, int *is_ptr_out,
                                    char **allocated_out);
char *process_printf_sugar(ParserContext *ctx, Token srctoken, const char *content, int newline,
                           const char *target, char ***used_syms, int *count, int check_symbols,
                           int is_raw, int is_expr);

/**
 * @brief Checks if a token is a reserved keyword.
 */
int is_reserved_keyword(Token t);

/**
 * @brief Checks if an identifier is valid (not a keyword).
 */
void check_identifier(Token t);

/**
 * @brief Main loop to parse top-level nodes in a file.
 */
ASTNode *parse_program_nodes(ParserContext *ctx, Lexer *l);

/**
 * @brief Collapses runs of four or more underscores into a double underscore.
 *        A run of exactly three is preserved: it encodes the "__" separator
 *        plus an identifier that begins with "_" (e.g. `Struct::_method`), so
 *        collapsing it would make distinct symbols collide.
 */
char *merge_underscores(const char *name);

// --- Parser function declarations (needed to avoid implicit declarations) ---
ASTNode *parse_statement(ParserContext *ctx, Lexer *l);
ASTNode *parse_block(ParserContext *ctx, Lexer *l);
ASTNode *parse_expression(ParserContext *ctx, Lexer *l);
ASTNode *parse_expr_prec(ParserContext *ctx, Lexer *l, Precedence min_prec);
ASTNode *parse_return(ParserContext *ctx, Lexer *l);
ASTNode *parse_assert(ParserContext *ctx, Lexer *l);
ASTNode *parse_expect(ParserContext *ctx, Lexer *l);
ASTNode *parse_plugin(ParserContext *ctx, Lexer *l, Token tk);
char *token_get_string_content(Token t);
ASTNode *find_struct_def(ParserContext *ctx, const char *name);
ASTNode *parse_function(ParserContext *ctx, Lexer *l, int is_async, int is_extern,
                        const char *link_name, int is_export);
ASTNode *parse_struct(ParserContext *ctx, Lexer *l, int is_union, int is_packed, int align,
                      const char *link_name, int is_export);
ASTNode *parse_enum(ParserContext *ctx, Lexer *l, const char *link_name, int is_export);
ASTNode *parse_impl(ParserContext *ctx, Lexer *l);
ASTNode *parse_trait(ParserContext *ctx, Lexer *l);
ASTNode *parse_include(ParserContext *ctx, Lexer *l);
ASTNode *parse_import(ParserContext *ctx, Lexer *l, int is_re_export);
ASTNode *parse_def(ParserContext *ctx, Lexer *l, int is_export);
ASTNode *parse_test(ParserContext *ctx, Lexer *l);
ASTNode *parse_var_decl(ParserContext *ctx, Lexer *l, int is_export);
ASTNode *parse_type_alias(ParserContext *ctx, Lexer *l, int is_opaque, int is_export);
char *parse_and_convert_args(ParserContext *ctx, Lexer *l, char ***defaults,
                             ASTNode ***default_values, int *count, Type ***arg_types,
                             char ***param_names, int *is_varargs, char ***ctype_overrides);
Type *parse_type_formal(ParserContext *ctx, Lexer *l);
Type *type_from_string_helper(const char *c);
FuncSig *find_func(ParserContext *ctx, const char *name);
EnumVariantReg *find_enum_variant(ParserContext *ctx, const char *name);
TypeAlias *find_type_alias_node(ParserContext *ctx, const char *name);
char *extract_module_name(const char *path);
ASTNode *transform_to_trait_object(ParserContext *ctx, const char *target_trait, ASTNode *expr);
int check_impl(ParserContext *ctx, const char *trait_name, const char *type_name);
int check_opaque_alias_compat(ParserContext *ctx, Type *a, Type *b);
Module *find_module(ParserContext *ctx, const char *alias);
SelectiveImport *find_selective_import(ParserContext *ctx, const char *name);
void re_export_wildcard_symbols(ParserContext *ctx, const char *module_base);
void re_export_propagated(ParserContext *ctx, const char *alias, const char *parent_prefix,
                          const char *base_name);
ASTNode *find_concrete_struct_def(ParserContext *ctx, const char *name);
const char *find_type_alias(ParserContext *ctx, const char *alias);
Type *parse_type_base(ParserContext *ctx, Lexer *l);
char *parse_type(ParserContext *ctx, Lexer *l);
ASTNode *find_trait_def(ParserContext *ctx, const char *name);
int is_extern_symbol(ParserContext *ctx, const char *name);
int is_file_imported(ParserContext *ctx, const char *path);
void mark_file_imported(ParserContext *ctx, const char *path);
int should_suppress_undef_warning(ParserContext *ctx, const char *name);
void init_builtins(void);
void register_extern_symbol(ParserContext *ctx, const char *name);
void register_impl(ParserContext *ctx, const char *trait, const char *type);
void register_struct_def(ParserContext *ctx, const char *name, ASTNode *node);
void register_type_alias(ParserContext *ctx, const char *alias, const char *original, Type *type,
                         int is_opaque, const char *defined_in_file, Token tok, int is_export);
void register_slice(ParserContext *ctx, const char *type);
void register_lambda(ParserContext *ctx, ASTNode *node);
void register_template(ParserContext *ctx, const char *name, ASTNode *node);
void register_enum_variant(ParserContext *ctx, const char *vname, const char *ename, int tag);
void register_plugin(ParserContext *ctx, const char *name, const char *alias);
void register_selective_import(ParserContext *ctx, const char *symbol, const char *alias,
                               const char *source_module);
void register_deprecated_func(ParserContext *ctx, const char *name, const char *replacement);
void try_parse_macro_const(ParserContext *ctx, const char *name);
void parser_audit_preprocessor(ParserContext *ctx, Token t);
char *normalize_raw_content(const char *content);
char *rewrite_expr_methods(ParserContext *ctx, char *raw);
char *parse_condition_raw(ParserContext *ctx, Lexer *l);
const char *resolve_plugin(ParserContext *ctx, const char *name_or_alias);
char *parse_array_literal(ParserContext *ctx, Lexer *l, const char *st);
char *parse_tuple_literal(ParserContext *ctx, Lexer *l, const char *tn);
ASTNode *parse_embed(ParserContext *ctx, Lexer *l);
ASTNode *parse_macro_call(ParserContext *ctx, Lexer *l, char *macro_name);
char *instantiate_function_template(ParserContext *ctx, const char *name, const char *arg1,
                                    const char *arg2);
void instantiate_generic(ParserContext *ctx, const char *name, const char *arg_str,
                         const char *arg_name, Token t);
void instantiate_generic_multi(ParserContext *ctx, const char *name, char **args, int count,
                               Token t);

// stmt/ declarations
ASTNode *parse_match(ParserContext *ctx, Lexer *l);
ASTNode *parse_loop(ParserContext *ctx, Lexer *l);
ASTNode *parse_repeat(ParserContext *ctx, Lexer *l);
ASTNode *parse_unless(ParserContext *ctx, Lexer *l);
ASTNode *parse_guard(ParserContext *ctx, Lexer *l);
ASTNode *parse_defer(ParserContext *ctx, Lexer *l);
ASTNode *parse_asm(ParserContext *ctx, Lexer *l);
ASTNode *parse_if(ParserContext *ctx, Lexer *l);
ASTNode *parse_while(ParserContext *ctx, Lexer *l);
ASTNode *parse_for(ParserContext *ctx, Lexer *l);

// utils/ declarations
void audit_section_5(ParserContext *ctx, Scope *scope, const char *name, const char *link_name,
                     Token tok);
void instantiate_methods(ParserContext *ctx, GenericImplTemplate *it,
                         const char *mangled_struct_name, const char *arg,
                         const char *unmangled_arg);
const char *get_closest_type_hint(ParserContext *ctx, const char *name);
void add_instantiated_func(ParserContext *ctx, ASTNode *fn);
char *ast_to_string(ASTNode *node);
void struct_hash_insert(ParserContext *ctx, const char *name, ASTNode *node);
void register_symbol_to_lsp(ParserContext *ctx, ZenSymbol *s);

// core/ declarations
ASTNode *generate_derive_impls(ParserContext *ctx, ASTNode *strct, char **traits, int count);

// decl/ declarations
void replace_it_with_var(ASTNode *node, char *var_name);

// struct/ declarations
void load_std_module(ParserContext *ctx, const char *path);
void auto_import_std_mem(ParserContext *ctx);
char *mangle_method_symbol(const char *struct_name, const char *trait_name,
                           const char *method_name);
void patch_and_fix_self(ParserContext *ctx, ASTNode *f, const char *full_struct_name);
Type *parse_type_obj(ParserContext *ctx, Lexer *l);

// expr/ declarations
ASTNode *parse_primary(ParserContext *ctx, Lexer *l);
Precedence get_token_precedence(Token t);

#endif // PARSER_H
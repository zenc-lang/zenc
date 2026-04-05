
#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "zprep.h"

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
    Attribute *custom_attributes;
    char **derived_traits;
    int derived_count;
} DeclarationAttributes;

/**
 * @brief Parses attributes (prefixed with @).
 */
DeclarationAttributes parse_attributes(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a program.
 */
ASTNode *parse_program(ParserContext *ctx, Lexer *l);

extern ParserContext *g_parser_ctx;

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

/**
 * @brief Represents an imported source file (to prevent cycles/duplication).
 */
typedef struct ImportedFile
{
    char *path; ///< Absolute file path.
    struct ImportedFile *next;
} ImportedFile;

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
    char *sig;
    struct TupleType *next;
} TupleType;

/**
 * @brief Registry of enum variants.
 */
typedef struct EnumVariantReg
{
    char *enum_name;    ///< Name of the enum.
    char *variant_name; ///< Name of the variant.
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
    char *alias;     ///< Import alias (or default name).
    char *path;      ///< File path.
    char *base_name; ///< Base name of the module.
    int is_c_header; ///< 1 if this is a C header import.
    struct Module *next;
} Module;

/**
 * @brief Symbol imported via selective import (import { X }).
 */
typedef struct SelectiveImport
{
    char *symbol;        ///< Symbol name.
    char *alias;         ///< Local alias.
    char *source_module; ///< Origin module.
    struct SelectiveImport *next;
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
    struct ImportedPlugin *next;
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

/**
 * @brief Global compilation state and symbol table.
 *
 * ParserContext maintains the state of the compiler during parsing and analysis.
 * It holds symbol tables, type definitions, function registries, and configuration.
 */
struct ParserContext
{
    Scope *global_scope;    ///< Root of the unified symbol table.
    Scope *current_scope;   ///< Current lexical scope for variable lookup.
    FuncSig *func_registry; ///< Registry of declared function signatures (DEPRECATED: moved to
                            ///< global_scope).

    // Lambdas
    LambdaRef *global_lambdas; ///< List of all lambdas generated during parsing.
    int lambda_counter;        ///< Counter for generating unique lambda IDs.

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
    StructRef *parsed_structs_list; ///< List of all parsed struct nodes.
    StructRef *parsed_enums_list;   ///< List of all parsed enum nodes.
    StructRef *parsed_funcs_list;   ///< List of all parsed function nodes.
    StructRef *parsed_impls_list;   ///< List of all parsed impl blocks.
    StructRef *parsed_globals_list; ///< List of all parsed global variables.
    StructDef *struct_defs;         ///< Registry of struct definitions (map name -> node).
    EnumVariantReg *enum_variants;  ///< Registry of enum variants for global lookup.
    ImplReg *registered_impls;      ///< Cache of type/trait implementations.

    // Types
    SliceType *used_slices;  ///< Cache of generated slice types.
    TupleType *used_tuples;  ///< Cache of generated tuple types.
    TypeAlias *type_aliases; ///< Defined type aliases.

    // Modules/Imports
    Module *modules;                    ///< List of registered modules.
    SelectiveImport *selective_imports; ///< Symbols imported via `import { ... }`.
    char *current_module_prefix;        ///< Prefix for current module (namespacing).
    ImportedFile *imported_files;       ///< List of files already included/imported.
    ImportedPlugin *imported_plugins;   ///< List of active plugins.

    // Config/State
    char *current_impl_struct;     ///< Name of struct currently being implemented (in impl block).
    ASTNode *current_impl_methods; ///< Head of method list for current impl block.
    int in_method_with_self;       ///< 1 if parsing body of method with self parameter.
    int self_is_pointer;           ///< 1 if self is a pointer receiver (self*).

    // Internal tracking
    DeprecatedFunc *deprecated_funcs; ///< Registry of deprecated functions.

    // LSP / Fault Tolerance
    int is_fault_tolerant;     ///< 1 if parser should recover from errors (LSP mode).
    void *error_callback_data; ///< User data for error callback.
    void (*on_error)(void *data, Token t, const char *msg); ///< Callback for reporting errors.

    // LSP: Flat symbol list (persists after parsing for LSP queries)
    ZenSymbol *all_symbols; ///< comprehensive list of all symbols seen.

    // External C interop: suppress undefined warnings for external symbols
    int has_external_includes; ///< Set when `#include <...>` is used.
    char **extern_symbols;     ///< Explicitly declared extern symbols.
    int extern_symbol_count;   ///< Count of external symbols.

    // Codegen state:
    FILE *hoist_out;    ///< File stream for hoisting code (e.g. from plugins).
    int skip_preamble;  ///< If 1, codegen won't emit standard preamble (includes etc).
    int is_repl;        ///< 1 if running in REPL mode.
    int has_async;      ///< 1 if async/await features are used in the program.
    int in_defer_block; ///< 1 if currently parsing inside a defer block.

    // Type Validation
    struct TypeUsage *pending_type_validations; ///< List of types to validate after parsing.
    int is_speculative;  ///< Flag to suppress side effects during speculative parsing.
    int silent_warnings; ///< Suppress warnings (e.g., during codegen interpolation).

    // Flow Analysis (Move Semantics)
    struct MoveState *move_state;
};

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
Token expect(Lexer *l, ZenTokenType type, const char *msg);

/**
 * @brief Skips comments in the lexer.
 */
void skip_comments(Lexer *l);

/**
 * @brief Consumes tokens until a semicolon is found.
 */
char *consume_until_semicolon(Lexer *l);

/**
 * @brief Consumes and rewrites tokens.
 */
char *consume_and_rewrite(ParserContext *ctx, Lexer *l);

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
void add_symbol(ParserContext *ctx, const char *n, const char *t, Type *type_info);

/**
 * @brief Adds a symbol with definition token location.
 */
void add_symbol_with_token(ParserContext *ctx, const char *n, const char *t, Type *type_info,
                           Token tok);

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
                   Token decl_token);

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
 * @brief Registers built-in types and functions.
 */
void register_builtins(ParserContext *ctx);
void register_comptime_builtins(ParserContext *ctx);

/**
 * @brief Adds an instantiated function to the list.
 */
void add_instantiated_func(ParserContext *ctx, ASTNode *fn);

/**
 * @brief Instantiates a generic struct/function.
 */
void instantiate_generic(ParserContext *ctx, const char *name, const char *concrete_type,
                         const char *unmangled_type, Token t);

/**
 * @brief Instantiates a multi-parameter generic.
 */
void instantiate_generic_multi(ParserContext *ctx, const char *name, char **args, int arg_count,
                               Token t);

/**
 * @brief Sanitizes a mangled name for use in codegen.
 */
char *sanitize_mangled_name(const char *name);

/**
 * @brief Registers a type alias.
 */
TypeAlias *find_type_alias_node(ParserContext *ctx, const char *name);
void register_type_alias(ParserContext *ctx, const char *alias, const char *original,
                         Type *type_info, int is_opaque, const char *defined_in_file);

/**
 * @brief Registers an implementation.
 */
void register_impl(ParserContext *ctx, const char *trait, const char *strct);

/**
 * @brief Checks if a type implements a trait.
 */
int check_impl(ParserContext *ctx, const char *trait, const char *strct);

/**
 * @brief Registers a template.
 */
void register_template(ParserContext *ctx, const char *name, ASTNode *node);

/**
 * @brief Registers a deprecated function.
 */
void register_deprecated_func(ParserContext *ctx, const char *name, const char *reason);

/**
 * @brief Finds a deprecated function.
 */
DeprecatedFunc *find_deprecated_func(ParserContext *ctx, const char *name);

/**
 * @brief Parses a single parameter arrow lambda.
 */
ASTNode *parse_arrow_lambda_single(ParserContext *ctx, Lexer *l, char *param_name,
                                   int default_capture_mode);

/**
 * @brief Parses a multi-parameter arrow lambda.
 */
ASTNode *parse_arrow_lambda_multi(ParserContext *ctx, Lexer *l, char **param_names,
                                  Type **param_types, int num_params, int default_capture_mode);

// Utils

/**
 * @brief Parses and converts arguments.
 */
char *parse_and_convert_args(ParserContext *ctx, Lexer *l, char ***defaults_out,
                             ASTNode ***default_values_out, int *count_out, Type ***types_out,
                             char ***names_out, int *is_varargs_out, char ***ctype_overrides_out);

/**
 * @brief Scan build directives.
 */
void scan_build_directives(struct ParserContext *ctx, const char *src);

/**
 * @brief Attempt to parse a #define macro as a constant integer.
 * Used for simple macros in headers to allow array sizes etc.
 */
void try_parse_macro_const(struct ParserContext *ctx, const char *content);

/**
 * @brief Scan a C header line for function prototypes.
 * Registers discovered function names as extern symbols.
 */
void try_parse_c_function_decl(struct ParserContext *ctx, const char *line);

/**
 * @brief Scan a C header line for struct/union declarations.
 * Registers discovered type names as opaque type aliases.
 */
void try_parse_c_struct_decl(struct ParserContext *ctx, const char *line);

/**
 * @brief Recursively scan a C header file for declarations.
 * Follows nested #include "..." directives and extracts macros,
 * function prototypes, and struct/union declarations.
 */
void scan_c_header_contents(struct ParserContext *ctx, const char *path, int depth);

/**
 * @brief Checks if a file has been imported.
 */
int is_file_imported(ParserContext *ctx, const char *path);

/**
 * @brief Marks a file as imported.
 */
void mark_file_imported(ParserContext *ctx, const char *path);

/**
 * @brief Registers a plugin.
 */
void register_plugin(ParserContext *ctx, const char *name, const char *alias);

/**
 * @brief Resolves a plugin by name or alias.
 */
const char *resolve_plugin(ParserContext *ctx, const char *name_or_alias);

/**
 * @brief Prints type definitions to a file.
 */
void print_type_defs(ParserContext *ctx, FILE *out, ASTNode *nodes);

// String manipulation

/**
 * @brief Replaces a substring in a string.
 */
char *replace_in_string(const char *src, const char *old_w, const char *new_w);

/**
 * @brief Replaces a type string in a string.
 */
char *replace_type_str(const char *src, const char *param, const char *concrete,
                       const char *old_struct, const char *new_struct);

/**
 * @brief Replaces a type formal in a type.
 */
Type *replace_type_formal(Type *t, const char *p, const char *c, const char *os, const char *ns);

/**
 * @brief Copies an AST node and replaces its type parameters.
 */
ASTNode *copy_ast_replacing(ASTNode *n, const char *p, const char *c, const char *os,
                            const char *ns);
char *extract_module_name(const char *path);

// Enum helpers
/**
 * @brief Registers an enum variant.
 */
void register_enum_variant(ParserContext *ctx, const char *ename, const char *vname, int tag);

/**
 * @brief Finds an enum variant.
 */
EnumVariantReg *find_enum_variant(ParserContext *ctx, const char *vname);

// Lambda helpers
/**
 * @brief Registers a lambda.
 */
void register_lambda(ParserContext *ctx, ASTNode *node);

/**
 * @brief Analyzes lambda captures.
 */
void analyze_lambda_captures(ParserContext *ctx, ASTNode *lambda);

// Type registration
/**
 * @brief Registers a slice type.
 */
void register_slice(ParserContext *ctx, const char *type);

/**
 * @brief Registers a tuple type.
 */
void register_tuple(ParserContext *ctx, const char *sig);

// Struct lookup
/**
 * @brief Finds a struct definition.
 */
ASTNode *find_struct_def(ParserContext *ctx, const char *name);
ASTNode *find_trait_def(ParserContext *ctx, const char *name);

/**
 * @brief Finds an already-registered concrete struct definition.
 */
ASTNode *find_concrete_struct_def(ParserContext *ctx, const char *name);

/**
 * @brief Registers a struct definition.
 */
void register_struct_def(ParserContext *ctx, const char *name, ASTNode *node);

// Module system
/**
 * @brief Finds a module.
 */
Module *find_module(ParserContext *ctx, const char *alias);

/**
 * @brief Registers a module.
 */
void register_module(ParserContext *ctx, const char *alias, const char *path);

/**
 * @brief Registers a selective import.
 */
void register_selective_import(ParserContext *ctx, const char *symbol, const char *alias,
                               const char *source_module);

/**
 * @brief Finds a selective import.
 */
SelectiveImport *find_selective_import(ParserContext *ctx, const char *name);

// Type Aliases

/**
 * @brief Finds a type alias.
 */
const char *find_type_alias(ParserContext *ctx, const char *alias);

// External symbol tracking (C interop)
/**
 * @brief Registers an external symbol.
 */
void register_extern_symbol(ParserContext *ctx, const char *name);

/**
 * @brief Checks if a symbol is external.
 */
int is_extern_symbol(ParserContext *ctx, const char *name);

/**
 * @brief Checks if a symbol should suppress an undefined warning.
 */
int should_suppress_undef_warning(ParserContext *ctx, const char *name);

// Initialization
/**
 * @brief Initializes built-in types and symbols.
 */
void init_builtins();

// Expression rewriting
/**
 * @brief Rewrites expression methods.
 */
char *rewrite_expr_methods(ParserContext *ctx, char *raw);

/**
 * @brief Processes a formatted string.
 */
char *process_fstring(ParserContext *ctx, const char *content, char ***used_syms, int *count);

/**
 * @brief Instantiates a function template.
 */
char *instantiate_function_template(ParserContext *ctx, const char *name, const char *concrete_type,
                                    const char *unmangled_type);

/**
 * @brief Finds a function.
 */
FuncSig *find_func(ParserContext *ctx, const char *name);

/**
 * @brief Parses a type formal.
 */
Type *parse_type_formal(ParserContext *ctx, Lexer *l);

/**
 * @brief Checks compatibility of opaque aliases (allows access within defining file).
 */
int check_opaque_alias_compat(ParserContext *ctx, Type *a, Type *b);

/**
 * @brief Parses a type.
 */
char *parse_type(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a type base.
 */
Type *parse_type_base(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an expression.
 */
ASTNode *parse_expression(ParserContext *ctx, Lexer *l);
ASTNode *parse_tuple_expression(ParserContext *ctx, Lexer *l, const char *type_name,
                                ASTNode *first_elem);

/**
 * @brief Parses an expression with minimum precedence.
 */
ASTNode *parse_expr_prec(ParserContext *ctx, Lexer *l, Precedence min_prec);

/**
 * @brief Parses a primary expression (literal, variable, grouping).
 */
ASTNode *parse_primary(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a lambda.
 */
ASTNode *parse_lambda(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a condition.
 */
char *parse_condition_raw(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an array literal.
 */
char *parse_array_literal(ParserContext *ctx, Lexer *l, const char *st);

/**
 * @brief Parses a tuple literal.
 */
char *parse_tuple_literal(ParserContext *ctx, Lexer *l, const char *tn);

/**
 * @brief Parses an embed.
 */
ASTNode *parse_embed(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a macro call.
 */
ASTNode *parse_macro_call(ParserContext *ctx, Lexer *l, char *name);

/**
 * @brief Parses a statement.
 */
ASTNode *parse_statement(ParserContext *ctx, Lexer *l);

/**
 * @brief Normalizes raw block content (strips \r from CRLF sequences).
 */
char *normalize_raw_content(const char *content);

/**
 * @brief Parses a block of statements { ... }.
 */
ASTNode *parse_block(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an if statement.
 */
ASTNode *parse_if(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a while loop.
 */
ASTNode *parse_while(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a for loop.
 */
ASTNode *parse_for(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a loop.
 */
ASTNode *parse_loop(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a repeat loop.
 */
ASTNode *parse_repeat(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an unless statement.
 */
ASTNode *parse_unless(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a guard statement.
 */
ASTNode *parse_guard(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a match statement.
 */
ASTNode *parse_match(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a return statement.
 */
ASTNode *parse_return(ParserContext *ctx, Lexer *l);

/**
 * @brief Processes a formatted string.
 */
char *escape_c_string(const char *input);
char *process_printf_sugar(ParserContext *ctx, Token srctoken, const char *content, int newline,
                           const char *target, char ***used_syms, int *count, int check_symbols);

/**
 * @brief Parses an assert statement.
 */
ASTNode *parse_assert(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a defer statement.
 */
ASTNode *parse_defer(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an asm statement.
 */
ASTNode *parse_asm(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a plugin statement.
 */
ASTNode *parse_plugin(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a variable declaration.
 */
ASTNode *parse_var_decl(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a def statement.
 */
ASTNode *parse_def(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a type alias.
 */
ASTNode *parse_type_alias(ParserContext *ctx, Lexer *l, int is_opaque);

/**
 * @brief Parses a function definition.
 */
ASTNode *parse_function(ParserContext *ctx, Lexer *l, int is_async);

/**
 * @brief Parses a struct definition.
 */
ASTNode *parse_struct(ParserContext *ctx, Lexer *l, int is_union, int is_opaque, int is_extern);

/**
 * @brief Parses an enum definition.
 */
ASTNode *parse_enum(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a trait definition.
 */
ASTNode *parse_trait(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an implementation.
 */
ASTNode *parse_impl(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an implementation trait.
 */
ASTNode *parse_impl_trait(ParserContext *ctx, Lexer *l);

/**
 * @brief Transforms an expression into a trait object (fat pointer).
 */
ASTNode *transform_to_trait_object(ParserContext *ctx, const char *target_trait,
                                   ASTNode *source_expr);

/**
 * @brief Parses a test definition.
 */
ASTNode *parse_test(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an include statement.
 */
ASTNode *parse_include(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses an import statement.
 */
ASTNode *parse_import(ParserContext *ctx, Lexer *l);

/**
 * @brief Parses a comptime statement.
 */
ASTNode *parse_comptime(ParserContext *ctx, Lexer *l);

/**
 * @brief Patches self arguments in a function.
 */
char *patch_self_args(const char *args, const char *struct_name);

/**
 * @brief Checks if a token is a reserved keyword.
 */
int is_reserved_keyword(Token t);

/**
 * @brief Checks if an identifier is valid (not a keyword).
 */
void check_identifier(ParserContext *ctx, Token t);

/**
 * @brief Main loop to parse top-level nodes in a file.
 */
ASTNode *parse_program_nodes(ParserContext *ctx, Lexer *l);

/**
 * @brief Collapses triple or more underscores into a double underscore.
 */
char *merge_underscores(const char *name);

#endif // PARSER_H
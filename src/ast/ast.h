
#ifndef AST_H
#define AST_H

#include "zprep.h"
#include <stdlib.h>

// Forward declarations.
struct ASTNode;
typedef struct ASTNode ASTNode;

// ** Formal Type System **
// Used for Generics, Type Inference, and robust pointer handling.

/**
 * @brief Kind of literal value.
 */
typedef enum
{
    LITERAL_INT = 0,       ///< Integer literal.
    LITERAL_FLOAT = 1,     ///< Floating point literal.
    LITERAL_STRING = 2,    ///< String literal.
    LITERAL_CHAR = 3,      ///< Character literal.
    LITERAL_RAW_STRING = 4 ///< Raw string literal.
} LiteralKind;

/**
 * @brief Formal type system kinds.
 */
typedef enum
{
    TYPE_VOID,   ///< `void` type.
    TYPE_BOOL,   ///< `bool` type.
    TYPE_CHAR,   ///< `char` type.
    TYPE_STRING, ///< `string` type.
    TYPE_U0,     ///< `u0` type.
    TYPE_I8,     ///< `i8` type.
    TYPE_U8,     ///< `u8` type.
    TYPE_I16,    ///< `i16` type.
    TYPE_U16,    ///< `u16` type.
    TYPE_I32,    ///< `i32` type.
    TYPE_U32,    ///< `u32` type.
    TYPE_I64,    ///< `i64` type.
    TYPE_U64,    ///< `u64` type.
    TYPE_I128,   ///< `i128` type.
    TYPE_U128,   ///< `u128` type.
    TYPE_F32,    ///< `f32` type.
    TYPE_F64,    ///< `f64` type.
    TYPE_INT,    ///< `int` (alias, usually i32).
    TYPE_FLOAT,  ///< `float` (alias).
    TYPE_USIZE,  ///< `usize` (pointer size unsigned).
    TYPE_ISIZE,  ///< `isize` (pointer size signed).
    TYPE_BYTE,   ///< `byte`.
    TYPE_RUNE,   ///< `rune`.
    TYPE_UINT,   ///< `uint` (alias).
    // Portable C Types (FFI)
    TYPE_C_INT,        ///< `c_int` (int).
    TYPE_C_UINT,       ///< `c_uint` (unsigned int).
    TYPE_C_LONG,       ///< `c_long` (long).
    TYPE_C_ULONG,      ///< `c_ulong` (unsigned long).
    TYPE_C_LONG_LONG,  ///< `c_long_long` (long long).
    TYPE_C_ULONG_LONG, ///< `c_ulong_long` (unsigned long long).
    TYPE_C_SHORT,      ///< `c_short` (short).
    TYPE_C_USHORT,     ///< `c_ushort` (unsigned short).
    TYPE_C_CHAR,       ///< `c_char` (char).
    TYPE_C_UCHAR,      ///< `c_uchar` (unsigned char).

    TYPE_STRUCT,   ///< Struct type.
    TYPE_ENUM,     ///< Enum type.
    TYPE_POINTER,  ///< Pointer type (*).
    TYPE_ARRAY,    ///< Fixed size array [N].
    TYPE_VECTOR,   ///< SIMD vector type.
    TYPE_FUNCTION, ///< Function pointer or reference.
    TYPE_GENERIC,  ///< Generic type parameter (T).
    TYPE_ALIAS,    ///< Opaque type alias.
    TYPE_BITINT,   ///< C23 _BitInt(N).
    TYPE_UBITINT,  ///< C23 unsigned _BitInt(N).
    TYPE_UNKNOWN   ///< Unknown/unresolved type.
} TypeKind;

/**
 * @brief Represents a formal type in the type system.
 */
typedef struct Type
{
    TypeKind kind;          ///< The kind of type.
    char *name;             ///< Name of the type (for STRUCT, GENERIC, ENUM).
    struct Type *inner;     ///< Inner type (for POINTER, ARRAY).
    struct Type **args;     ///< Generic arguments (for GENERIC instantiations).
    int arg_count;          ///< Count of generic arguments.
    int is_const;           ///< 1 if const-qualified.
    int is_explicit_struct; ///< 1 if defined with "struct" keyword explicitly.
    int is_raw;             // Raw function pointer (fn*)
    int array_size;         ///< Size for fixed-size arrays. For TYPE_BITINT, this is the bit width.
    struct
    {
        int has_drop;     ///< 1 if type implements Drop trait (RAII).
        int has_iterable; ///< 1 if type implements Iterable trait.
    } traits;
    union
    {
        int is_varargs;  ///< 1 if function type is variadic.
        int is_restrict; ///< 1 if pointer is restrict-qualified.
        struct
        {
            int is_opaque_alias;
            char *alias_defined_in_file;
        } alias;
    };
} Type;

// ** AST Node Types **
/**
 * @brief AST Node Types.
 */
typedef enum
{
    NODE_ROOT,               ///< Root of the AST.
    NODE_FUNCTION,           ///< Function definition.
    NODE_BLOCK,              ///< Code block { ... }.
    NODE_RETURN,             ///< Return statement.
    NODE_VAR_DECL,           ///< Variable declaration.
    NODE_CONST,              ///< Constant definition.
    NODE_TYPE_ALIAS,         ///< Type alias (typedef).
    NODE_IF,                 ///< If statement.
    NODE_WHILE,              ///< While loop.
    NODE_FOR,                ///< For loop.
    NODE_FOR_RANGE,          ///< For-range loop (iterator).
    NODE_LOOP,               ///< Infinite loop.
    NODE_REPEAT,             ///< Repeat loop (n times).
    NODE_UNLESS,             ///< Unless statement (if !cond).
    NODE_GUARD,              ///< Guard clause (if !cond return).
    NODE_BREAK,              ///< Break statement.
    NODE_CONTINUE,           ///< Continue statement.
    NODE_MATCH,              ///< Match statement.
    NODE_MATCH_CASE,         ///< Case within match.
    NODE_EXPR_BINARY,        ///< Binary expression (a + b).
    NODE_EXPR_UNARY,         ///< Unary expression (!a).
    NODE_EXPR_LITERAL,       ///< Literal value.
    NODE_EXPR_VAR,           ///< Variable reference.
    NODE_EXPR_CALL,          ///< Function call.
    NODE_EXPR_MEMBER,        ///< Member access (a.b).
    NODE_EXPR_INDEX,         ///< Array index (a[b]).
    NODE_EXPR_CAST,          ///< Type cast.
    NODE_EXPR_SIZEOF,        ///< Sizeof expression.
    NODE_EXPR_STRUCT_INIT,   ///< Struct initializer.
    NODE_EXPR_ARRAY_LITERAL, ///< Array literal.
    NODE_EXPR_TUPLE_LITERAL, ///< Tuple literal.
    NODE_EXPR_SLICE,         ///< Slice operation.
    NODE_STRUCT,             ///< Struct definition.
    NODE_FIELD,              ///< Struct field.
    NODE_ENUM,               ///< Enum definition.
    NODE_ENUM_VARIANT,       ///< Enum variant.
    NODE_TRAIT,              ///< Trait definition.
    NODE_IMPL,               ///< Impl block.
    NODE_IMPL_TRAIT,         ///< Trait implementation.
    NODE_INCLUDE,            ///< Include directive.
    NODE_RAW_STMT,           ///< Raw statement (transpiler bypass).
    NODE_TEST,               ///< Test block.
    NODE_ASSERT,             ///< Assert statement.
    NODE_DEFER,              ///< Defer statement.
    NODE_DESTRUCT_VAR,       ///< Destructuring declaration.
    NODE_TERNARY,            ///< Ternary expression (?:).
    NODE_ASM,                ///< Assembly block.
    NODE_LAMBDA,             ///< Lambda function.
    NODE_PLUGIN,             ///< Plugin invocation.
    NODE_GOTO,               ///< Goto statement.
    NODE_LABEL,              ///< Label.
    NODE_DO_WHILE,           ///< Do-while loop.
    NODE_TYPEOF,             ///< Typeof operator.
    NODE_TRY,                ///< Try statement (error handling).
    NODE_REFLECTION,         ///< Reflection info.
    NODE_AWAIT,              ///< Await expression.
    NODE_REPL_PRINT,         ///< Implicit print (REPL).
    NODE_CUDA_LAUNCH,        ///< CUDA kernel launch (<<<...>>>).
    NODE_VA_START,           ///< va_start intrinsic.
    NODE_VA_END,             ///< va_end intrinsic.
    NODE_VA_COPY,            ///< va_copy intrinsic.
    NODE_VA_ARG,             ///< va_arg intrinsic.
    NODE_AST_COMMENT         ///< Comment node.
} NodeType;

// ** AST Node Structure **
typedef struct Attribute
{
    char *name;
    char **args;
    int arg_count;
    struct Attribute *next;
} Attribute;

struct ASTNode
{
    NodeType type;
    ASTNode *next;
    int line; // Source line number for debugging.

    // Type information.
    char *resolved_type; // Legacy string representation (for example: "int",
                         // "User*"). > Yes, 'legacy' is a thing, this is the
                         // third iteration > of this project (for now).
    Type *type_info;     // Formal type object (for inference/generics).
    Token token;
    Token definition_token; // For LSP: Location where the symbol used in this
                            // node was defined.
    char *cfg_condition;    // C preprocessor condition from @cfg

    union
    {
        struct
        {
            ASTNode *children;
        } root;

        struct
        {
            char *name;
            char *generic_params; // <T, U>
            char *args;           // Legacy string args.
            char *ret_type;       // Legacy string return type.
            ASTNode *body;
            Type **arg_types;
            char **defaults;
            ASTNode **default_values; // AST representation (for robust substitution)
            char **param_names;       // Explicit parameter names.
            int arg_count;
            Type *ret_type_info;
            int is_varargs;
            int is_inline;
            int required; // @required: warn if return value is discarded.
            // GCC attributes
            int noinline;    // @noinline
            int constructor; // @constructor
            int destructor;  // @destructor
            int unused;      // @unused
            int weak;        // @weak
            int is_export;   // @export (visibility default).
            int cold;        // @cold
            int hot;         // @hot
            int noreturn;    // @noreturn
            int pure;        // @pure
            char *section;   // @section("name")
            int is_async;    // async function
            int is_comptime; // @comptime function
            // CUDA qualifiers
            int cuda_global; // @global -> __global__
            int cuda_device; // @device -> __device__
            int cuda_host;   // @host -> __host__

            char **c_type_overrides; // @ctype("...") per parameter

            Attribute *attributes; // Custom attributes
        } func;

        struct
        {
            ASTNode *statements;
        } block;

        struct
        {
            ASTNode *value;
        } ret;

        struct
        {
            char *name;
            char *type_str;
            ASTNode *init_expr;
            Type *type_info;
            int is_autofree;
            int is_static;
        } var_decl;

        struct
        {
            char *name;
            Type *payload;
            int tag_id;
        } variant;

        struct
        {
            char *name;
            ASTNode *variants;
            int is_template;
            char *generic_param;
        } enm;

        struct
        {
            char *alias;
            char *original_type;
            int is_opaque;
            char *defined_in_file;
        } type_alias;

        struct
        {
            ASTNode *condition;
            ASTNode *then_body;
            ASTNode *else_body;
        } if_stmt;

        struct
        {
            ASTNode *condition;
            ASTNode *body;
            char *loop_label;
        } while_stmt;

        struct
        {
            ASTNode *init;
            ASTNode *condition;
            ASTNode *step;
            ASTNode *body;
            char *loop_label;
        } for_stmt;

        struct
        {
            char *var_name;
            ASTNode *start;
            ASTNode *end;
            char *step;
            int is_inclusive;
            ASTNode *body;
            char *loop_label;
        } for_range;

        struct
        {
            ASTNode *body;
            char *loop_label;
        } loop_stmt;

        struct
        {
            char *count;
            ASTNode *body;
            char *loop_label;
        } repeat_stmt;

        struct
        {
            ASTNode *condition;
            ASTNode *body;
        } unless_stmt;

        struct
        {
            ASTNode *condition;
            ASTNode *body;
        } guard_stmt;

        struct
        {
            ASTNode *condition;
            ASTNode *body;
            char *loop_label;
        } do_while_stmt;

        struct
        {
            ASTNode *expr;
            ASTNode *cases;
        } match_stmt;

        struct
        {
            char *pattern;
            char **binding_names; // Multiple bindings
            int binding_count;    // Count
            int *binding_refs;    // Ref flags per binding
            int is_destructuring;
            ASTNode *guard;
            ASTNode *body;
            int is_default;
        } match_case;

        struct
        {
            char *op;
            ASTNode *left;
            ASTNode *right;
        } binary;

        struct
        {
            char *op;
            ASTNode *operand;
        } unary;

        struct
        {
            LiteralKind type_kind;
            unsigned long long int_val;
            double float_val;
            char *string_val;
        } literal;

        struct
        {
            char *name;
            char *suggestion;
        } var_ref;

        struct
        {
            ASTNode *callee;
            ASTNode *args;
            char **arg_names;
            int arg_count;
        } call;

        struct
        {
            ASTNode *target;
            char *field;
            int is_pointer_access;
        } member;

        struct
        {
            ASTNode *array;
            ASTNode *index;
            ASTNode *extra_indices; // Linked list of additional indices (for v[i, j, k])
            int index_count;        // Total index count (1 for v[i], 2 for v[i,j], etc.)
        } index;

        struct
        {
            ASTNode *array;
            ASTNode *start;
            ASTNode *end;
        } slice;

        struct
        {
            char *target_type;
            ASTNode *expr;
        } cast;

        struct
        {
            char *struct_name;
            ASTNode *fields;
        } struct_init;

        struct
        {
            ASTNode *elements;
            int count;
        } array_literal;

        struct
        {
            ASTNode *elements;
            int count;
        } tuple_literal;

        struct
        {
            char *name;
            ASTNode *fields;
            int is_template;
            char **generic_params;   // Array of generic parameter names (for example, ["K", "V"])
            int generic_param_count; // Number of generic parameters
            char *parent;
            int is_union;
            int is_packed;         // @packed attribute.
            int align;             // @align(N) attribute, 0 = default.
            int is_incomplete;     // Forward declaration (prototype)
            int is_export;         // @export attribute
            Attribute *attributes; // Custom attributes
            char **used_structs;   // Names of structs used/mixed-in
            int used_struct_count;
            int is_opaque;
            char *defined_in_file; // File where the struct is defined (for privacy check)
        } strct;

        struct
        {
            char *name;
            char *type;
            int bit_width;
        } field;

        struct
        {
            char *name;
            ASTNode *methods;
            char **generic_params;
            int generic_param_count;
        } trait;

        struct
        {
            char *struct_name;
            ASTNode *methods;
        } impl;

        struct
        {
            char *trait_name;
            char *target_type;
            ASTNode *methods;
        } impl_trait;

        struct
        {
            char *path;
            int is_system;
        } include;

        struct
        {
            char *content;
            char **used_symbols;
            int used_symbol_count;
        } raw_stmt;

        struct
        {
            char *name;
            ASTNode *body;
        } test_stmt;

        struct
        {
            ASTNode *condition;
            char *message;
        } assert_stmt;

        struct
        {
            ASTNode *stmt;
        } defer_stmt;

        struct
        {
            char *plugin_name;
            char *body;
        } plugin_stmt;

        struct
        {
            char **names;
            char **types;      // Explicit types (NULL entries if inferred)
            Type **type_infos; // Formal type objects (NULL entries if inferred)
            int count;
            ASTNode *init_expr;
            int is_struct_destruct;
            char *struct_name;  // "Point" (or NULL if unchecked/inferred).
            char **field_names; // NULL if same as 'names', otherwise mapped.
            int is_guard;
            char *guard_variant; // "Some", "Ok".
            ASTNode *else_block;
        } destruct;

        struct
        {
            ASTNode *cond;
            ASTNode *true_expr;
            ASTNode *false_expr;
        } ternary;

        struct
        {
            char *code;
            int is_volatile;
            int register_size; // The register size to use of 32/64/128 bits
            char **outputs;
            char **output_modes;
            char **inputs;
            char **clobbers;
            int num_outputs;
            int num_inputs;
            int num_clobbers;
        } asm_stmt;

        struct
        {
            char **param_names;
            char **param_types;
            char *return_type;
            ASTNode *body;
            int num_params;
            int lambda_id;
            int is_expression;
            int is_bare; // 1 if should be emitted without void* ctx (for fn*)
            char **captured_vars;
            char **captured_types;
            int num_captures;
            int *capture_modes;
            int default_capture_mode;
            char **explicit_captures;
            int *explicit_capture_modes;
            int num_explicit_captures;
        } lambda;

        struct
        {
            char *target_type;
            ASTNode *expr;
            Type *target_type_info;
            int is_type;
        } size_of;

        struct
        {
            char *label_name;
            ASTNode *goto_expr;
        } goto_stmt;

        struct
        {
            char *label_name;
        } label_stmt;

        struct
        {
            char *target_label;
        } break_stmt;

        struct
        {
            char *target_label;
        } continue_stmt;

        struct
        {
            ASTNode *expr;
        } try_stmt;

        struct
        {
            int kind; // 0=type_name, 1=fields.
            Type *target_type;
        } reflection;

        struct
        {
            ASTNode *expr;
        } repl_print;

        struct
        {
            ASTNode *call;       // The kernel call (NODE_EXPR_CALL)
            ASTNode *grid;       // Grid dimensions expression
            ASTNode *block;      // Block dimensions expression
            ASTNode *shared_mem; // Optional shared memory size (NULL = default)
            ASTNode *stream;     // Optional CUDA stream (NULL = default)
        } cuda_launch;

        struct
        {
            ASTNode *ap;
            ASTNode *last_arg;
        } va_start;

        struct
        {
            ASTNode *ap;
        } va_end;

        struct
        {
            ASTNode *dest;
            ASTNode *src;
        } va_copy;

        struct
        {
            ASTNode *ap;
            Type *type_info;
        } va_arg;

        struct
        {
            char *content;
        } comment;
    };
};

// ** Functions **
ASTNode *ast_create(NodeType type);
void ast_free(ASTNode *node);

Type *type_new(TypeKind kind);
Type *type_new_ptr(Type *inner);
Type *type_new_array(Type *inner, int size);
Type *type_new_vector(Type *inner, int size);
int type_eq(Type *a, Type *b);
int is_integer_type(Type *t);
int is_float_type(Type *t);
char *type_to_string(Type *t);
char *type_to_c_string(Type *t);
Type *get_inner_type(Type *t);

#endif

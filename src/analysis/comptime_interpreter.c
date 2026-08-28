// SPDX-License-Identifier: MIT
#include "comptime_interpreter.h"
#include "../diagnostics/diagnostics.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Max comptime execution steps
#define MAX_STEPS 1000000
// Max recursion depth for @comptime fn calls
#define MAX_RECURSION 64
// Max yield buffer
#define MAX_YIELD (1024 * 1024)

// Tagged value type
typedef enum
{
    VAL_NULL,
    VAL_INT,
    VAL_BOOL,
    VAL_STRING,
    VAL_ARRAY
} CValType;

typedef struct CValue
{
    CValType kind;
    union
    {
        int64_t i;
        int b;
        char *s;
        struct
        {
            struct CValue *data;
            size_t len;
        } arr;
    } as;
} CValue;

static const CValue val_null = {VAL_NULL, {0}};

// Scope entry
typedef struct CScope
{
    char *name;
    CValue value;
    struct CScope *next;
} CScope;

// Interpreter state
typedef struct
{
    ParserContext *pctx;
    CScope *scope;
    char *yield_buf;
    size_t yield_cap;
    size_t yield_len;
    const char *source_file;
    int step_count;
    int rec_depth;
    int error_happened;
} CInterp;

static CScope *scope_push(CScope *head, const char *name, CValue val)
{
    CScope *e = xmalloc(sizeof(CScope));
    e->name = xstrdup(name);
    e->value = val;
    e->next = head;
    return e;
}

static void scope_pop(CScope **head)
{
    if (!*head)
    {
        return;
    }
    CScope *tmp = *head;
    *head = (*head)->next;
    if (tmp->value.kind == VAL_STRING)
    {
        zfree(tmp->value.as.s);
    }
    zfree(tmp->name);
    zfree(tmp);
}

static void scope_pop_all(CScope **head)
{
    while (*head)
    {
        scope_pop(head);
    }
}

static CValue *scope_find(CScope *head, const char *name)
{
    for (CScope *e = head; e; e = e->next)
    {
        if (strcmp(e->name, name) == 0)
        {
            return &e->value;
        }
    }
    return NULL;
}

static CValue val_string(const char *s)
{
    CValue v = {VAL_STRING, {0}};
    v.as.s = xstrdup(s ? s : "");
    return v;
}

static void val_free(CValue *v)
{
    if (!v)
    {
        return;
    }
    if (v->kind == VAL_STRING)
    {
        zfree(v->as.s);
        v->as.s = NULL;
    }
    else if (v->kind == VAL_ARRAY)
    {
        for (size_t k = 0; k < v->as.arr.len; k++)
        {
            val_free(&v->as.arr.data[k]);
        }
        zfree(v->as.arr.data);
        v->as.arr.data = NULL;
        v->as.arr.len = 0;
    }
}

// Deep copy a CValue so that assigning or reading an array element does not
// alias the storage of another value. Scalars copy by value; strings and
// arrays allocate fresh storage.
static CValue val_deep_copy(const CValue *v)
{
    if (!v)
    {
        return val_null;
    }
    if (v->kind == VAL_STRING)
    {
        return val_string(v->as.s);
    }
    if (v->kind == VAL_ARRAY)
    {
        CValue out = {VAL_ARRAY, {0}};
        out.as.arr.len = v->as.arr.len;
        out.as.arr.data = xcalloc(v->as.arr.len ? v->as.arr.len : 1, sizeof(CValue));
        for (size_t k = 0; k < v->as.arr.len; k++)
        {
            out.as.arr.data[k] = val_deep_copy(&v->as.arr.data[k]);
        }
        return out;
    }
    return *v;
}

static CValue val_new_array(size_t len)
{
    CValue v = {VAL_ARRAY, {0}};
    v.as.arr.len = len;
    v.as.arr.data = xcalloc(len ? len : 1, sizeof(CValue));
    return v;
}

static void yield_append(CInterp *ci, const char *s)
{
    if (!s)
    {
        return;
    }
    size_t len = strlen(s);
    if (ci->yield_len + len + 1 > ci->yield_cap)
    {
        size_t new_cap = ci->yield_cap ? ci->yield_cap * 2 : 4096;
        while (ci->yield_len + len + 1 > new_cap)
        {
            new_cap *= 2;
        }
        if (new_cap > MAX_YIELD)
        {
            zerror_at(TOKEN_UNKNOWN, "comptime yield buffer exceeded max size (1 MiB)");
            ci->error_happened = 1;
            return;
        }
        ci->yield_buf = xrealloc(ci->yield_buf, new_cap);
        ci->yield_cap = new_cap;
    }
    memcpy(ci->yield_buf + ci->yield_len, s, (size_t)(len));
    ci->yield_len += len;
    ci->yield_buf[ci->yield_len] = 0;
}

// Forward declarations
static CValue eval_expr(CInterp *ci, ASTNode *node);
static void exec_stmt(CInterp *ci, ASTNode *node);
static void exec_assign_index(CInterp *ci, ASTNode *node);
static CValue call_builtin(CInterp *ci, const char *name, ASTNode *args);
static CValue call_comptime_fn(CInterp *ci, ASTNode *fn_node, ASTNode *args);

static CValue eval_literal(CInterp *ci, ASTNode *node)
{
    (void)ci;
    CValue v = {VAL_NULL, {0}};
    switch (node->literal.kind)
    {
    case LITERAL_INT:
        v.kind = VAL_INT;
        v.as.i = (int64_t)node->literal.int_val;
        break;
    case LITERAL_FLOAT:
        v.kind = VAL_INT;
        v.as.i = (int64_t)node->literal.float_val;
        break;
    case LITERAL_STRING:
    case LITERAL_RAW_STRING:
        v = val_string(node->literal.string_val ? node->literal.string_val : "");
        break;
    case LITERAL_CHAR:
        v.kind = VAL_INT;
        v.as.i = (int64_t)node->literal.int_val;
        break;
    default:
        v = val_null;
        break;
    }
    return v;
}

static CValue eval_var(CInterp *ci, ASTNode *node)
{
    const char *name = node->var_ref.name;
    if (!name)
    {
        return val_null;
    }

    // Builtin variables
    if (strcmp(name, "__COMPTIME_TARGET__") == 0)
    {
        return val_string(z_get_system_name());
    }
    if (strcmp(name, "__COMPTIME_FILE__") == 0)
    {
        return val_string(ci->source_file ? ci->source_file : "");
    }
    if (strcmp(name, "true") == 0)
    {
        CValue v = {VAL_BOOL, {.b = 1}};
        return v;
    }
    if (strcmp(name, "false") == 0)
    {
        CValue v = {VAL_BOOL, {.b = 0}};
        return v;
    }

    CValue *found = scope_find(ci->scope, name);
    if (!found)
    {
        zerror_at(node->token, "comptime: undefined variable '%s'", name);
        ci->error_happened = 1;
        return val_null;
    }
    CValue v;
    if (found->kind == VAL_STRING)
    {
        v = val_string(found->as.s);
    }
    else if (found->kind == VAL_ARRAY)
    {
        // Deep copy so reads never alias the scope's array storage.
        v = val_deep_copy(found);
    }
    else
    {
        memcpy(&v, found, sizeof(CValue));
    }
    return v;
}

static int64_t eval_int(CInterp *ci, ASTNode *node)
{
    CValue v = eval_expr(ci, node);
    if (ci->error_happened)
    {
        return 0;
    }
    if (v.kind != VAL_INT)
    {
        zerror_at(node->token, "comptime: expected integer expression");
        ci->error_happened = 1;
        val_free(&v);
        return 0;
    }
    int64_t r = v.as.i;
    val_free(&v);
    return r;
}

static CValue eval_binary(CInterp *ci, ASTNode *node)
{
    CValue left = eval_expr(ci, node->binary.left);
    if (ci->error_happened)
    {
        return val_null;
    }

    const char *op = node->binary.op;
    if (!op)
    {
        val_free(&left);
        return val_null;
    }

    // Handle string concat with +
    if (left.kind == VAL_STRING && strcmp(op, "+") == 0)
    {
        CValue right = eval_expr(ci, node->binary.right);
        if (ci->error_happened)
        {
            val_free(&left);
            val_free(&right);
            return val_null;
        }
        if (right.kind != VAL_STRING)
        {
            zerror_at(node->token, "comptime: cannot concatenate string with non-string");
            ci->error_happened = 1;
            val_free(&left);
            val_free(&right);
            return val_null;
        }
        size_t sz = strlen(left.as.s) + strlen(right.as.s) + 1;
        char *buf = xmalloc(sz);
        snprintf(buf, sz, "%s%s", left.as.s, right.as.s);
        val_free(&left);
        val_free(&right);
        CValue r = {VAL_STRING, {0}};
        r.as.s = buf;
        return r;
    }

    CValue right = eval_expr(ci, node->binary.right);
    if (ci->error_happened)
    {
        val_free(&left);
        val_free(&right);
        return val_null;
    }

    // Logical operators work on bools; arithmetic/comparison on ints
    int is_logic = (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0);
    int is_compare = (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
                      strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0);

    if (is_logic)
    {
        int a = 0, b = 0;
        if (left.kind == VAL_BOOL)
        {
            a = left.as.b;
        }
        else if (left.kind == VAL_INT)
        {
            a = left.as.i != 0;
        }
        if (right.kind == VAL_BOOL)
        {
            b = right.as.b;
        }
        else if (right.kind == VAL_INT)
        {
            b = right.as.i != 0;
        }
        val_free(&left);
        val_free(&right);
        CValue r = {VAL_BOOL, {.b = (strcmp(op, "&&") == 0) ? (a && b) : (a || b)}};
        return r;
    }

    if (left.kind != VAL_INT)
    {
        zerror_at(node->token, "comptime: operator '%s' requires integer operands", op);
        ci->error_happened = 1;
        val_free(&left);
        val_free(&right);
        return val_null;
    }
    if (right.kind != VAL_INT)
    {
        zerror_at(node->token, "comptime: operator '%s' requires integer operands", op);
        ci->error_happened = 1;
        val_free(&left);
        val_free(&right);
        return val_null;
    }

    int64_t a = left.as.i, b = right.as.i;
    CValue r = {VAL_INT, {0}};

    if (strcmp(op, "+") == 0)
    {
        r.as.i = a + b;
    }
    else if (strcmp(op, "-") == 0)
    {
        r.as.i = a - b;
    }
    else if (strcmp(op, "*") == 0)
    {
        r.as.i = a * b;
    }
    else if (strcmp(op, "/") == 0)
    {
        if (b == 0)
        {
            zerror_at(node->token, "comptime: division by zero");
            ci->error_happened = 1;
            val_free(&left);
            val_free(&right);
            return val_null;
        }
        r.as.i = a / b;
    }
    else if (strcmp(op, "%") == 0)
    {
        if (b == 0)
        {
            zerror_at(node->token, "comptime: modulo by zero");
            ci->error_happened = 1;
            val_free(&left);
            val_free(&right);
            return val_null;
        }
        r.as.i = a % b;
    }
    else if (is_compare)
    {
        r.kind = VAL_BOOL;
        if (strcmp(op, "==") == 0)
        {
            r.as.b = (a == b);
        }
        else if (strcmp(op, "!=") == 0)
        {
            r.as.b = (a != b);
        }
        else if (strcmp(op, "<") == 0)
        {
            r.as.b = (a < b);
        }
        else if (strcmp(op, ">") == 0)
        {
            r.as.b = (a > b);
        }
        else if (strcmp(op, "<=") == 0)
        {
            r.as.b = (a <= b);
        }
        else if (strcmp(op, ">=") == 0)
        {
            r.as.b = (a >= b);
        }
    }
    else if (strcmp(op, "&&") == 0)
    {
        r.kind = VAL_BOOL;
        r.as.b = (a && b);
    }
    else if (strcmp(op, "||") == 0)
    {
        r.kind = VAL_BOOL;
        r.as.b = (a || b);
    }
    else
    {
        zerror_at(node->token, "comptime: unsupported binary operator '%s'", op);
        ci->error_happened = 1;
    }

    val_free(&left);
    val_free(&right);
    return r;
}

static CValue eval_unary(CInterp *ci, ASTNode *node)
{
    if (!node->unary.op)
    {
        return val_null;
    }
    if (strcmp(node->unary.op, "-") == 0)
    {
        CValue v = eval_expr(ci, node->unary.operand);
        if (ci->error_happened || v.kind != VAL_INT)
        {
            val_free(&v);
            return val_null;
        }
        v.as.i = -v.as.i;
        return v;
    }
    if (strcmp(node->unary.op, "!") == 0)
    {
        CValue v = eval_expr(ci, node->unary.operand);
        if (ci->error_happened)
        {
            return val_null;
        }
        if (v.kind == VAL_INT)
        {
            v.as.b = !v.as.i;
            v.kind = VAL_BOOL;
        }
        else if (v.kind == VAL_BOOL)
        {
            v.as.b = !v.as.b;
        }
        return v;
    }
    zerror_at(node->token, "comptime: unsupported unary operator '%s'", node->unary.op);
    ci->error_happened = 1;
    return val_null;
}

static CValue eval_call(CInterp *ci, ASTNode *node)
{
    const char *name = NULL;
    if (node->call.callee && node->call.callee->kind == NODE_EXPR_VAR)
    {
        name = node->call.callee->var_ref.name;
    }
    if (!name)
    {
        zerror_at(node->token, "comptime: cannot call indirect expressions");
        ci->error_happened = 1;
        return val_null;
    }

    // Try builtins first
    if (strcmp(name, "yield") == 0 || strcmp(name, "code") == 0 ||
        strcmp(name, "compile_error") == 0 || strcmp(name, "compile_warn") == 0)
    {
        return call_builtin(ci, name, node->call.args);
    }

    // Look for @comptime function
    for (StructRef *r = ci->pctx->parsed_funcs_list; r; r = r->next)
    {
        if (r->node && r->node->kind == NODE_FUNCTION && r->node->func.is_comptime &&
            strcmp(r->node->func.name, name) == 0)
        {
            return call_comptime_fn(ci, r->node, node->call.args);
        }
    }

    zerror_at(node->token, "comptime: undefined function '%s'", name);
    ci->error_happened = 1;
    return val_null;
}

static CValue call_builtin(CInterp *ci, const char *name, ASTNode *args)
{
    // Evaluate first argument
    CValue arg = {VAL_NULL, {0}};
    if (args && args->type_info && args->type_info->kind == TYPE_STRING)
    {
        // For string-typed args, we need the compile-time value
        if (args->kind == NODE_EXPR_LITERAL || args->kind == NODE_EXPR_VAR)
        {
            arg = eval_expr(ci, args);
        }
        else if (args->kind == NODE_EXPR_BINARY && strcmp(args->binary.op, "+") == 0)
        {
            arg = eval_expr(ci, args); // string concat
        }
        else if (args->kind == NODE_EXPR_CALL)
        {
            arg = eval_expr(ci, args); // function return value
        }
        else
        {
            zerror_at(args->token, "comptime: argument to '%s' must be a compile-time constant",
                      name);
            ci->error_happened = 1;
            return val_null;
        }
    }
    else if (args)
    {
        arg = eval_expr(ci, args);
    }

    if (ci->error_happened)
    {
        val_free(&arg);
        return val_null;
    }

    if (strcmp(name, "yield") == 0 || strcmp(name, "code") == 0)
    {
        char int_buf[32];
        const char *s = "";
        if (arg.kind == VAL_STRING)
        {
            s = arg.as.s ? arg.as.s : "";
        }
        else if (arg.kind == VAL_INT)
        {
            snprintf(int_buf, sizeof(int_buf), "%lld", (long long)arg.as.i);
            s = int_buf;
        }
        yield_append(ci, s);
        val_free(&arg);
        return val_null;
    }

    if (strcmp(name, "compile_error") == 0)
    {
        const char *msg = (arg.kind == VAL_STRING && arg.as.s) ? arg.as.s : "comptime error";
        zerror_at(TOKEN_UNKNOWN, "comptime error: %s", msg);
        ci->error_happened = 1;
        val_free(&arg);
        return val_null;
    }

    if (strcmp(name, "compile_warn") == 0)
    {
        const char *msg = (arg.kind == VAL_STRING && arg.as.s) ? arg.as.s : "comptime warning";
        fprintf(stderr, "comptime warning: %s\n", msg);
        val_free(&arg);
        return val_null;
    }

    val_free(&arg);
    return val_null;
}

static CValue call_comptime_fn(CInterp *ci, ASTNode *fn_node, ASTNode *args)
{
    if (ci->rec_depth >= MAX_RECURSION)
    {
        zerror_at(fn_node->token, "comptime: recursion depth limit exceeded");
        ci->error_happened = 1;
        return val_null;
    }

    // Save scope
    CScope *saved_scope = ci->scope;
    ci->scope = NULL;
    ci->rec_depth++;

    // Bind args to params
    ASTNode *arg = args;
    for (int i = 0; i < fn_node->func.count && arg; i++, arg = arg->next)
    {
        CValue val = eval_expr(ci, arg);
        if (ci->error_happened)
        {
            break;
        }
        if (fn_node->func.param_names && fn_node->func.param_names[i])
        {
            ci->scope = scope_push(ci->scope, fn_node->func.param_names[i], val);
        }
        else
        {
            val_free(&val);
        }
    }

    // Execute body
    if (!ci->error_happened && fn_node->func.body)
    {
        ASTNode *stmt = fn_node->func.body->block.statements;
        while (stmt && !ci->error_happened)
        {
            // Check for return
            if (stmt->kind == NODE_RETURN)
            {
                CValue ret = val_null;
                if (stmt->ret.value)
                {
                    ret = eval_expr(ci, stmt->ret.value);
                }
                scope_pop_all(&ci->scope);
                ci->scope = saved_scope;
                ci->rec_depth--;
                return ret;
            }
            exec_stmt(ci, stmt);
            if (ci->error_happened)
            {
                break;
            }
            stmt = stmt->next;
        }
    }

    scope_pop_all(&ci->scope);
    ci->scope = saved_scope;
    ci->rec_depth--;
    return val_null;
}

static CValue eval_index(CInterp *ci, ASTNode *node)
{
    CValue arr = eval_expr(ci, node->index.array);
    if (ci->error_happened)
    {
        return val_null;
    }
    if (arr.kind != VAL_ARRAY)
    {
        val_free(&arr);
        zerror_at(node->token, "comptime: cannot index a non-array value");
        ci->error_happened = 1;
        return val_null;
    }
    int64_t idx = eval_int(ci, node->index.index);
    if (ci->error_happened)
    {
        val_free(&arr);
        return val_null;
    }
    if (idx < 0 || (uint64_t)idx >= arr.as.arr.len)
    {
        val_free(&arr);
        zerror_at(node->token, "comptime: array index %lld out of bounds (length %zu)",
                  (long long)idx, arr.as.arr.len);
        ci->error_happened = 1;
        return val_null;
    }
    CValue elem = val_deep_copy(&arr.as.arr.data[idx]);
    val_free(&arr);
    return elem;
}

static CValue eval_expr(CInterp *ci, ASTNode *node)
{
    if (ci->step_count++ > MAX_STEPS)
    {
        zerror_at(node ? node->token : TOKEN_UNKNOWN,
                  "comptime: step limit exceeded (possible infinite loop)");
        ci->error_happened = 1;
        return val_null;
    }
    if (!node || ci->error_happened)
    {
        return val_null;
    }

    switch (node->kind)
    {
    case NODE_EXPR_LITERAL:
        return eval_literal(ci, node);
    case NODE_EXPR_VAR:
        return eval_var(ci, node);
    case NODE_EXPR_BINARY:
        return eval_binary(ci, node);
    case NODE_EXPR_UNARY:
        return eval_unary(ci, node);
    case NODE_EXPR_CALL:
        return eval_call(ci, node);
    case NODE_EXPR_INDEX:
        return eval_index(ci, node);
    case NODE_EXPR_CAST:
        // Casts are transparent in the comptime value model.
        return eval_expr(ci, node->cast.expr);
    case NODE_EXPR_MEMBER:
        // Simple member access not supported yet
        zerror_at(node->token, "comptime: member access not supported yet");
        ci->error_happened = 1;
        return val_null;
    default:
        zerror_at(node->token, "comptime: unsupported expression type %d", (int)node->kind);
        ci->error_happened = 1;
        return val_null;
    }
}

static void exec_let(CInterp *ci, ASTNode *node)
{
    const char *name = node->var_decl.name;
    if (!name)
    {
        return;
    }
    CValue val = val_null;
    if (node->var_decl.init_expr)
    {
        val = eval_expr(ci, node->var_decl.init_expr);
        if (ci->error_happened)
        {
            return;
        }
    }
    else if (node->var_decl.type_info && node->var_decl.type_info->kind == TYPE_ARRAY &&
             node->var_decl.type_info->array_size > 0)
    {
        // Materialize a zero-initialized fixed-size array (e.g. let fib: long[20];)
        val = val_new_array((size_t)node->var_decl.type_info->array_size);
    }
    ci->scope = scope_push(ci->scope, name, val);
}

static void exec_assign(CInterp *ci, ASTNode *node)
{
    if (!node->binary.left)
    {
        zerror_at(node->token, "comptime: assignment target must be a variable");
        ci->error_happened = 1;
        return;
    }
    if (node->binary.left->kind == NODE_EXPR_INDEX)
    {
        exec_assign_index(ci, node);
        return;
    }
    if (node->binary.left->kind != NODE_EXPR_VAR)
    {
        zerror_at(node->token, "comptime: assignment target must be a variable");
        ci->error_happened = 1;
        return;
    }
    const char *name = node->binary.left->var_ref.name;
    CValue *existing = scope_find(ci->scope, name);
    if (!existing)
    {
        zerror_at(node->token, "comptime: undefined variable '%s'", name);
        ci->error_happened = 1;
        return;
    }
    CValue new_val = eval_expr(ci, node->binary.right);
    if (ci->error_happened)
    {
        return;
    }
    val_free(existing);
    *existing = new_val;
}

// Assign to an array element (left side of '=' is a NODE_EXPR_INDEX).
// Writes directly into the scope's array storage so the mutation persists.
static void exec_assign_index(CInterp *ci, ASTNode *node)
{
    ASTNode *lhs = node->binary.left;
    if (!lhs->index.array || lhs->index.array->kind != NODE_EXPR_VAR)
    {
        zerror_at(node->token, "comptime: can only assign to an element of an array variable");
        ci->error_happened = 1;
        return;
    }
    const char *name = lhs->index.array->var_ref.name;
    CValue *existing = scope_find(ci->scope, name);
    if (!existing)
    {
        zerror_at(node->token, "comptime: undefined variable '%s'", name);
        ci->error_happened = 1;
        return;
    }
    if (existing->kind != VAL_ARRAY)
    {
        zerror_at(node->token, "comptime: cannot index a non-array value");
        ci->error_happened = 1;
        return;
    }
    int64_t idx = eval_int(ci, lhs->index.index);
    if (ci->error_happened)
    {
        return;
    }
    if (idx < 0 || (uint64_t)idx >= existing->as.arr.len)
    {
        zerror_at(node->token, "comptime: array index %lld out of bounds (length %zu)",
                  (long long)idx, existing->as.arr.len);
        ci->error_happened = 1;
        return;
    }
    CValue new_val = eval_expr(ci, node->binary.right);
    if (ci->error_happened)
    {
        return;
    }
    CValue *slot = existing->as.arr.data + idx;
    val_free(slot);
    *slot = new_val;
}

static void exec_if(CInterp *ci, ASTNode *node)
{
    CValue cond = eval_expr(ci, node->if_stmt.condition);
    if (ci->error_happened)
    {
        val_free(&cond);
        return;
    }
    int truthy = (cond.kind == VAL_INT && cond.as.i) || (cond.kind == VAL_BOOL && cond.as.b);
    val_free(&cond);
    if (truthy)
    {
        ASTNode *s = node->if_stmt.then_body;
        if (s && s->kind == NODE_BLOCK)
        {
            s = s->block.statements;
        }
        while (s && !ci->error_happened)
        {
            exec_stmt(ci, s);
            s = s->next;
        }
    }
    else if (node->if_stmt.else_body)
    {
        ASTNode *s = node->if_stmt.else_body;
        if (s && s->kind == NODE_BLOCK)
        {
            s = s->block.statements;
        }
        while (s && !ci->error_happened)
        {
            exec_stmt(ci, s);
            s = s->next;
        }
    }
}

static void exec_block(CInterp *ci, ASTNode *node)
{
    ASTNode *s = node->block.statements;
    while (s && !ci->error_happened)
    {
        exec_stmt(ci, s);
        s = s->next;
    }
}

static void exec_stmt(CInterp *ci, ASTNode *node)
{
    if (!node || ci->error_happened)
    {
        return;
    }
    if (ci->step_count++ > MAX_STEPS)
    {
        zerror_at(node->token, "comptime: step limit exceeded (possible infinite loop)");
        ci->error_happened = 1;
        return;
    }
    switch (node->kind)
    {
    case NODE_ASSERT:
    case NODE_EXPECT:
    {
        CValue cv = eval_expr(ci, node->assert_stmt.condition);
        int truthy = (cv.kind == VAL_INT && cv.as.i) || (cv.kind == VAL_BOOL && cv.as.b);
        val_free(&cv);
        if (!truthy)
        {
            const char *msg =
                node->assert_stmt.message ? node->assert_stmt.message : "comptime assertion failed";
            zerror_at(node->token, "comptime: %s", msg);
            ci->error_happened = 1;
        }
        break;
    }
    case NODE_VAR_DECL:
        exec_let(ci, node);
        break;
    case NODE_EXPR_BINARY:
        if (node->binary.op && strcmp(node->binary.op, "=") == 0)
        {
            exec_assign(ci, node);
        }
        else
        {
            eval_expr(ci, node);
        }
        break;
    case NODE_EXPR_CALL:
    case NODE_EXPR_VAR:
    case NODE_EXPR_LITERAL:
    case NODE_EXPR_UNARY:
        eval_expr(ci, node);
        break;
    case NODE_IF:
        exec_if(ci, node);
        break;
    case NODE_BLOCK:
        exec_block(ci, node);
        break;
    case NODE_FOR:
    {
        // for (init; cond; step) body
        if (node->for_stmt.init)
        {
            exec_stmt(ci, node->for_stmt.init);
        }
        while (!ci->error_happened)
        {
            CValue cv = val_null;
            if (node->for_stmt.condition)
            {
                cv = eval_expr(ci, node->for_stmt.condition);
                int truthy = (cv.kind == VAL_INT && cv.as.i) || (cv.kind == VAL_BOOL && cv.as.b);
                val_free(&cv);
                if (!truthy)
                {
                    break;
                }
            }
            if (node->for_stmt.body)
            {
                exec_stmt(ci, node->for_stmt.body);
            }
            if (ci->error_happened)
            {
                break;
            }
            if (node->for_stmt.step)
            {
                exec_stmt(ci, node->for_stmt.step);
            }
        }
        break;
    }
    case NODE_FOR_RANGE:
    {
        int64_t start = node->for_range.start ? eval_int(ci, node->for_range.start) : 0;
        int64_t end = node->for_range.end ? eval_int(ci, node->for_range.end) : 0;
        if (ci->error_happened)
        {
            break;
        }
        int inclusive = node->for_range.is_inclusive;
        for (int64_t i = start; inclusive ? i <= end : i < end; i++)
        {
            if (ci->step_count++ > MAX_STEPS)
            {
                zerror_at(node->token, "comptime: step limit exceeded in for-range loop");
                ci->error_happened = 1;
                break;
            }
            // Bind loop variable
            if (node->for_range.var_name)
            {
                CValue iv = {VAL_INT, {.i = i}};
                ci->scope = scope_push(ci->scope, node->for_range.var_name, iv);
            }
            if (node->for_range.body)
            {
                exec_stmt(ci, node->for_range.body);
            }
            if (node->for_range.var_name)
            {
                scope_pop(&ci->scope);
            }
            if (ci->error_happened)
            {
                break;
            }
        }
        break;
    }
    case NODE_WHILE:
    {
        while (!ci->error_happened)
        {
            CValue cv = eval_expr(ci, node->while_stmt.condition);
            int truthy = (cv.kind == VAL_INT && cv.as.i) || (cv.kind == VAL_BOOL && cv.as.b);
            val_free(&cv);
            if (!truthy)
            {
                break;
            }
            if (node->while_stmt.body)
            {
                exec_stmt(ci, node->while_stmt.body);
            }
        }
        break;
    }
    case NODE_LOOP:
    {
        while (!ci->error_happened)
        {
            if (ci->step_count++ > MAX_STEPS)
            {
                break;
            }
            if (node->loop_stmt.body)
            {
                exec_stmt(ci, node->loop_stmt.body);
            }
        }
        break;
    }
    case NODE_REPEAT:
    {
        int64_t n = node->repeat_stmt.count ? atoll(node->repeat_stmt.count) : 0;
        for (int64_t i = 0; i < n && !ci->error_happened; i++)
        {
            if (node->repeat_stmt.body)
            {
                exec_stmt(ci, node->repeat_stmt.body);
            }
        }
        break;
    }
    case NODE_BREAK:
    case NODE_CONTINUE:
        // Not directly supported in comptime loops; ignore
        break;
    default:
        // Skip unsupported statement types silently (they may be comptime-only constructs)
        break;
    }
}

char *interpret_comptime(ParserContext *ctx, ASTNode *body, const char *source_file)
{
    CInterp ci;
    memset(&ci, 0, sizeof(ci));
    ci.pctx = ctx;
    ci.source_file = source_file;
    ci.yield_buf = NULL;
    ci.yield_cap = 0;
    ci.yield_len = 0;

    // Execute body statements
    ASTNode *stmt = body;
    while (stmt && !ci.error_happened)
    {
        exec_stmt(&ci, stmt);
        stmt = stmt->next;
    }

    // Finalize yield buffer
    char *result = ci.yield_buf ? ci.yield_buf : xstrdup("");

    // Cleanup
    scope_pop_all(&ci.scope);

    if (ci.error_happened)
    {
        if (result != ci.yield_buf)
        {
            zfree(result);
        }
        zfree(ci.yield_buf);
        return NULL;
    }

    return result;
}


#include "parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../ast/ast.h"
#include "../plugins/plugin_manager.h"
#include "../zen/zen_facts.h"
#include "zprep_plugin.h"
#include "../codegen/codegen.h"
#include "analysis/move_check.h"

char *curr_func_ret = NULL;
char *run_comptime_block(ParserContext *ctx, Lexer *l);
extern char *g_current_filename;

/**
 * @brief Auto-imports std/slice.zc if not already imported.
 *
 * This is called when array iteration is detected in for-in loops,
 * to ensure the Slice<T>, SliceIter<T>, and Option<T> templates are available.
 */
static void auto_import_std_slice(ParserContext *ctx)
{
    // Check if already imported via templates
    GenericTemplate *t = ctx->templates;
    while (t)
    {
        if (strcmp(t->name, "Slice") == 0)
        {
            return; // Already have the Slice template
        }
        t = t->next;
    }

    // Resolve path to std/slice.zc
    char *resolved = z_resolve_path("std/slice.zc", g_current_filename);
    if (!resolved)
    {
        return; // Could not find slice.zc
    }

    // Check if already imported by path
    if (is_file_imported(ctx, resolved))
    {
        free(resolved);
        return;
    }
    mark_file_imported(ctx, resolved);

    // Load and parse the file
    char *src = load_file(resolved);
    if (!src)
    {
        free(resolved);
        return;
    }

    Lexer i;
    lexer_init(&i, src);

    // Save and restore filename context
    char *saved_fn = g_current_filename;
    g_current_filename = resolved;

    // Parse the slice module contents
    parse_program_nodes(ctx, &i);

    g_current_filename = saved_fn;
    free(resolved);
}

static void check_assignment_condition(ASTNode *cond)
{
    if (!cond)
    {
        return;
    }
    if (cond->type == NODE_EXPR_BINARY)
    {
        if (cond->binary.op && strcmp(cond->binary.op, "=") == 0)
        {
            zwarn_at(cond->token, "Assignment in condition");
            fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "Did you mean '=='?\n");
        }
    }
}

char *normalize_raw_content(const char *content)
{
    if (!content)
    {
        return NULL;
    }

    size_t len = strlen(content);
    char *normalized = xmalloc(len + 1);
    char *d = normalized;
    const char *s = content;

    while (*s)
    {
        if (*s == '\r')
        {
            if (*(s + 1) == '\n')
            {
                *d++ = '\n';
                s += 2;
                continue;
            }
            // Bare \r -> \n
            *d++ = '\n';
            s++;
        }
        else
        {
            *d++ = *s++;
        }
    }
    *d = '\0';
    return normalized;
}

ASTNode *parse_match(ParserContext *ctx, Lexer *l)
{
    init_builtins();
    Token start_token = lexer_peek(l);
    lexer_next(l); // eat 'match'
    ASTNode *expr = parse_expression(ctx, l);

    Token t_brace = lexer_next(l);
    if (t_brace.type != TOK_LBRACE)
    {
        zpanic_at(t_brace, "Expected { in match");
    }

    ASTNode *h = 0, *tl = 0;
    while (lexer_peek(l).type != TOK_RBRACE)
    {
        skip_comments(l);
        if (lexer_peek(l).type == TOK_RBRACE)
        {
            break;
        }
        if (lexer_peek(l).type == TOK_COMMA)
        {
            lexer_next(l);
        }
        skip_comments(l);
        if (lexer_peek(l).type == TOK_RBRACE)
        {
            break;
        }

        // Parse Patterns (with OR and range support)
        // Patterns can be:
        //   - Single value: 1
        //   - OR patterns: 1 || 2 or 1 or 2 or 1, 2
        //   - Range patterns: 1..5 or 1..=5 or 1..<5
        char patterns_buf[1024];
        patterns_buf[0] = 0;
        int pattern_count = 0;

        while (1)
        {
            Token p = lexer_next(l);
            char *p_str = token_strdup(p);

            while (lexer_peek(l).type == TOK_DCOLON)
            {
                lexer_next(l); // eat ::
                Token suffix = lexer_next(l);
                char *tmp = xmalloc(strlen(p_str) + suffix.len + 2);
                // Join with underscore: Result::Ok -> Result__Ok
                sprintf(tmp, "%s__%.*s", p_str, suffix.len, suffix.start);
                free(p_str);
                p_str = tmp;
            }

            // Check for range pattern: value..end, value..<end or value..=end
            if (lexer_peek(l).type == TOK_DOTDOT || lexer_peek(l).type == TOK_DOTDOT_EQ ||
                lexer_peek(l).type == TOK_DOTDOT_LT)
            {
                int is_inclusive = (lexer_peek(l).type == TOK_DOTDOT_EQ);
                lexer_next(l); // eat operator
                Token end_tok = lexer_next(l);
                char *end_str = token_strdup(end_tok);

                // Build range pattern: "start..end" or "start..=end"
                char *range_str = xmalloc(strlen(p_str) + strlen(end_str) + 4);
                sprintf(range_str, "%s%s%s", p_str, is_inclusive ? "..=" : "..", end_str);
                free(p_str);
                free(end_str);
                p_str = range_str;
            }

            if (pattern_count > 0)
            {
                strcat(patterns_buf, "|");
            }
            strcat(patterns_buf, p_str);
            free(p_str);
            pattern_count++;

            // Check for OR continuation: ||, 'or', or comma (legacy)
            Token next = lexer_peek(l);
            skip_comments(l);
            int is_or = (next.type == TOK_OR) ||
                        (next.type == TOK_OP && next.len == 2 && next.start[0] == '|' &&
                         next.start[1] == '|') ||
                        (next.type == TOK_COMMA); // Legacy comma support
            if (is_or)
            {
                lexer_next(l); // eat ||, 'or', or comma
                skip_comments(l);
                continue;
            }
            else
            {
                break;
            }
        }

        char *pattern = xstrdup(patterns_buf);
        int is_default = (strcmp(pattern, "_") == 0);
        int is_destructure = 0;

        // Handle Destructuring: Ok(v) or Rect(w, h)
        char **bindings = NULL;
        int *binding_refs = NULL;
        int binding_count = 0;

        if (!is_default && pattern_count == 1 && lexer_peek(l).type == TOK_LPAREN)
        {
            lexer_next(l); // eat (

            bindings = xmalloc(sizeof(char *) * 8); // hardcap at 8 for now or realloc
            binding_refs = xmalloc(sizeof(int) * 8);

            while (1)
            {
                int is_r = 0;
                // Check for 'ref' keyword
                if (lexer_peek(l).type == TOK_IDENT && lexer_peek(l).len == 3 &&
                    strncmp(lexer_peek(l).start, "ref", 3) == 0)
                {
                    lexer_next(l); // eat 'ref'
                    is_r = 1;
                }

                Token b = lexer_next(l);
                if (b.type != TOK_IDENT)
                {
                    zpanic_at(b, "Expected variable name in pattern");
                }
                bindings[binding_count] = token_strdup(b);
                binding_refs[binding_count] = is_r;
                binding_count++;

                if (lexer_peek(l).type == TOK_COMMA)
                {
                    lexer_next(l);
                    continue;
                }
                break;
            }

            if (lexer_next(l).type != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
            }
            is_destructure = 1;
        }

        // Parse Guard (if condition)
        ASTNode *guard = NULL;
        if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "if", 2) == 0)
        {
            lexer_next(l);
            guard = parse_expression(ctx, l);
            check_assignment_condition(guard);
        }

        if (lexer_next(l).type != TOK_ARROW)
        {
            zpanic_at(lexer_peek(l), "Expected =>");
        }

        // Create scope for the case to hold the binding
        enter_scope(ctx);
        if (binding_count > 0)
        {
            // Try to infer binding type from enum variant payload
            // Look up the enum variant to get its payload type
            EnumVariantReg *vreg = find_enum_variant(ctx, pattern);

            ASTNode *payload_node_field = NULL;
            int is_tuple_payload = 0;
            Type *payload_type = NULL;
            ASTNode *enum_def = NULL;

            if (vreg)
            {
                // Find the enum definition
                enum_def = find_struct_def(ctx, vreg->enum_name);
                if (enum_def && enum_def->type == NODE_ENUM)
                {
                    // Find the specific variant
                    ASTNode *v = enum_def->enm.variants;
                    while (v)
                    {
                        // Match by variant name (pattern suffix after last _)
                        int size = strlen(vreg->enum_name) + strlen(v->variant.name) + 2;
                        char *v_full = xmalloc(size);
                        snprintf(v_full, size, "%s_%s", vreg->enum_name, v->variant.name);
                        if (strcmp(v_full, pattern) == 0 && v->variant.payload)
                        {
                            // Found the variant, extract payload type
                            payload_type = v->variant.payload;
                            if (payload_type && payload_type->kind == TYPE_STRUCT &&
                                strncmp(payload_type->name, "Tuple_", 6) == 0)
                            {
                                is_tuple_payload = 1;
                                ASTNode *tuple_def = find_struct_def(ctx, payload_type->name);
                                if (tuple_def)
                                {
                                    payload_node_field = tuple_def->strct.fields;
                                }
                            }
                            free(v_full);
                            break;
                        }
                        v = v->next;
                    }
                }
            }

            for (int i = 0; i < binding_count; i++)
            {
                char *binding = bindings[i];
                int is_ref = binding_refs[i];
                char *binding_type = is_ref ? "void*" : "unknown";
                Type *binding_type_info = NULL; // Default unknown

                if (payload_type)
                {
                    if (binding_count == 1 && !is_tuple_payload)
                    {
                        binding_type = type_to_string(payload_type);
                        binding_type_info = payload_type;
                    }
                    else if (binding_count == 1 && is_tuple_payload)
                    {
                        binding_type = type_to_string(payload_type);
                        binding_type_info = payload_type;
                    }
                    else if (binding_count > 1 && is_tuple_payload)
                    {
                        if (payload_node_field)
                        {
                            Lexer tmp;
                            lexer_init(&tmp, payload_node_field->field.type);
                            binding_type_info = parse_type_formal(ctx, &tmp);
                            binding_type = type_to_string(binding_type_info);
                            payload_node_field = payload_node_field->next;
                        }
                    }
                }

                if (is_ref && binding_type_info)
                {
                    Type *ptr = type_new(TYPE_POINTER);
                    ptr->inner = binding_type_info;
                    binding_type_info = ptr;

                    char *ptr_s = xmalloc(strlen(binding_type) + 2);
                    sprintf(ptr_s, "%s*", binding_type);
                    binding_type = ptr_s;
                }

                int is_generic_unresolved = 0;

                if (enum_def)
                {
                    if (enum_def->enm.generic_param)
                    {
                        char *param = enum_def->enm.generic_param;
                        if (strstr(binding_type, param))
                        {
                            is_generic_unresolved = 1;
                        }
                    }
                }

                if (!is_generic_unresolved &&
                    (strcmp(binding_type, "T") == 0 || strcmp(binding_type, "T*") == 0))
                {
                    is_generic_unresolved = 1;
                }

                if (is_generic_unresolved)
                {
                    if (is_ref)
                    {
                        binding_type = "unknown*";
                        Type *u = type_new(TYPE_UNKNOWN);
                        Type *p = type_new(TYPE_POINTER);
                        p->inner = u;
                        binding_type_info = p;
                    }
                    else
                    {
                        binding_type = "unknown";
                        binding_type_info = type_new(TYPE_UNKNOWN);
                    }
                }

                add_symbol(ctx, binding, binding_type, binding_type_info);
            }
        }

        ASTNode *body;
        Token pk = lexer_peek(l);
        if (pk.type == TOK_LBRACE)
        {
            body = parse_block(ctx, l);
        }
        else if (pk.type == TOK_ASSERT ||
                 (pk.type == TOK_IDENT && strncmp(pk.start, "assert", 6) == 0))
        {
            body = parse_assert(ctx, l);
        }
        else if (pk.type == TOK_IDENT && strncmp(pk.start, "return", 6) == 0)
        {
            body = parse_return(ctx, l);
        }
        else
        {
            body = parse_expression(ctx, l);
        }

        exit_scope(ctx);

        ASTNode *c = ast_create(NODE_MATCH_CASE);
        c->token = pk;
        c->match_case.pattern = pattern;
        c->match_case.binding_names = bindings;
        c->match_case.binding_count = binding_count;
        c->match_case.binding_refs = binding_refs;
        c->match_case.is_destructuring = is_destructure;
        c->match_case.guard = guard;
        c->match_case.body = body;
        c->match_case.is_default = is_default;

        if (!h)
        {
            h = c;
        }
        else
        {
            tl->next = c;
        }
        tl = c;
    }
    lexer_next(l); // eat }

    ASTNode *n = ast_create(NODE_MATCH);
    n->line = start_token.line;
    n->token = start_token; // Capture token for rich warning
    n->match_stmt.expr = expr;
    n->match_stmt.cases = h;
    return n;
}

ASTNode *parse_loop(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l);
    ASTNode *b = parse_block(ctx, l);
    ASTNode *n = ast_create(NODE_LOOP);
    n->token = tk;
    n->loop_stmt.body = b;
    return n;
}

ASTNode *parse_repeat(ParserContext *ctx, Lexer *l)
{
    Token t = lexer_next(l);
    zwarn_at(t, "repeat is deprecated. Use 'for _ in 0..N' instead.");
    char *c = rewrite_expr_methods(ctx, parse_condition_raw(ctx, l));
    ASTNode *b = parse_block(ctx, l);
    ASTNode *n = ast_create(NODE_REPEAT);
    n->repeat_stmt.count = c;
    n->repeat_stmt.body = b;
    return n;
}

ASTNode *parse_unless(ParserContext *ctx, Lexer *l)
{
    lexer_next(l);
    ASTNode *cond = parse_expression(ctx, l);
    ASTNode *body = parse_block(ctx, l);
    ASTNode *n = ast_create(NODE_UNLESS);
    n->unless_stmt.condition = cond;
    n->unless_stmt.body = body;
    return n;
}

ASTNode *parse_guard(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l); // consume 'guard'

    // Parse the condition as an AST
    ASTNode *cond = parse_expression(ctx, l);

    // Check for 'else'
    Token t = lexer_peek(l);
    if (t.type != TOK_IDENT || strncmp(t.start, "else", 4) != 0)
    {
        zpanic_at(t, "Expected 'else' after guard condition");
    }
    lexer_next(l); // consume 'else'

    // Parse the body - either a block or a single statement
    ASTNode *body;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        // Single statement (e.g., guard x != NULL else return;)
        body = parse_statement(ctx, l);
    }

    // Create the node
    ASTNode *n = ast_create(NODE_GUARD);
    n->token = tk;
    n->guard_stmt.condition = cond;
    n->guard_stmt.body = body;
    return n;
}

ASTNode *parse_defer(ParserContext *ctx, Lexer *l)
{
    Token defer_token = lexer_next(l); // defer

    // Track that we're parsing inside a defer block
    int prev_in_defer = ctx->in_defer_block;
    ctx->in_defer_block = 1;

    ASTNode *s;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        s = parse_block(ctx, l);
    }
    else
    {
        s = parse_statement(ctx, l);
    }

    ctx->in_defer_block = prev_in_defer;

    ASTNode *n = ast_create(NODE_DEFER);
    n->token = defer_token;
    n->defer_stmt.stmt = s;
    return n;
}

ASTNode *parse_asm(ParserContext *ctx, Lexer *l)
{
    (void)ctx; // suppress unused parameter warning
    Token t = lexer_peek(l);
    zen_trigger_at(TRIGGER_ASM, t);
    lexer_next(l); // eat 'asm'

    // Check for 'volatile'
    int is_volatile = 0;
    if (lexer_peek(l).type == TOK_VOLATILE)
    {
        is_volatile = 1;
        lexer_next(l);
    }

    // Expect {
    if (lexer_peek(l).type != TOK_LBRACE)
    {
        zpanic_at(lexer_peek(l), "Expected { after asm");
    }
    lexer_next(l);

    // Parse assembly template strings
    char *code = xmalloc(4096); // Buffer for assembly code
    code[0] = 0;

    while (1)
    {
        Token inner_t = lexer_peek(l);

        // Check for end of asm block or start of operands
        if (inner_t.type == TOK_RBRACE)
        {
            break;
        }
        if (inner_t.type == TOK_COLON)
        {
            break;
        }

        // Support string literals for assembly instructions
        if (inner_t.type == TOK_STRING)
        {
            lexer_next(l);
            // Extract string content (strip quotes)
            int str_len = inner_t.len - 2;
            if (strlen(code) > 0)
            {
                strcat(code, "\n");
            }
            strncat(code, inner_t.start + 1, str_len);
        }
        // Also support bare identifiers for simple instructions like 'nop', 'pause'
        else if (inner_t.type == TOK_IDENT)
        {
            lexer_next(l);
            if (strlen(code) > 0)
            {
                strcat(code, "\n");
            }
            strncat(code, inner_t.start, inner_t.len);

            // Check for instruction arguments
            while (lexer_peek(l).type != TOK_RBRACE && lexer_peek(l).type != TOK_COLON)
            {
                Token arg = lexer_peek(l);

                if (arg.type == TOK_SEMICOLON)
                {
                    lexer_next(l);
                    break;
                }

                // Handle substitution {var}
                if (arg.type == TOK_LBRACE)
                {
                    lexer_next(l);
                    strcat(code, "{");
                    // Consume until }
                    while (lexer_peek(l).type != TOK_RBRACE && lexer_peek(l).type != TOK_EOF)
                    {
                        Token sub = lexer_next(l);
                        strncat(code, sub.start, sub.len);
                    }
                    if (lexer_peek(l).type == TOK_RBRACE)
                    {
                        lexer_next(l);
                        strcat(code, "}");
                    }
                    continue;
                }

                if (arg.type == TOK_IDENT)
                {
                    // Check prev char for % or $
                    char last_char = 0;
                    size_t clen = strlen(code);
                    if (clen > 0)
                    {
                        if (code[clen - 1] == ' ' && clen > 1)
                        {
                            last_char = code[clen - 2];
                        }
                        else
                        {
                            last_char = code[clen - 1];
                        }
                    }
                    if (last_char != '%' && last_char != '$' && last_char != ',')
                    {
                        break;
                    }
                }

                lexer_next(l);

                // No space logic
                int no_space = 0;
                size_t clen = strlen(code);
                if (clen > 0)
                {
                    char lc = code[clen - 1];
                    if (lc == '%' || lc == '$')
                    {
                        no_space = 1;
                    }
                }

                if (!no_space)
                {
                    strcat(code, " ");
                }
                strncat(code, arg.start, arg.len);
            }
        }
        else
        {
            zpanic_at(t, "Expected assembly string, instruction, or ':' in asm block");
        }
    }

    // Parse outputs (: out(x), inout(y))
    char **outputs = NULL;
    char **output_modes = NULL;
    int num_outputs = 0;

    if (lexer_peek(l).type == TOK_COLON)
    {
        lexer_next(l); // eat :

        outputs = xmalloc(sizeof(char *) * 16);
        output_modes = xmalloc(sizeof(char *) * 16);

        while (1)
        {
            Token inner_t = lexer_peek(l);
            if (inner_t.type == TOK_COLON || inner_t.type == TOK_RBRACE)
            {
                break;
            }
            if (inner_t.type == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }

            // Parse out(var) or inout(var)
            if (inner_t.type == TOK_IDENT)
            {
                char *mode = token_strdup(inner_t);
                lexer_next(l);

                if (lexer_peek(l).type != TOK_LPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ( after output mode");
                }
                lexer_next(l);

                Token var = lexer_next(l);
                if (var.type != TOK_IDENT)
                {
                    zpanic_at(var, "Expected variable name");
                }

                if (lexer_peek(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after variable");
                }
                lexer_next(l);

                outputs[num_outputs] = token_strdup(var);
                output_modes[num_outputs] = mode;
                num_outputs++;
            }
            else
            {
                break;
            }
        }
    }

    // Parse inputs (: in(a), in(b))
    char **inputs = NULL;
    int num_inputs = 0;

    if (lexer_peek(l).type == TOK_COLON)
    {
        lexer_next(l); // eat :

        inputs = xmalloc(sizeof(char *) * 16);

        while (1)
        {
            Token inner_t = lexer_peek(l);
            if (inner_t.type == TOK_COLON || inner_t.type == TOK_RBRACE)
            {
                break;
            }
            if (inner_t.type == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }

            // Parse in(var)
            if (inner_t.type == TOK_IDENT && strncmp(inner_t.start, "in", 2) == 0)
            {
                lexer_next(l);

                if (lexer_peek(l).type != TOK_LPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ( after in");
                }
                lexer_next(l);

                Token var = lexer_next(l);
                if (var.type != TOK_IDENT)
                {
                    zpanic_at(var, "Expected variable name");
                }

                if (lexer_peek(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after variable");
                }
                lexer_next(l);

                inputs[num_inputs] = token_strdup(var);
                num_inputs++;
            }
            else
            {
                break;
            }
        }
    }

    // Parse clobbers (: "eax", "memory" OR : clobber("eax"), clobber("memory"))
    char **clobbers = NULL;
    int num_clobbers = 0;

    if (lexer_peek(l).type == TOK_COLON)
    {
        lexer_next(l); // eat :

        clobbers = xmalloc(sizeof(char *) * 16);

        while (1)
        {
            Token inner_t = lexer_peek(l);
            if (inner_t.type == TOK_RBRACE)
            {
                break;
            }
            if (inner_t.type == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }

            // check for clobber("...")
            if (inner_t.type == TOK_IDENT && strncmp(inner_t.start, "clobber", 7) == 0)
            {
                lexer_next(l); // eat clobber
                if (lexer_peek(l).type != TOK_LPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ( after clobber");
                }
                lexer_next(l); // eat (

                Token clob = lexer_next(l);
                if (clob.type != TOK_STRING)
                {
                    zpanic_at(clob, "Expected string literal for clobber");
                }

                if (lexer_peek(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after clobber string");
                }
                lexer_next(l); // eat )

                char *c = xmalloc(clob.len);
                strncpy(c, clob.start + 1, clob.len - 2);
                c[clob.len - 2] = 0;
                clobbers[num_clobbers++] = c;
            }
            else
            {
                zpanic_at(t, "Expected 'clobber(\"...\")' in clobber list");
                break;
            }
        }
    }

    // Expect closing }
    if (lexer_peek(l).type != TOK_RBRACE)
    {
        zpanic_at(lexer_peek(l), "Expected } at end of asm block");
    }
    lexer_next(l);

    // Create AST node
    ASTNode *n = ast_create(NODE_ASM);
    n->token = t;
    n->asm_stmt.code = code;
    n->asm_stmt.is_volatile = is_volatile;
    n->asm_stmt.outputs = outputs;
    n->asm_stmt.output_modes = output_modes;
    n->asm_stmt.inputs = inputs;
    n->asm_stmt.clobbers = clobbers;
    n->asm_stmt.num_outputs = num_outputs;
    n->asm_stmt.num_inputs = num_inputs;
    n->asm_stmt.num_clobbers = num_clobbers;

    return n;
}

ASTNode *parse_test(ParserContext *ctx, Lexer *l)
{
    lexer_next(l); // eat 'test'
    Token t = lexer_next(l);
    if (t.type != TOK_STRING)
    {
        zpanic_at(t, "Test name must be a string literal");
    }

    // Strip quotes for AST storage
    char *name = xmalloc(t.len);
    strncpy(name, t.start + 1, t.len - 2);
    name[t.len - 2] = 0;

    ASTNode *body = parse_block(ctx, l);

    ASTNode *n = ast_create(NODE_TEST);
    n->test_stmt.name = name;
    n->test_stmt.body = body;
    return n;
}

ASTNode *parse_assert(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l); // assert
    if (lexer_peek(l).type == TOK_LPAREN)
    {
        lexer_next(l); // optional paren? usually yes
    }

    ASTNode *cond = parse_expression(ctx, l);

    char *msg = NULL;
    if (lexer_peek(l).type == TOK_COMMA)
    {
        lexer_next(l);
        Token st = lexer_next(l);
        if (st.type != TOK_STRING)
        {
            zpanic_at(st, "Expected message string");
        }
        msg = xmalloc(st.len + 1);
        strncpy(msg, st.start, st.len);
        msg[st.len] = 0;
    }

    if (lexer_peek(l).type == TOK_RPAREN)
    {
        lexer_next(l);
    }
    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *n = ast_create(NODE_ASSERT);
    n->token = tk;
    n->assert_stmt.condition = cond;
    n->assert_stmt.message = msg;
    return n;
}

ASTNode *parse_return(ParserContext *ctx, Lexer *l)
{
    Token return_token = lexer_next(l); // eat 'return'

    if (ctx->in_defer_block)
    {
        zpanic_at(return_token, "'return' is not allowed inside a 'defer' block");
    }

    ASTNode *n = ast_create(NODE_RETURN);
    n->token = return_token;

    int handled = 0;

    if (curr_func_ret && strncmp(curr_func_ret, "Tuple_", 6) == 0 &&
        lexer_peek(l).type == TOK_LPAREN)
    {

        int is_tuple_lit = 0;
        int depth = 0;

        Lexer temp_l = *l;

        while (1)
        {
            Token t = lexer_next(&temp_l);
            if (t.type == TOK_EOF)
            {
                break;
            }
            if (t.type == TOK_SEMICOLON)
            {
                break;
            }

            if (t.type == TOK_LPAREN)
            {
                depth++;
            }
            if (t.type == TOK_RPAREN)
            {
                depth--;
                if (depth == 0)
                {
                    break;
                }
            }

            if (depth == 1 && t.type == TOK_COMMA)
            {
                is_tuple_lit = 1;
                break;
            }
        }

        if (is_tuple_lit)
        {
            n->ret.value = parse_tuple_expression(ctx, l, curr_func_ret, NULL);
            n->ret.value->token = return_token;
            handled = 1;
        }
    }

    if (!handled)
    {
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            n->ret.value = NULL;
        }
        else
        {
            n->ret.value = parse_expression(ctx, l);
        }
    }

    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }
    return n;
}

ASTNode *parse_if(ParserContext *ctx, Lexer *l)
{
    Token if_token = lexer_next(l); // eat if
    ASTNode *cond = parse_expression(ctx, l);
    check_assignment_condition(cond);

    ASTNode *then_b = NULL;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        then_b = parse_block(ctx, l);
    }
    else
    {
        // Single statement: Wrap in scope + block
        enter_scope(ctx);
        ASTNode *s = parse_statement(ctx, l);
        exit_scope(ctx);
        then_b = ast_create(NODE_BLOCK);
        then_b->block.statements = s;
    }

    ASTNode *else_b = NULL;
    skip_comments(l);
    if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "else", 4) == 0)
    {
        lexer_next(l);
        if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "if", 2) == 0)
        {
            else_b = parse_if(ctx, l);
        }
        else if (lexer_peek(l).type == TOK_LBRACE)
        {
            else_b = parse_block(ctx, l);
        }
        else
        {
            // Single statement else
            enter_scope(ctx);
            ASTNode *s = parse_statement(ctx, l);
            exit_scope(ctx);
            else_b = ast_create(NODE_BLOCK);
            else_b->block.statements = s;
        }
    }
    ASTNode *n = ast_create(NODE_IF);
    n->token = if_token;
    n->if_stmt.condition = cond;
    n->if_stmt.then_body = then_b;
    n->if_stmt.else_body = else_b;
    return n;
}

ASTNode *parse_while(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l);
    ASTNode *cond = parse_expression(ctx, l);
    check_assignment_condition(cond);

    // Zen: While(true)
    if ((cond->type == NODE_EXPR_LITERAL && cond->literal.type_kind == LITERAL_INT &&
         cond->literal.int_val == 1) ||
        (cond->type == NODE_EXPR_VAR && strcmp(cond->var_ref.name, "true") == 0))
    {
        zen_trigger_at(TRIGGER_WHILE_TRUE, cond->token);
    }
    ASTNode *body;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        body = parse_statement(ctx, l);
    }
    ASTNode *n = ast_create(NODE_WHILE);
    n->token = tk;
    n->while_stmt.condition = cond;
    n->while_stmt.body = body;
    return n;
}

ASTNode *parse_for(ParserContext *ctx, Lexer *l)
{
    Token for_token = lexer_next(l);

    if (lexer_peek(l).type == TOK_IDENT)
    {
        int saved_pos = l->pos;
        Token var = lexer_next(l);

        char *enum_idx_name = NULL;
        Token val_tok = {0};
        if (lexer_peek(l).type == TOK_COMMA)
        {
            lexer_next(l);
            val_tok = lexer_next(l);
            enum_idx_name = xmalloc(var.len + 1);
            strncpy(enum_idx_name, var.start, var.len);
            enum_idx_name[var.len] = 0;
            var = val_tok;
        }

        Token in_tok = lexer_next(l);

        if (in_tok.type == TOK_IDENT && strncmp(in_tok.start, "in", 2) == 0)
        {
            ASTNode *start_expr = parse_expression(ctx, l);
            Token tk = lexer_peek(l);
            ZenTokenType next_tok = tk.type;
            if (next_tok == TOK_DOTDOT || next_tok == TOK_DOTDOT_LT || next_tok == TOK_DOTDOT_EQ)
            {
                int is_inclusive = 0;
                if (next_tok == TOK_DOTDOT || next_tok == TOK_DOTDOT_LT)
                {
                    lexer_next(l);
                }
                else if (next_tok == TOK_DOTDOT_EQ)
                {
                    is_inclusive = 1;
                    lexer_next(l);
                }

                if (1)
                {
                    ASTNode *end_expr = parse_expression(ctx, l);

                    ASTNode *n = ast_create(NODE_FOR_RANGE);
                    n->token = for_token;
                    n->for_range.var_name = xmalloc(var.len + 1);
                    strncpy(n->for_range.var_name, var.start, var.len);
                    n->for_range.var_name[var.len] = 0;
                    n->for_range.start = start_expr;
                    n->for_range.end = end_expr;
                    n->for_range.is_inclusive = is_inclusive;

                    if (lexer_peek(l).type == TOK_IDENT &&
                        strncmp(lexer_peek(l).start, "step", 4) == 0)
                    {
                        lexer_next(l);
                        Token s_tok = lexer_next(l);

                        if (s_tok.type == TOK_OP && s_tok.len == 1 && s_tok.start[0] == '-')
                        {
                            Token num_tok = lexer_next(l);
                            char *sval = xmalloc(s_tok.len + num_tok.len + 1);
                            strncpy(sval, s_tok.start, s_tok.len);
                            strncpy(sval + s_tok.len, num_tok.start, num_tok.len);
                            sval[s_tok.len + num_tok.len] = 0;
                            n->for_range.step = sval;
                        }
                        else
                        {
                            char *sval = xmalloc(s_tok.len + 1);
                            strncpy(sval, s_tok.start, s_tok.len);
                            sval[s_tok.len] = 0;
                            n->for_range.step = sval;
                        }
                    }
                    else
                    {
                        n->for_range.step = NULL;
                    }

                    enter_scope(ctx);
                    add_symbol(ctx, n->for_range.var_name, "int", type_new(TYPE_INT));
                    if (enum_idx_name)
                    {
                        add_symbol(ctx, enum_idx_name, "int", type_new(TYPE_INT));
                    }

                    ASTNode *user_body = NULL;
                    if (lexer_peek(l).type == TOK_LBRACE)
                    {
                        user_body = parse_block(ctx, l);
                    }
                    else
                    {
                        user_body = parse_statement(ctx, l);
                    }
                    exit_scope(ctx);

                    if (enum_idx_name)
                    {
                        ASTNode *idx_decl = ast_create(NODE_VAR_DECL);
                        idx_decl->token = tk;
                        idx_decl->var_decl.name = xstrdup("__zc_enum_idx");
                        idx_decl->var_decl.type_str = xstrdup("int");
                        idx_decl->var_decl.type_info = type_new(TYPE_INT);
                        ASTNode *zero_lit = ast_create(NODE_EXPR_LITERAL);
                        zero_lit->literal.type_kind = LITERAL_INT;
                        zero_lit->literal.int_val = 0;
                        zero_lit->literal.string_val = xstrdup("0");
                        idx_decl->var_decl.init_expr = zero_lit;

                        ASTNode *idx_bind = ast_create(NODE_VAR_DECL);
                        idx_bind->token = tk;
                        idx_bind->var_decl.name = enum_idx_name;
                        idx_bind->var_decl.type_str = xstrdup("int");
                        idx_bind->var_decl.type_info = type_new(TYPE_INT);
                        ASTNode *idx_ref = ast_create(NODE_EXPR_VAR);
                        idx_ref->var_ref.name = xstrdup("__zc_enum_idx");
                        idx_bind->var_decl.init_expr = idx_ref;

                        ASTNode *idx_inc = ast_create(NODE_EXPR_UNARY);
                        idx_inc->unary.op = xstrdup("++");
                        ASTNode *idx_ref2 = ast_create(NODE_EXPR_VAR);
                        idx_ref2->var_ref.name = xstrdup("__zc_enum_idx");
                        idx_inc->unary.operand = idx_ref2;

                        ASTNode *new_body = ast_create(NODE_BLOCK);
                        idx_bind->next = user_body;
                        if (user_body && user_body->type == NODE_BLOCK)
                        {
                            ASTNode *last = user_body->block.statements;
                            if (last)
                            {
                                while (last->next)
                                {
                                    last = last->next;
                                }
                                last->next = idx_inc;
                            }
                            idx_bind->next = user_body->block.statements;
                            user_body->block.statements = idx_bind;
                            new_body = user_body;
                        }
                        else
                        {
                            if (user_body)
                            {
                                user_body->next = idx_inc;
                            }
                            idx_bind->next = user_body;
                            new_body->block.statements = idx_bind;
                        }

                        n->for_range.body = new_body;

                        ASTNode *outer = ast_create(NODE_BLOCK);
                        idx_decl->next = n;
                        outer->block.statements = idx_decl;
                        return outer;
                    }
                    else
                    {
                        n->for_range.body = user_body;
                        return n;
                    }
                }
            }
            else
            {
                char *var_name = xmalloc(var.len + 1);
                strncpy(var_name, var.start, var.len);
                var_name[var.len] = 0;

                ASTNode *obj_expr = start_expr;
                char *iter_method = "iterator";
                ASTNode *slice_decl = NULL;

                if (obj_expr->type == NODE_EXPR_UNARY && obj_expr->unary.op &&
                    strcmp(obj_expr->unary.op, "&") == 0)
                {
                    obj_expr = obj_expr->unary.operand;
                    iter_method = "iter_ref";
                }

                if (obj_expr->type_info && obj_expr->type_info->kind == TYPE_ARRAY &&
                    obj_expr->type_info->array_size > 0)
                {
                    slice_decl = ast_create(NODE_VAR_DECL);
                    slice_decl->token = tk;
                    slice_decl->var_decl.name = xstrdup("__zc_arr_slice");

                    char *elem_type_str = type_to_string(obj_expr->type_info->inner);
                    char slice_type[256];
                    sprintf(slice_type, "Slice<%s>", elem_type_str);
                    slice_decl->var_decl.type_str = xstrdup(slice_type);

                    ASTNode *from_array_call = ast_create(NODE_EXPR_CALL);
                    from_array_call->token = tk;
                    ASTNode *static_method = ast_create(NODE_EXPR_VAR);

                    char func_name[512];
                    snprintf(func_name, 511, "%s::from_array", slice_type);
                    static_method->var_ref.name = xstrdup(func_name);

                    from_array_call->call.callee = static_method;

                    ASTNode *arr_addr = ast_create(NODE_EXPR_UNARY);
                    arr_addr->unary.op = xstrdup("&");
                    arr_addr->unary.operand = obj_expr;

                    ASTNode *arr_cast = ast_create(NODE_EXPR_CAST);
                    char cast_type[256];
                    sprintf(cast_type, "%s*", elem_type_str);
                    arr_cast->cast.target_type = xstrdup(cast_type);
                    arr_cast->cast.expr = arr_addr;

                    ASTNode *size_arg = ast_create(NODE_EXPR_LITERAL);
                    size_arg->literal.type_kind = LITERAL_INT;
                    size_arg->literal.int_val = obj_expr->type_info->array_size;
                    char size_buf[32];
                    sprintf(size_buf, "%d", obj_expr->type_info->array_size);
                    size_arg->literal.string_val = xstrdup(size_buf);

                    arr_cast->next = size_arg;
                    from_array_call->call.args = arr_cast;
                    from_array_call->call.arg_count = 2;

                    slice_decl->var_decl.init_expr = from_array_call;

                    auto_import_std_slice(ctx);
                    Token dummy_tok = {0};
                    instantiate_generic(ctx, "Slice", elem_type_str, elem_type_str, dummy_tok);

                    char iter_type[256];
                    sprintf(iter_type, "SliceIter<%s>", elem_type_str);
                    instantiate_generic(ctx, "SliceIter", elem_type_str, elem_type_str, dummy_tok);

                    char option_type[256];
                    sprintf(option_type, "Option<%s>", elem_type_str);
                    instantiate_generic(ctx, "Option", elem_type_str, elem_type_str, dummy_tok);

                    ASTNode *slice_ref = ast_create(NODE_EXPR_VAR);
                    slice_ref->var_ref.name = xstrdup("__zc_arr_slice");
                    slice_ref->resolved_type = xstrdup(slice_type);
                    obj_expr = slice_ref;

                    free(elem_type_str);
                }

                ASTNode *it_decl = ast_create(NODE_VAR_DECL);
                it_decl->token = tk;
                it_decl->var_decl.name = xstrdup("__it");
                it_decl->var_decl.type_str = NULL;

                ASTNode *call_iter = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_iter = ast_create(NODE_EXPR_MEMBER);
                memb_iter->member.target = obj_expr;
                memb_iter->member.field = xstrdup(iter_method);
                memb_iter->token = tk;
                call_iter->token = tk;
                call_iter->call.callee = memb_iter;
                call_iter->call.args = NULL;
                call_iter->call.arg_count = 0;

                it_decl->var_decl.init_expr = call_iter;

                ASTNode *while_loop = ast_create(NODE_WHILE);
                ASTNode *true_lit = ast_create(NODE_EXPR_LITERAL);
                true_lit->literal.type_kind = LITERAL_INT;
                true_lit->literal.int_val = 1;
                true_lit->literal.string_val = xstrdup("1");
                true_lit->token = tk;
                while_loop->token = tk;
                while_loop->while_stmt.condition = true_lit;

                ASTNode *loop_body = ast_create(NODE_BLOCK);
                loop_body->token = tk;
                ASTNode *stmts_head = NULL;
                ASTNode *stmts_tail = NULL;

#define APPEND_STMT(node)                                                                          \
    if (!stmts_head)                                                                               \
    {                                                                                              \
        stmts_head = node;                                                                         \
        stmts_tail = node;                                                                         \
    }                                                                                              \
    else                                                                                           \
    {                                                                                              \
        stmts_tail->next = node;                                                                   \
        stmts_tail = node;                                                                         \
    }

                char *iter_type_ptr = NULL;
                char *option_type_ptr = NULL;

                if (slice_decl)
                {
                    char *slice_t = slice_decl->var_decl.type_str;
                    char *start = strchr(slice_t, '<');
                    if (start)
                    {
                        char *end = strrchr(slice_t, '>');
                        if (end)
                        {
                            int len = end - start - 1;
                            char *elem = xmalloc(len + 1);
                            strncpy(elem, start + 1, len);
                            elem[len] = 0;

                            iter_type_ptr = xmalloc(256);
                            sprintf(iter_type_ptr, "SliceIter<%s>", elem);

                            option_type_ptr = xmalloc(256);
                            sprintf(option_type_ptr, "Option<%s>", elem);

                            free(elem);
                        }
                    }
                }

                // var __opt = __it.next();
                ASTNode *opt_decl = ast_create(NODE_VAR_DECL);
                opt_decl->token = tk;
                opt_decl->var_decl.name = xstrdup("__opt");
                opt_decl->var_decl.type_str = NULL;

                // __it.next()
                ASTNode *call_next = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_next = ast_create(NODE_EXPR_MEMBER);
                ASTNode *it_ref = ast_create(NODE_EXPR_VAR);
                it_ref->var_ref.name = xstrdup("__it");
                if (iter_type_ptr)
                {
                    it_ref->resolved_type = xstrdup(iter_type_ptr);
                }
                memb_next->member.target = it_ref;
                memb_next->member.field = xstrdup("next");
                memb_next->token = tk;
                call_next->token = tk;
                call_next->call.callee = memb_next;

                opt_decl->var_decl.init_expr = call_next;
                APPEND_STMT(opt_decl);

                // __opt.is_none()
                ASTNode *call_is_none = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_is_none = ast_create(NODE_EXPR_MEMBER);
                ASTNode *opt_ref1 = ast_create(NODE_EXPR_VAR);
                opt_ref1->var_ref.name = xstrdup("__opt");
                if (option_type_ptr)
                {
                    opt_ref1->resolved_type = xstrdup(option_type_ptr);
                }
                memb_is_none->member.target = opt_ref1;
                memb_is_none->member.field = xstrdup("is_none");
                memb_is_none->token = tk;
                call_is_none->token = tk;
                call_is_none->call.callee = memb_is_none;
                call_is_none->call.args = NULL;
                call_is_none->call.arg_count = 0;

                // if (__opt.is_none()) break;
                ASTNode *if_break = ast_create(NODE_IF);
                if_break->token = tk;
                if_break->if_stmt.condition = call_is_none;

                ASTNode *break_blk = ast_create(NODE_BLOCK);
                break_blk->block.statements = ast_create(NODE_BREAK);
                break_blk->block.statements->token = tk;

                if_break->if_stmt.then_body = break_blk;
                if_break->if_stmt.else_body = NULL;
                APPEND_STMT(if_break);

                // var <user_var> = __opt.unwrap();
                ASTNode *user_var_decl = ast_create(NODE_VAR_DECL);
                user_var_decl->token = tk;
                user_var_decl->var_decl.name = var_name;
                user_var_decl->var_decl.type_str = NULL;

                // __opt.unwrap()
                ASTNode *call_unwrap = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_unwrap = ast_create(NODE_EXPR_MEMBER);
                ASTNode *opt_ref2 = ast_create(NODE_EXPR_VAR);
                opt_ref2->var_ref.name = xstrdup("__opt");
                if (option_type_ptr)
                {
                    opt_ref2->resolved_type = xstrdup(option_type_ptr);
                }
                memb_unwrap->member.target = opt_ref2;
                memb_unwrap->member.field = xstrdup("unwrap");
                memb_unwrap->token = tk;
                call_unwrap->token = tk;
                call_unwrap->call.callee = memb_unwrap;
                call_unwrap->call.args = NULL;
                call_unwrap->call.arg_count = 0;

                user_var_decl->var_decl.init_expr = call_unwrap;

                // If enumerated, bind idx before user_var
                if (enum_idx_name)
                {
                    ASTNode *idx_bind = ast_create(NODE_VAR_DECL);
                    idx_bind->token = tk;
                    idx_bind->var_decl.name = enum_idx_name;
                    idx_bind->var_decl.type_str = xstrdup("int");
                    idx_bind->var_decl.type_info = type_new(TYPE_INT);
                    ASTNode *idx_ref = ast_create(NODE_EXPR_VAR);
                    idx_ref->var_ref.name = xstrdup("__zc_enum_idx");
                    idx_bind->var_decl.init_expr = idx_ref;
                    APPEND_STMT(idx_bind);
                }

                APPEND_STMT(user_var_decl);

                // User body statements
                enter_scope(ctx);
                add_symbol(ctx, var_name, NULL, NULL);
                if (enum_idx_name)
                {
                    add_symbol(ctx, enum_idx_name, "int", type_new(TYPE_INT));
                }

                // Body block
                ASTNode *stmt = parse_statement(ctx, l);
                ASTNode *user_body_node = stmt;
                if (stmt && stmt->type != NODE_BLOCK)
                {
                    ASTNode *blk = ast_create(NODE_BLOCK);
                    blk->block.statements = stmt;
                    user_body_node = blk;
                }
                exit_scope(ctx);

                // Append user body statements to our loop body
                APPEND_STMT(user_body_node);

                // If enumerated, append __zc_enum_idx++
                if (enum_idx_name)
                {
                    ASTNode *idx_inc = ast_create(NODE_EXPR_UNARY);
                    idx_inc->unary.op = xstrdup("++");
                    ASTNode *idx_ref3 = ast_create(NODE_EXPR_VAR);
                    idx_ref3->var_ref.name = xstrdup("__zc_enum_idx");
                    idx_inc->unary.operand = idx_ref3;
                    APPEND_STMT(idx_inc);
                }

                loop_body->block.statements = stmts_head;
                while_loop->while_stmt.body = loop_body;

                // Wrap entire thing in a block to scope __it (and __zc_arr_slice if present)
                ASTNode *outer_block = ast_create(NODE_BLOCK);

                // If enumerated, add __zc_enum_idx decl to the chain
                ASTNode *enum_idx_decl_node = NULL;
                if (enum_idx_name)
                {
                    enum_idx_decl_node = ast_create(NODE_VAR_DECL);
                    enum_idx_decl_node->token = tk;
                    enum_idx_decl_node->var_decl.name = xstrdup("__zc_enum_idx");
                    enum_idx_decl_node->var_decl.type_str = xstrdup("int");
                    enum_idx_decl_node->var_decl.type_info = type_new(TYPE_INT);
                    ASTNode *zero_lit = ast_create(NODE_EXPR_LITERAL);
                    zero_lit->literal.type_kind = LITERAL_INT;
                    zero_lit->literal.int_val = 0;
                    zero_lit->literal.string_val = xstrdup("0");
                    enum_idx_decl_node->var_decl.init_expr = zero_lit;
                }

                if (slice_decl)
                {
                    if (enum_idx_decl_node)
                    {
                        enum_idx_decl_node->next = slice_decl;
                        slice_decl->next = it_decl;
                        it_decl->next = while_loop;
                        outer_block->block.statements = enum_idx_decl_node;
                    }
                    else
                    {
                        slice_decl->next = it_decl;
                        it_decl->next = while_loop;
                        outer_block->block.statements = slice_decl;
                    }
                }
                else
                {
                    if (enum_idx_decl_node)
                    {
                        enum_idx_decl_node->next = it_decl;
                        it_decl->next = while_loop;
                        outer_block->block.statements = enum_idx_decl_node;
                    }
                    else
                    {
                        it_decl->next = while_loop;
                        outer_block->block.statements = it_decl;
                    }
                }

                return outer_block;
            }
        }
        l->pos = saved_pos; // Restore
    }

    // C-Style For Loop
    enter_scope(ctx);
    if (lexer_peek(l).type == TOK_LPAREN)
    {
        lexer_next(l);
    }

    ASTNode *init = NULL;
    if (lexer_peek(l).type != TOK_SEMICOLON)
    {
        if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "let", 3) == 0)
        {
            init = parse_var_decl(ctx, l);
        }
        else if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "var", 3) == 0)
        {
            zpanic_at(lexer_peek(l), "'var' is deprecated. Use 'let' instead.");
        }
        else
        {
            init = parse_expression(ctx, l);
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
    }
    else
    {
        lexer_next(l);
    }

    ASTNode *cond = NULL;
    if (lexer_peek(l).type != TOK_SEMICOLON)
    {
        cond = parse_expression(ctx, l);
    }
    else
    {
        // Empty condition = true
        ASTNode *true_lit = ast_create(NODE_EXPR_LITERAL);
        true_lit->literal.type_kind = LITERAL_INT;
        true_lit->literal.int_val = 1;
        cond = true_lit;
    }
    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *step = NULL;
    if (lexer_peek(l).type != TOK_RPAREN && lexer_peek(l).type != TOK_LBRACE)
    {
        step = parse_expression(ctx, l);
    }

    if (lexer_peek(l).type == TOK_RPAREN)
    {
        lexer_next(l);
    }

    ASTNode *body;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        body = parse_statement(ctx, l);
    }
    exit_scope(ctx);

    ASTNode *n = ast_create(NODE_FOR);
    n->token = for_token;
    n->for_stmt.init = init;
    n->for_stmt.condition = cond;
    n->for_stmt.step = step;
    n->for_stmt.body = body;
    return n;
}

char *process_printf_sugar(ParserContext *ctx, Token srctoken, const char *content, int newline,
                           const char *target, char ***used_syms, int *count, int check_symbols)
{
    int saved_silent = ctx->silent_warnings;
    ctx->silent_warnings = !check_symbols;
    char *gen = xmalloc(8192);
    strcpy(gen, "({ ");

    char *s = xstrdup(content);
    char *cur = s;

    while (*cur)
    {
        char *brace = cur;
        while (*brace)
        {
            if (*brace == '{')
            {
                if (brace[1] == '{')
                {
                    brace += 2;
                    continue;
                }
                break; // Found single '{'
            }
            if (*brace == '}' && brace[1] == '}')
            {
                // We'll treat '}}' as part of literal text and unescape it later
                brace += 2;
                continue;
            }
            brace++;
        }

        if (brace > cur)
        {
            // Append text literal
            char buf[256];
            sprintf(buf, "fprintf(%s, \"%%s\", \"", target);
            strcat(gen, buf);

            int seg_len = brace - cur;
            char *txt = xmalloc(seg_len + 1);
            int write_idx = 0;
            for (int i = 0; i < seg_len; i++)
            {
                if (cur[i] == '{' && cur[i + 1] == '{')
                {
                    txt[write_idx++] = '{';
                    i++;
                }
                else if (cur[i] == '}' && cur[i + 1] == '}')
                {
                    txt[write_idx++] = '}';
                    i++;
                }
                else
                {
                    txt[write_idx++] = cur[i];
                }
            }
            txt[write_idx] = 0;

            char *escaped = escape_c_string(txt);
            strcat(gen, escaped);
            strcat(gen, "\"); ");

            free(escaped);
            free(txt);
        }

        if (*brace == 0)
        {
            break;
        }

        char *p = brace + 1;
        char *colon = NULL;
        int depth = 1;
        while (*p && depth > 0)
        {
            if (*p == '{')
            {
                depth++;
            }
            if (*p == '}')
            {
                depth--;
            }
            if (depth == 1 && *p == ':' && !colon)
            {
                if (*(p + 1) == ':')
                {
                    p++;
                }
                else
                {
                    colon = p;
                }
            }
            if (depth == 0)
            {
                break;
            }
            p++;
        }

        *p = 0; // Terminate expression
        char *expr = brace + 1;

        // Unescape \" to " in the expression code to ensure correct parsing
        char *read = expr;
        char *write = expr;
        while (*read)
        {
            if (*read == '\\' && *(read + 1) == '"')
            {
                *write = '"';
                read += 2;
                write++;
            }
            else
            {
                *write = *read;
                read++;
                write++;
            }
        }
        *write = 0;
        char *fmt = NULL;
        if (colon)
        {
            *colon = 0;
            fmt = colon + 1;
        }

        char *clean_expr = expr;
        while (*clean_expr == ' ')
        {
            clean_expr++; // Skip leading spaces
        }

        // Analyze usage & Type Check for to_string()
        char *final_expr = xstrdup(clean_expr);

        if (check_symbols)
        {
            Lexer lex;
            lexer_init(&lex, clean_expr); // Scan original for symbols
            lex.line = srctoken.line;
            lex.col = srctoken.col;

            Token t;
            Token prev = {0};
            while ((t = lexer_next(&lex)).type != TOK_EOF)
            {
                if (t.type == TOK_IDENT)
                {
                    // Skip if preceded by '.' (member access)
                    if (prev.type == TOK_OP && prev.len == 1 && prev.start[0] == '.')
                    {
                        prev = t;
                        continue;
                    }

                    char *name = token_strdup(t);
                    ZenSymbol *sym = find_symbol_entry(ctx, name);
                    if (sym)
                    {
                        sym->is_used = 1;
                    }

                    if (used_syms && count)
                    {
                        *used_syms = xrealloc(*used_syms, sizeof(char *) * (*count + 1));
                        (*used_syms)[*count] = name;
                        (*count)++;
                    }
                    else
                    {
                        free(name);
                    }
                }
                prev = t;
            }
        }

        expr = final_expr;
        clean_expr = final_expr;

        // Parse expression fully
        Lexer lex;
        lexer_init(&lex, clean_expr);
        lex.line = srctoken.line;
        lex.col = srctoken.col;

        ASTNode *expr_node = parse_expression(ctx, &lex);

        char *rw_expr = NULL;
        int used_codegen = 0;

        if (expr_node)
        {
            // Check for to_string conversion on struct types
            if (expr_node->type_info)
            {
                Type *t = expr_node->type_info;
                char *struct_name = NULL;
                int is_ptr = 0;

                if (t->kind == TYPE_STRUCT)
                {
                    struct_name = t->name;
                }
                else if (t->kind == TYPE_POINTER && t->inner && t->inner->kind == TYPE_STRUCT)
                {
                    struct_name = t->inner->name;
                    is_ptr = 1;
                }

                if (struct_name)
                {
                    size_t mangled_sz = strlen(struct_name) + sizeof("__to_string");
                    char *mangled = xmalloc(mangled_sz);
                    snprintf(mangled, mangled_sz, "%s__to_string", struct_name);
                    if (find_func(ctx, mangled))
                    {
                        char *inner_c = NULL;
                        size_t len = 0;
                        FILE *ms = tmpfile();
                        if (ms)
                        {
                            codegen_expression(ctx, expr_node, ms);
                            len = ftell(ms);
                            fseek(ms, 0, SEEK_SET);
                            inner_c = xmalloc(len + 1);
                            fread(inner_c, 1, len, ms);
                            inner_c[len] = 0;
                            fclose(ms);
                        }

                        if (inner_c)
                        {
                            char *new_expr = xmalloc(strlen(inner_c) + strlen(mangled) + 64);
                            if (is_ptr)
                            {
                                sprintf(new_expr, "%s(%s)", mangled, inner_c);
                            }
                            else
                            {
                                sprintf(new_expr, "%s(({ %s _z_tmp = (%s); &_z_tmp; }))", mangled,
                                        struct_name, inner_c);
                            }
                            rw_expr = new_expr;
                            free(inner_c);
                        }
                    }
                    free(mangled);
                }
            }

            if (!rw_expr)
            {
                char *buf = NULL;
                size_t len = 0;
                FILE *ms = tmpfile();
                if (ms)
                {
                    codegen_expression(ctx, expr_node, ms);
                    len = ftell(ms);
                    fseek(ms, 0, SEEK_SET);
                    buf = xmalloc(len + 1);
                    fread(buf, 1, len, ms);
                    buf[len] = 0;
                    fclose(ms);
                    rw_expr = buf;
                    used_codegen = 1;
                }
            }
        }

        if (!rw_expr)
        {
            rw_expr = xstrdup(expr); // Fallback
        }

        if (fmt)
        {
            // Explicit format: {x:%.2f}
            char buf[128];
            sprintf(buf, "fprintf(%s, \"%%", target);
            strcat(gen, buf);
            strcat(gen, fmt);
            strcat(gen, "\", ");
            strcat(gen, rw_expr); // Use rewritten expr
            strcat(gen, "); ");
        }
        else
        {
            const char *format_spec = NULL;
            Type *t = expr_node ? expr_node->type_info : NULL;
            char *inferred_type = t ? type_to_string(t) : find_symbol_type(ctx, clean_expr);

            int is_bool = 0;

            // Robust string-like detection (Slice/Vector or compatible struct)
            // MUST run before name-based heuristics to handle pointers to slices/vectors correctly
            if (t)
            {
                Type *base = t;
                int is_p = 0;
                if (base->kind == TYPE_POINTER)
                {
                    base = base->inner;
                    is_p = 1;
                }

                while (base)
                {
                    if (base->kind == TYPE_ALIAS)
                    {
                        base = base->inner;
                    }
                    else if (base->kind == TYPE_STRUCT && base->name)
                    {
                        TypeAlias *ta = find_type_alias_node(ctx, base->name);
                        if (ta && ta->type_info)
                        {
                            base = ta->type_info;
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }

                if (base && base->kind == TYPE_ARRAY && base->array_size == 0)
                {
                    char *inner_name = type_to_string(base->inner);
                    char slice_name[256];
                    sprintf(slice_name, "Slice_%s", inner_name);
                    free(inner_name);

                    ASTNode *def = find_struct_def(ctx, slice_name);
                    if (def && def->type == NODE_STRUCT)
                    {
                        int has_data = 0;
                        int has_len = 0;
                        char *data_type = NULL;

                        ASTNode *curr = def->strct.fields;
                        while (curr)
                        {
                            if (strcmp(curr->field.name, "data") == 0)
                            {
                                has_data = 1;
                                data_type = curr->field.type;
                            }
                            else if (strcmp(curr->field.name, "len") == 0)
                            {
                                has_len = 1;
                            }
                            curr = curr->next;
                        }

                        if (has_data && has_len && data_type &&
                            (strstr(data_type, "char") || strstr(data_type, "u8") ||
                             strstr(data_type, "byte")))
                        {
                            char buf[512];
                            const char *acc = is_p ? "->" : ".";
                            sprintf(buf, "fprintf(%s, \"%%.*s\", (int)(%s)%slen, (%s)%sdata); ",
                                    target, rw_expr, acc, rw_expr, acc);
                            strcat(gen, buf);
                            goto next_segment;
                        }
                    }
                }
                else if (base && base->kind == TYPE_STRUCT && base->name)
                {
                    ASTNode *def = find_struct_def(ctx, base->name);
                    if (def && def->type == NODE_STRUCT)
                    {
                        int has_data = 0;
                        int has_len = 0;
                        char *data_type = NULL;

                        ASTNode *curr = def->strct.fields;
                        while (curr)
                        {
                            if (strcmp(curr->field.name, "data") == 0)
                            {
                                has_data = 1;
                                data_type = curr->field.type;
                            }
                            else if (strcmp(curr->field.name, "len") == 0)
                            {
                                has_len = 1;
                            }
                            curr = curr->next;
                        }

                        if (has_data && has_len && data_type &&
                            (strstr(data_type, "char") || strstr(data_type, "u8") ||
                             strstr(data_type, "byte")))
                        {
                            char buf[512];
                            const char *acc = is_p ? "->" : ".";
                            sprintf(buf, "fprintf(%s, \"%%.*s\", (int)(%s)%slen, (%s)%sdata); ",
                                    target, rw_expr, acc, rw_expr, acc);
                            strcat(gen, buf);
                            goto next_segment;
                        }
                    }
                }
            }

            if (t && t->kind == TYPE_RUNE)
            {
                format_spec = "%s";
                char *orig = rw_expr;
                rw_expr = xmalloc(strlen(orig) + 32);
                sprintf(rw_expr, "_z_str_rune(%s)", orig);
            }
            else if (inferred_type)
            {
                if (strcmp(inferred_type, "bool") == 0)
                {
                    format_spec = "%s";
                    is_bool = 1;
                }
                else if (strcmp(inferred_type, "int") == 0 || strcmp(inferred_type, "i32") == 0 ||
                         strcmp(inferred_type, "I32") == 0 ||
                         strcmp(inferred_type, "int32_t") == 0 ||
                         strcmp(inferred_type, "i16") == 0 || strcmp(inferred_type, "I16") == 0 ||
                         strcmp(inferred_type, "int16_t") == 0 ||
                         strcmp(inferred_type, "i8") == 0 || strcmp(inferred_type, "I8") == 0 ||
                         strcmp(inferred_type, "int8_t") == 0 ||
                         strcmp(inferred_type, "short") == 0 ||
                         strcmp(inferred_type, "ushort") == 0)
                {
                    format_spec = "%d";
                }
                else if (strcmp(inferred_type, "uint") == 0 || strcmp(inferred_type, "u32") == 0 ||
                         strcmp(inferred_type, "U32") == 0 ||
                         strcmp(inferred_type, "uint32_t") == 0 ||
                         strcmp(inferred_type, "u16") == 0 || strcmp(inferred_type, "U16") == 0 ||
                         strcmp(inferred_type, "uint16_t") == 0 ||
                         strcmp(inferred_type, "u8") == 0 || strcmp(inferred_type, "U8") == 0 ||
                         strcmp(inferred_type, "uint8_t") == 0 ||
                         strcmp(inferred_type, "byte") == 0)
                {
                    format_spec = "%u";
                }
                else if (strcmp(inferred_type, "long") == 0 || strcmp(inferred_type, "i64") == 0 ||
                         strcmp(inferred_type, "I64") == 0 ||
                         strcmp(inferred_type, "int64_t") == 0 ||
                         strcmp(inferred_type, "isize") == 0 ||
                         strcmp(inferred_type, "ptrdiff_t") == 0)
                {
                    format_spec = "%ld";
                }
                else if (strcmp(inferred_type, "usize") == 0 || strcmp(inferred_type, "u64") == 0 ||
                         strcmp(inferred_type, "U64") == 0 ||
                         strcmp(inferred_type, "uint64_t") == 0 ||
                         strcmp(inferred_type, "size_t") == 0 ||
                         strcmp(inferred_type, "ulong") == 0)
                {
                    format_spec = "%lu";
                }
                else if (strcmp(inferred_type, "float") == 0 || strcmp(inferred_type, "f32") == 0 ||
                         strcmp(inferred_type, "F32") == 0 ||
                         strcmp(inferred_type, "double") == 0 ||
                         strcmp(inferred_type, "f64") == 0 || strcmp(inferred_type, "F64") == 0)
                {
                    format_spec = "%f";
                }
                else if (strcmp(inferred_type, "char") == 0)
                {
                    format_spec = "%c";
                }
                else if (strcmp(inferred_type, "string") == 0 ||
                         strcmp(inferred_type, "str") == 0 ||
                         (inferred_type[strlen(inferred_type) - 1] == '*' &&
                          strstr(inferred_type, "char")))
                {
                    format_spec = "%s";
                }
                else if (strstr(inferred_type, "*"))
                {
                    format_spec = "%p"; // Pointer
                }

                if (t)
                {
                    free(inferred_type);
                }
            }

            if (!format_spec)
            {
                if (isdigit(clean_expr[0]) || clean_expr[0] == '-')
                {
                    if (strchr(clean_expr, '.') || strchr(clean_expr, 'e') ||
                        strchr(clean_expr, 'E'))
                    {
                        format_spec = "%f";
                    }
                    else
                    {
                        format_spec = "%d";
                    }
                }
                else if (clean_expr[0] == '"')
                {
                    format_spec = "%s";
                }
                else if (clean_expr[0] == '\'')
                {
                    format_spec = "%c";
                }
            }

            if (format_spec)
            {
                char buf[128];
                sprintf(buf, "fprintf(%s, \"", target);
                strcat(gen, buf);
                strcat(gen, format_spec);
                strcat(gen, "\", ");
                if (is_bool)
                {
                    strcat(gen, "_z_bool_str(");
                    strcat(gen, rw_expr);
                    strcat(gen, ")");
                }
                else
                {
                    strcat(gen, rw_expr);
                }
                strcat(gen, "); ");
            }
            else
            {
                // Fallback to runtime macro
                char buf[128];
                sprintf(buf, "fprintf(%s, _z_str(", target);
                strcat(gen, buf);
                strcat(gen, rw_expr);
                strcat(gen, "), _z_arg(");
                strcat(gen, rw_expr);
                strcat(gen, ")); ");
            }
        }

    next_segment:
        if (rw_expr && used_codegen)
        {
            free(rw_expr);
        }
        else if (rw_expr && !used_codegen)
        {
            free(rw_expr);
        }

        cur = p + 1;
    }

    if (newline)
    {
        char buf[128];
        sprintf(buf, "fprintf(%s, \"\\n\"); ", target);
        strcat(gen, buf);
    }
    else
    {
        strcat(gen, "fflush(stdout); ");
    }

    strcat(gen, "0; })");

    free(s);
    ctx->silent_warnings = saved_silent;
    return gen;
}

ASTNode *parse_macro_call(ParserContext *ctx, Lexer *l, char *macro_name)
{
    Token start_tok = lexer_peek(l);
    if (lexer_peek(l).type != TOK_OP || lexer_peek(l).start[0] != '!')
    {
        return NULL;
    }
    lexer_next(l); // consume !

    // Expect {
    if (lexer_peek(l).type != TOK_LBRACE)
    {
        zpanic_at(lexer_peek(l), "Expected { after macro invocation");
    }
    lexer_next(l); // consume {

    // Collect body until }
    char *body = xmalloc(8192);
    body[0] = '\0';
    int body_len = 0;
    int depth = 1;
    int last_line = start_tok.line;

    while (depth > 0)
    {
        Token t = lexer_peek(l);
        if (t.type == TOK_EOF)
        {
            zpanic_at(t, "Unexpected EOF in macro block");
        }

        if (t.type == TOK_LBRACE)
        {
            depth++;
        }
        if (t.type == TOK_RBRACE)
        {
            depth--;
        }

        if (depth > 0)
        {
            if (body_len + t.len + 2 < 8192)
            {
                // Preserve newlines
                if (t.line > last_line)
                {
                    body[body_len] = '\n';
                    body[body_len + 1] = 0;
                    body_len++;
                }
                else
                {
                    body[body_len] = ' ';
                    body[body_len + 1] = 0;
                    body_len++;
                }

                strncat(body, t.start, t.len);
                body_len += t.len;
            }
        }

        last_line = t.line;
        lexer_next(l);
    }

    // Resolve plugin name
    const char *plugin_name = resolve_plugin(ctx, macro_name);
    if (!plugin_name)
    {
        char err[256];
        snprintf(err, sizeof(err), "Unknown plugin: %s (did you forget 'import plugin \"%s\"'?)",
                 macro_name, macro_name);
        zpanic_at(start_tok, "%s", err);
    }

    // Find Plugin Definition
    // Verify plugin exists
    ZPlugin *found = zptr_find_plugin(plugin_name);

    if (!found)
    {
        char err[256];
        snprintf(err, sizeof(err), "Plugin implementation not found: %s", plugin_name);
        zpanic_at(start_tok, "%s", err);
    }

    // Execute Plugin Immediately (Expansion)
    FILE *capture = tmpfile();
    if (!capture)
    {
        zpanic_at(start_tok, "Failed to create capture buffer for plugin expansion");
    }

    ZApi api = {.filename = g_current_filename ? g_current_filename : "input.zc",
                .current_line = start_tok.line,
                .out = capture,
                .hoist_out = ctx->hoist_out};

    found->fn(body, &api);

    // Read captured output
    long len = ftell(capture);
    rewind(capture);
    char *expanded_code = xmalloc(len + 1);
    fread(expanded_code, 1, len, capture);
    expanded_code[len] = 0;
    fclose(capture);
    free(body);

    // Create Raw Statement/Expression Node
    ASTNode *n = ast_create(NODE_RAW_STMT);
    n->token = start_tok;
    n->line = start_tok.line;
    n->raw_stmt.content = expanded_code;

    return n;
}

ASTNode *parse_statement(ParserContext *ctx, Lexer *l)
{
    int prev_emit = l->emit_comments;
    if (g_config.keep_comments)
    {
        l->emit_comments = 1;
    }
    Token tk = lexer_peek(l);
    l->emit_comments = prev_emit;

    if (tk.type == TOK_COMMENT)
    {
        l->emit_comments = 1;
        lexer_next(l); // consume comment
        l->emit_comments = prev_emit;

        ASTNode *node = ast_create(NODE_AST_COMMENT);
        node->comment.content = xmalloc(tk.len + 1);
        strncpy(node->comment.content, tk.start, tk.len);
        node->comment.content[tk.len] = 0;
        return node;
    }

    ASTNode *s = NULL;

    if (tk.type == TOK_SEMICOLON)
    {
        lexer_next(l);
        ASTNode *nop = ast_create(NODE_BLOCK); // Empty block as NOP
        nop->block.statements = NULL;
        return nop;
    }

    if (tk.type == TOK_PREPROC)
    {
        tk = lexer_next(l); // consume token
        char *content = xmalloc(tk.len + 2);
        strncpy(content, tk.start, tk.len);
        content[tk.len] = '\n'; // Ensure newline
        content[tk.len + 1] = 0;
        ASTNode *raw_s = ast_create(NODE_RAW_STMT);
        raw_s->token = tk;
        raw_s->raw_stmt.content = content;
        return raw_s;
    }

    if (tk.type == TOK_STRING || tk.type == TOK_FSTRING)
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token next = lexer_peek(&lookahead);
        ZenTokenType next_type = next.type;

        if (next_type == TOK_SEMICOLON || next_type == TOK_DOTDOT || next_type == TOK_RBRACE)
        {
            Token t = lexer_next(l); // consume string

            char *inner = xmalloc(t.len);
            // Strip quotes
            if (t.type == TOK_FSTRING)
            {
                int is_multi =
                    (t.len >= 7 && t.start[1] == '"' && t.start[2] == '"' && t.start[3] == '"');
                int start_offset = is_multi ? 4 : 2;
                int end_offset = is_multi ? 3 : 1;
                strncpy(inner, t.start + start_offset, t.len - start_offset - end_offset);
                inner[t.len - start_offset - end_offset] = 0;
            }
            else
            {
                int is_multi =
                    (t.len >= 6 && t.start[0] == '"' && t.start[1] == '"' && t.start[2] == '"');
                int start_offset = is_multi ? 3 : 1;
                int end_offset = is_multi ? 3 : 1;
                strncpy(inner, t.start + start_offset, t.len - start_offset - end_offset);
                inner[t.len - start_offset - end_offset] = 0;
            }

            int is_ln = (next_type == TOK_SEMICOLON || next_type == TOK_RBRACE);
            char **used_syms = NULL;
            int used_count = 0;
            char *code =
                process_printf_sugar(ctx, next, inner, is_ln, "stdout", &used_syms, &used_count, 1);

            if (next_type == TOK_SEMICOLON)
            {
                lexer_next(l); // consume ;
            }
            else if (next_type == TOK_DOTDOT)
            {
                lexer_next(l); // consume ..
                if (lexer_peek(l).type == TOK_SEMICOLON)
                {
                    lexer_next(l); // consume optional ;
                }
            }
            // If TOK_RBRACE, do not consume it, so parse_block can see it and terminate loop.

            ASTNode *n = ast_create(NODE_RAW_STMT);
            n->token = tk;
            // Append semicolon to Statement Expression to make it a valid statement
            char *stmt_code = xmalloc(strlen(code) + 2);
            sprintf(stmt_code, "%s;", code);
            free(code);
            n->raw_stmt.content = stmt_code;
            n->raw_stmt.used_symbols = used_syms;
            n->raw_stmt.used_symbol_count = used_count;
            free(inner);
            return n;
        }
    }

    // Block
    if (tk.type == TOK_LBRACE)
    {
        return parse_block(ctx, l);
    }

    // Keywords / Special
    if (tk.type == TOK_DO)
    {
        Token do_tok = lexer_next(l); // eat 'do'
        ASTNode *body = parse_block(ctx, l);

        // Expect 'while'
        Token while_tok = lexer_peek(l);
        if (while_tok.type != TOK_IDENT || strncmp(while_tok.start, "while", 5) != 0 ||
            while_tok.len != 5)
        {
            zpanic_at(while_tok, "Expected 'while' after do block");
        }
        lexer_next(l); // eat 'while'

        ASTNode *cond = parse_expression(ctx, l);
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        ASTNode *n = ast_create(NODE_DO_WHILE);
        n->token = do_tok;
        n->do_while_stmt.body = body;
        n->do_while_stmt.condition = cond;
        n->do_while_stmt.loop_label = NULL;
        return n;
    }
    if (tk.type == TOK_TRAIT)
    {
        return parse_trait(ctx, l);
    }
    if (tk.type == TOK_IMPL)
    {
        return parse_impl(ctx, l);
    }
    if (tk.type == TOK_IDENT && strncmp(tk.start, "struct", 6) == 0 && tk.len == 6)
    {
        return parse_struct(ctx, l, 0, 0);
    }
    if (tk.type == TOK_UNION)
    {
        return parse_struct(ctx, l, 1, 0);
    }
    if (tk.type == TOK_IDENT && strncmp(tk.start, "enum", 4) == 0 && tk.len == 4)
    {
        return parse_enum(ctx, l);
    }

    if (tk.type == TOK_AUTOFREE)
    {
        lexer_next(l);
        if (lexer_peek(l).type != TOK_IDENT || strncmp(lexer_peek(l).start, "let", 3) != 0)
        {
            zpanic_at(lexer_peek(l), "Expected 'let' after autofree");
        }
        s = parse_var_decl(ctx, l);
        s->var_decl.is_autofree = 1;
        // Mark symbol as autofree to suppress unused variable warning
        ZenSymbol *sym = find_symbol_entry(ctx, s->var_decl.name);
        if (sym)
        {
            sym->is_autofree = 1;
        }
        return s;
    }
    if (tk.type == TOK_TEST)
    {
        return parse_test(ctx, l);
    }
    if (tk.type == TOK_COMPTIME)
    {
        char *src = run_comptime_block(ctx, l);
        Lexer new_l;
        lexer_init(&new_l, src);
        ASTNode *head = NULL, *tail = NULL;

        while (lexer_peek(&new_l).type != TOK_EOF)
        {
            ASTNode *inner_s = parse_statement(ctx, &new_l);
            if (!inner_s)
            {
                break;
            }
            if (!head)
            {
                head = inner_s;
            }
            else
            {
                tail->next = inner_s;
            }
            tail = inner_s;
            while (tail->next)
            {
                tail = tail->next;
            }
        }

        if (head && !head->next)
        {
            return head;
        }

        ASTNode *b = ast_create(NODE_BLOCK);
        b->block.statements = head;
        return b;
    }
    if (tk.type == TOK_ASSERT)
    {
        return parse_assert(ctx, l);
    }
    if (tk.type == TOK_DEFER)
    {
        return parse_defer(ctx, l);
    }
    if (tk.type == TOK_ASM)
    {
        return parse_asm(ctx, l);
    }
    if (tk.type == TOK_DEF)
    {
        return parse_def(ctx, l);
    }

    // Identifiers (Keywords or Expressions)
    if (tk.type == TOK_IDENT)
    {
        // Check for macro invocation: identifier! { code }
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token exclaim = lexer_peek(&lookahead);
        lexer_next(&lookahead);
        Token lbrace = lexer_peek(&lookahead);
        if (exclaim.type == TOK_OP && exclaim.len == 1 && exclaim.start[0] == '!' &&
            lbrace.type == TOK_LBRACE)
        {
            // This is a macro invocation
            char *macro_name = token_strdup(tk);
            lexer_next(l); // consume identifier

            ASTNode *n = parse_macro_call(ctx, l, macro_name);
            free(macro_name);
            return n;
        }

        // Check for raw blocks
        if (strncmp(tk.start, "raw", 3) == 0 && tk.len == 3)
        {
            lexer_next(l); // eat raw
            if (lexer_peek(l).type != TOK_LBRACE)
            {
                zpanic_at(lexer_peek(l), "Expected { after raw");
            }
            lexer_next(l); // eat {

            const char *start = l->src + l->pos;
            int depth = 1;
            while (depth > 0)
            {
                Token t = lexer_next(l);
                if (t.type == TOK_EOF)
                {
                    zpanic_at(t, "Unexpected EOF in raw block");
                }
                if (t.type == TOK_LBRACE)
                {
                    depth++;
                }
                if (t.type == TOK_RBRACE)
                {
                    depth--;
                }
            }
            const char *end = l->src + l->pos - 1;
            size_t len = end - start;

            char *content = xmalloc(len + 1);
            memcpy(content, start, len);
            content[len] = 0;

            ASTNode *raw_s = ast_create(NODE_RAW_STMT);
            raw_s->token = tk;
            raw_s->raw_stmt.content = normalize_raw_content(content);
            free(content);
            return raw_s;
        }

        // Check for plugin blocks
        if (strncmp(tk.start, "plugin", 6) == 0 && tk.len == 6)
        {
            lexer_next(l); // consume 'plugin'
            return parse_plugin(ctx, l);
        }

        if (strncmp(tk.start, "let", 3) == 0 && tk.len == 3)
        {
            return parse_var_decl(ctx, l);
        }

        if (strncmp(tk.start, "var", 3) == 0 && tk.len == 3)
        {
            zpanic_at(tk, "'var' is deprecated. Use 'let' instead.");
            return parse_var_decl(ctx, l);
        }

        // Static local variable: static let x = 0;
        if (strncmp(tk.start, "static", 6) == 0 && tk.len == 6)
        {
            lexer_next(l); // eat 'static'
            Token next = lexer_peek(l);
            if (strncmp(next.start, "let", 3) == 0 && next.len == 3)
            {
                ASTNode *v = parse_var_decl(ctx, l);
                v->var_decl.is_static = 1;
                return v;
            }
            zpanic_at(next, "Expected 'let' after 'static'");
        }

        if (strncmp(tk.start, "const", 5) == 0 && tk.len == 5)
        {
            zpanic_at(tk, "'const' for declarations is deprecated. Use 'def' for constants or 'let "
                          "x: const T' for read-only variables.");
        }
        if (strncmp(tk.start, "return", 6) == 0 && tk.len == 6)
        {
            return parse_return(ctx, l);
        }
        if (strncmp(tk.start, "if", 2) == 0 && tk.len == 2)
        {
            return parse_if(ctx, l);
        }
        if (strncmp(tk.start, "while", 5) == 0 && tk.len == 5)
        {
            return parse_while(ctx, l);
        }
        if (strncmp(tk.start, "for", 3) == 0 && tk.len == 3)
        {
            return parse_for(ctx, l);
        }
        if (strncmp(tk.start, "match", 5) == 0 && tk.len == 5)
        {
            return parse_match(ctx, l);
        }

        // Break with optional label: break; or break 'outer;
        if (strncmp(tk.start, "break", 5) == 0 && tk.len == 5)
        {
            Token break_token = lexer_next(l);

            // Error if break is used inside a defer block
            if (ctx->in_defer_block)
            {
                zpanic_at(break_token, "'break' is not allowed inside a 'defer' block");
            }

            ASTNode *n = ast_create(NODE_BREAK);
            n->token = break_token;
            n->break_stmt.target_label = NULL;
            // Check for 'label or label
            if (lexer_peek(l).type == TOK_CHAR || lexer_peek(l).type == TOK_IDENT)
            {
                Token label_tok = lexer_next(l);
                if (label_tok.type == TOK_CHAR)
                {
                    // Extract label name (strip quotes)
                    char *label = xmalloc(label_tok.len);
                    strncpy(label, label_tok.start + 1, label_tok.len - 2);
                    label[label_tok.len - 2] = 0;
                    n->break_stmt.target_label = label;
                }
                else
                {
                    n->break_stmt.target_label = token_strdup(label_tok);
                }
            }
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
            return n;
        }

        // Continue with optional label
        if (strncmp(tk.start, "continue", 8) == 0 && tk.len == 8)
        {
            Token continue_token = lexer_next(l);

            // Error if continue is used inside a defer block
            if (ctx->in_defer_block)
            {
                zpanic_at(continue_token, "'continue' is not allowed inside a 'defer' block");
            }

            ASTNode *n = ast_create(NODE_CONTINUE);
            n->token = continue_token;
            n->continue_stmt.target_label = NULL;
            if (lexer_peek(l).type == TOK_CHAR || lexer_peek(l).type == TOK_IDENT)
            {
                Token label_tok = lexer_next(l);
                if (label_tok.type == TOK_CHAR)
                {
                    char *label = xmalloc(label_tok.len);
                    strncpy(label, label_tok.start + 1, label_tok.len - 2);
                    label[label_tok.len - 2] = 0;
                    n->continue_stmt.target_label = label;
                }
                else
                {
                    n->continue_stmt.target_label = token_strdup(label_tok);
                }
            }
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
            return n;
        }

        if (strncmp(tk.start, "loop", 4) == 0 && tk.len == 4)
        {
            return parse_loop(ctx, l);
        }
        if (strncmp(tk.start, "repeat", 6) == 0 && tk.len == 6)
        {
            return parse_repeat(ctx, l);
        }
        if (strncmp(tk.start, "unless", 6) == 0 && tk.len == 6)
        {
            return parse_unless(ctx, l);
        }
        if (strncmp(tk.start, "guard", 5) == 0 && tk.len == 5)
        {
            return parse_guard(ctx, l);
        }

        // CUDA launch: launch kernel(args) with { grid: X, block: Y };
        if (strncmp(tk.start, "launch", 6) == 0 && tk.len == 6)
        {
            Token launch_tok = lexer_next(l); // eat 'launch'

            // Parse the kernel call expression
            ASTNode *call = parse_expression(ctx, l);
            if (!call || call->type != NODE_EXPR_CALL)
            {
                zpanic_at(launch_tok, "Expected kernel call after 'launch'");
            }

            // Expect 'with'
            Token with_tok = lexer_peek(l);
            if (with_tok.type != TOK_IDENT || strncmp(with_tok.start, "with", 4) != 0 ||
                with_tok.len != 4)
            {
                zpanic_at(with_tok, "Expected 'with' after kernel call in launch statement");
            }
            lexer_next(l); // eat 'with'

            // Expect '{' for configuration block
            if (lexer_peek(l).type != TOK_LBRACE)
            {
                zpanic_at(lexer_peek(l), "Expected '{' after 'with' in launch statement");
            }
            lexer_next(l); // eat '{'

            ASTNode *grid = NULL;
            ASTNode *block = NULL;
            ASTNode *shared_mem = NULL;
            ASTNode *stream = NULL;

            // Parse configuration fields
            while (lexer_peek(l).type != TOK_RBRACE && lexer_peek(l).type != TOK_EOF)
            {
                Token field_name = lexer_next(l);
                if (field_name.type != TOK_IDENT)
                {
                    zpanic_at(field_name, "Expected field name in launch configuration");
                }

                // Expect ':'
                if (lexer_peek(l).type != TOK_COLON)
                {
                    zpanic_at(lexer_peek(l), "Expected ':' after field name");
                }
                lexer_next(l); // eat ':'

                // Parse value expression
                ASTNode *value = parse_expression(ctx, l);

                // Assign to appropriate field
                if (strncmp(field_name.start, "grid", 4) == 0 && field_name.len == 4)
                {
                    grid = value;
                }
                else if (strncmp(field_name.start, "block", 5) == 0 && field_name.len == 5)
                {
                    block = value;
                }
                else if (strncmp(field_name.start, "shared_mem", 10) == 0 && field_name.len == 10)
                {
                    shared_mem = value;
                }
                else if (strncmp(field_name.start, "stream", 6) == 0 && field_name.len == 6)
                {
                    stream = value;
                }
                else
                {
                    zpanic_at(field_name, "Unknown launch configuration field (expected: grid, "
                                          "block, shared_mem, stream)");
                }

                // Optional comma
                if (lexer_peek(l).type == TOK_COMMA)
                {
                    lexer_next(l);
                }
            }

            // Expect '}'
            if (lexer_peek(l).type != TOK_RBRACE)
            {
                zpanic_at(lexer_peek(l), "Expected '}' to close launch configuration");
            }
            lexer_next(l); // eat '}'

            // Expect ';'
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }

            // Require at least grid and block
            if (!grid || !block)
            {
                zpanic_at(launch_tok, "Launch configuration requires at least 'grid' and 'block'");
            }

            ASTNode *n = ast_create(NODE_CUDA_LAUNCH);
            n->cuda_launch.call = call;
            n->cuda_launch.grid = grid;
            n->cuda_launch.block = block;
            n->cuda_launch.shared_mem = shared_mem;
            n->cuda_launch.stream = stream;
            n->token = launch_tok;
            return n;
        }

        // Do-while logic was moved.

        if (strncmp(tk.start, "defer", 5) == 0 && tk.len == 5)
        {
            return parse_defer(ctx, l);
        }

        // Goto statement: goto label_name; OR goto *expr; (computed goto)
        if (strncmp(tk.start, "goto", 4) == 0 && tk.len == 4)
        {
            Token goto_tok = lexer_next(l); // eat 'goto'

            // Error if goto is used inside a defer block
            if (ctx->in_defer_block)
            {
                zpanic_at(goto_tok, "'goto' is not allowed inside a 'defer' block");
            }

            Token next = lexer_peek(l);

            // Computed goto: goto *ptr;
            if (next.type == TOK_OP && next.start[0] == '*')
            {
                lexer_next(l); // eat '*'
                ASTNode *target = parse_expression(ctx, l);
                if (lexer_peek(l).type == TOK_SEMICOLON)
                {
                    lexer_next(l);
                }

                ASTNode *n = ast_create(NODE_GOTO);
                n->goto_stmt.label_name = NULL;
                n->goto_stmt.goto_expr = target;
                n->token = goto_tok;
                return n;
            }

            // Regular goto
            Token label = lexer_next(l);
            if (label.type != TOK_IDENT)
            {
                zpanic_at(label, "Expected label name after goto");
            }
            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
            ASTNode *n = ast_create(NODE_GOTO);
            n->goto_stmt.label_name = token_strdup(label);
            n->token = goto_tok;
            zen_trigger_at(TRIGGER_GOTO, goto_tok);
            return n;
        }

        // Label detection: identifier followed by : (but not ::)
        {
            Lexer inner_lookahead = *l;
            Token ident = lexer_next(&inner_lookahead);
            Token maybe_colon = lexer_peek(&inner_lookahead);
            if (maybe_colon.type == TOK_COLON)
            {
                // Check it's not :: (double colon for namespaces)
                lexer_next(&inner_lookahead);
                Token after_colon = lexer_peek(&inner_lookahead);
                if (after_colon.type != TOK_COLON)
                {
                    // This is a label!
                    lexer_next(l); // eat identifier
                    lexer_next(l); // eat :

                    char *label_name = token_strdup(ident);
                    ASTNode *next = parse_statement(ctx, l);

                    if (next)
                    {
                        if (next->type == NODE_WHILE)
                        {
                            next->while_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->type == NODE_FOR)
                        {
                            next->for_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->type == NODE_FOR_RANGE)
                        {
                            next->for_range.loop_label = label_name;
                            return next;
                        }
                        else if (next->type == NODE_LOOP)
                        {
                            next->loop_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->type == NODE_REPEAT)
                        {
                            next->repeat_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->type == NODE_DO_WHILE)
                        {
                            next->do_while_stmt.loop_label = label_name;
                            return next;
                        }
                    }

                    ASTNode *n = ast_create(NODE_LABEL);
                    n->label_stmt.label_name = label_name;
                    n->token = ident;
                    n->next = next;
                    return n;
                }
            }
        }

        if ((strncmp(tk.start, "print", 5) == 0 && tk.len == 5) ||
            (strncmp(tk.start, "println", 7) == 0 && tk.len == 7) ||
            (strncmp(tk.start, "eprint", 6) == 0 && tk.len == 6) ||
            (strncmp(tk.start, "eprintln", 8) == 0 && tk.len == 8))
        {

            // Revert: User requested print without newline
            int is_ln = (tk.len == 7 || tk.len == 8);
            // int is_ln = (tk.len == 7 || tk.len == 8);
            int is_err = (tk.start[0] == 'e');
            char *target = is_err ? "stderr" : "stdout";

            lexer_next(l); // eat keyword

            Token t = lexer_next(l);
            if (t.type != TOK_STRING && t.type != TOK_FSTRING)
            {
                zpanic_at(t, "Expected string literal after print/eprint");
            }

            char *inner = xmalloc(t.len);
            if (t.type == TOK_FSTRING)
            {
                int is_multi =
                    (t.len >= 7 && t.start[1] == '"' && t.start[2] == '"' && t.start[3] == '"');
                int start_offset = is_multi ? 4 : 2;
                int end_offset = is_multi ? 3 : 1;
                strncpy(inner, t.start + start_offset, t.len - start_offset - end_offset);
                inner[t.len - start_offset - end_offset] = 0;
            }
            else
            {
                int is_multi =
                    (t.len >= 6 && t.start[0] == '"' && t.start[1] == '"' && t.start[2] == '"');
                int start_offset = is_multi ? 3 : 1;
                int end_offset = is_multi ? 3 : 1;
                strncpy(inner, t.start + start_offset, t.len - start_offset - end_offset);
                inner[t.len - start_offset - end_offset] = 0;
            }

            char **used_syms = NULL;
            int used_count = 0;
            char *code =
                process_printf_sugar(ctx, t, inner, is_ln, target, &used_syms, &used_count, 1);
            free(inner);

            if (lexer_peek(l).type == TOK_SEMICOLON)
            {
                lexer_next(l);
            }

            ASTNode *n = ast_create(NODE_RAW_STMT);
            n->token = t;
            // Append semicolon to Statement Expression to make it a valid statement
            char *stmt_code = xmalloc(strlen(code) + 2);
            sprintf(stmt_code, "%s;", code);
            free(code);
            n->raw_stmt.content = stmt_code;
            n->raw_stmt.used_symbols = used_syms;
            n->raw_stmt.used_symbol_count = used_count;
            return n;
        }
    }

    // Default: Expression Statement
    s = parse_expression(ctx, l);
    if (!s)
    {
        return NULL;
    }

    int has_semi = 0;
    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
        has_semi = 1;
    }

    // Auto-print in REPL: If no semicolon (implicit expr at block end)
    // and not an assignment, print it.
    if (ctx->is_repl && s && !has_semi)
    {
        int is_assign = 0;
        if (s->type == NODE_EXPR_BINARY)
        {
            char *op = s->binary.op;
            if (strcmp(op, "=") == 0 ||
                (strlen(op) > 1 && op[strlen(op) - 1] == '=' && strcmp(op, "==") != 0 &&
                 strcmp(op, "!=") != 0 && strcmp(op, "<=") != 0 && strcmp(op, ">=") != 0))
            {
                is_assign = 1;
            }
        }

        if (!is_assign)
        {
            ASTNode *print_node = ast_create(NODE_REPL_PRINT);
            print_node->repl_print.expr = s;
            // Preserve line info
            print_node->line = s->line;
            print_node->token = s->token;
            return print_node;
        }
    }

    if (s)
    {
        s->line = tk.line;
    }

    // Check for discarded required result
    if (s && s->type == NODE_EXPR_CALL)
    {
        ASTNode *callee = s->call.callee;
        if (callee && callee->type == NODE_EXPR_VAR)
        {
            FuncSig *sig = find_func(ctx, callee->var_ref.name);
            if (sig && sig->required)
            {
                zwarn_at(tk, "Ignoring return value of function marked @required");
                fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET
                                           "Use the result or explicitly discard with `_ = ...`\n");
            }
        }
    }

    return s;
}

ASTNode *parse_block(ParserContext *ctx, Lexer *l)
{
    expect(l, TOK_LBRACE, "Expected '{' to start a block");
    enter_scope(ctx);
    ASTNode *head = 0, *tail = 0;
    Token t = lexer_peek(l);
    int unreachable = 0;

    while (1)
    {
        skip_comments(l);
        Token tk = lexer_peek(l);
        if (tk.type == TOK_RBRACE)
        {
            lexer_next(l);
            break;
        }

        if (tk.type == TOK_EOF)
        {
            break;
        }

        if (tk.type == TOK_COMPTIME)
        {
            // lexer_next(l); // don't eat here, run_comptime_block expects it
            char *src = run_comptime_block(ctx, l);
            Lexer new_l;
            lexer_init(&new_l, src);
            // Parse statements from the generated source
            while (lexer_peek(&new_l).type != TOK_EOF)
            {
                ASTNode *s = parse_statement(ctx, &new_l);
                if (!s)
                {
                    break; // EOF or error handling dependency
                }

                // Link
                if (!head)
                {
                    head = s;
                }
                else
                {
                    tail->next = s;
                }
                tail = s;
                while (tail->next)
                {
                    tail = tail->next;
                }
            }
            continue;
        }

        ASTNode *s = parse_statement(ctx, l);
        if (s)
        {
            if (!head)
            {
                head = s;
            }
            else
            {
                tail->next = s;
            }
            tail = s;
            while (tail->next)
            {
                tail = tail->next; // Handle chains (e.g. var decl + defer)
            }

            // Check for control flow interruption
            if (s->type == NODE_RETURN || s->type == NODE_BREAK || s->type == NODE_CONTINUE)
            {
                if (unreachable == 0)
                {
                    unreachable = 1;
                }
            }
        }
    }

    // Check for unused variables in this block scope
    if (ctx->current_scope && !ctx->is_repl)
    {
        ZenSymbol *sym = ctx->current_scope->symbols;
        while (sym)
        {
            // Skip special names and already warned
            if (!sym->is_used && sym->name[0] != '_' && strcmp(sym->name, "it") != 0 &&
                strcmp(sym->name, "self") != 0)
            {
                // Skip autofree variables (used implicitly for cleanup)
                if (sym->is_autofree)
                {
                    sym = sym->next;
                    continue;
                }

                // RAII: Don't warn if type implements Drop (it is used implicitly)
                int has_drop = (sym->type_info && sym->type_info->traits.has_drop);
                if (!has_drop && sym->type_info && sym->type_info->name)
                {
                    ASTNode *def = find_struct_def(ctx, sym->type_info->name);
                    if (def && def->type_info && def->type_info->traits.has_drop)
                    {
                        has_drop = 1;
                    }
                }

                if (!has_drop)
                {
                    warn_unused_variable(sym->decl_token, sym->name);
                }
            }
            sym = sym->next;
        }
    }

    exit_scope(ctx);
    ASTNode *b = ast_create(NODE_BLOCK);
    b->token = t;
    b->block.statements = head;
    return b;
}

void try_parse_c_function_decl(ParserContext *ctx, const char *line)
{
    const char *p = line;
    while (*p && isspace(*p))
    {
        p++;
    }

    // Skip lines we don't want to parse as function declarations
    if (*p == '#' || *p == '/' || *p == '*' || *p == '\0')
    {
        return;
    }
    if (strncmp(p, "typedef", 7) == 0 && !isalnum(p[7]) && p[7] != '_')
    {
        return;
    }
    if (strncmp(p, "static", 6) == 0 && !isalnum(p[6]) && p[6] != '_')
    {
        return;
    }
    if (strncmp(p, "struct", 6) == 0 && !isalnum(p[6]) && p[6] != '_')
    {
        return;
    }
    if (strncmp(p, "union", 5) == 0 && !isalnum(p[5]) && p[5] != '_')
    {
        return;
    }
    if (strncmp(p, "enum", 4) == 0 && !isalnum(p[4]) && p[4] != '_')
    {
        return;
    }

    // Must contain '(' and end with ';' (prototype, not definition body)
    const char *lparen = strchr(p, '(');
    if (!lparen)
    {
        return;
    }

    // Check that the line ends with ';' (skip trailing whitespace)
    const char *end = p + strlen(p) - 1;
    while (end > p && isspace(*end))
    {
        end--;
    }
    if (*end != ';')
    {
        return; // Likely a function definition (has body) or multi-line
    }

    // Must not contain '{' — that would be a function body
    if (strchr(p, '{'))
    {
        return;
    }

    // Walk backwards from '(' to find the function name
    const char *name_end = lparen;
    while (name_end > p && isspace(*(name_end - 1)))
    {
        name_end--;
    }

    // name_end now points just past the last char of the function name
    const char *name_start = name_end;
    while (name_start > p && (isalnum(*(name_start - 1)) || *(name_start - 1) == '_'))
    {
        name_start--;
    }

    int name_len = (int)(name_end - name_start);
    if (name_len <= 0)
    {
        return;
    }

    // Reject names that are C keywords commonly seen in headers
    if ((name_len == 6 && strncmp(name_start, "return", 6) == 0) ||
        (name_len == 2 && strncmp(name_start, "if", 2) == 0) ||
        (name_len == 3 && strncmp(name_start, "for", 3) == 0) ||
        (name_len == 5 && strncmp(name_start, "while", 5) == 0))
    {
        return;
    }

    // There must be a return type before the name (at least one identifier/keyword)
    if (name_start == p)
    {
        return; // No return type
    }

    char *name = xmalloc(name_len + 1);
    strncpy(name, name_start, name_len);
    name[name_len] = '\0';

    register_extern_symbol(ctx, name);
    free(name);
}

/**
 * @brief Try to parse a C struct/union declaration from a header line.
 *
 * Detects patterns like:
 *   - typedef struct <tag> { ... (open brace on same line)
 *   - typedef struct <tag> <alias>;
 *   - struct <name> {
 *   - } <name>;  (closing typedef)
 *
 * Registers detected names as opaque type aliases so Zen C code can
 * reference them (e.g. as pointer types) without needing raw {} blocks.
 */
void try_parse_c_struct_decl(ParserContext *ctx, const char *line)
{
    const char *p = line;
    while (*p && isspace(*p))
    {
        p++;
    }

    if (*p == '#' || *p == '/' || *p == '*' || *p == '\0')
    {
        return;
    }

    int is_typedef = 0;
    int is_union = 0;

    // Check for typedef prefix
    if (strncmp(p, "typedef", 7) == 0 && !isalnum(p[7]) && p[7] != '_')
    {
        is_typedef = 1;
        p += 7;
        while (*p && isspace(*p))
        {
            p++;
        }
    }

    // Check for struct/union keyword
    if (strncmp(p, "struct", 6) == 0 && !isalnum(p[6]) && p[6] != '_')
    {
        p += 6;
    }
    else if (strncmp(p, "union", 5) == 0 && !isalnum(p[5]) && p[5] != '_')
    {
        p += 5;
        is_union = 1;
    }
    else if (is_typedef)
    {
        return; // typedef of something else (e.g. typedef int foo_t;)
    }
    else
    {
        // Check for closing typedef: } Name;
        if (*p == '}')
        {
            p++;
            while (*p && isspace(*p))
            {
                p++;
            }
            // Extract name before ';'
            const char *name_start = p;
            while (*p && (isalnum(*p) || *p == '_'))
            {
                p++;
            }
            int name_len = (int)(p - name_start);
            while (*p && isspace(*p))
            {
                p++;
            }
            if (name_len > 0 && *p == ';')
            {
                char *name = xmalloc(name_len + 1);
                strncpy(name, name_start, name_len);
                name[name_len] = '\0';
                register_type_alias(ctx, name, name, NULL, 1, NULL);
                register_extern_symbol(ctx, name);
                free(name);
            }
        }
        return;
    }

    // Skip whitespace after struct/union keyword
    while (*p && isspace(*p))
    {
        p++;
    }

    // Extract tag name (the name right after struct/union)
    const char *tag_start = p;
    while (*p && (isalnum(*p) || *p == '_'))
    {
        p++;
    }
    int tag_len = (int)(p - tag_start);

    if (tag_len <= 0)
    {
        return; // Anonymous struct/union
    }

    // Register the tag name as an opaque type
    char *tag_name = xmalloc(tag_len + 1);
    strncpy(tag_name, tag_start, tag_len);
    tag_name[tag_len] = '\0';

    // Skip whitespace
    while (*p && isspace(*p))
    {
        p++;
    }

    // Only register if this looks like a real declaration (has '{' or ';')
    if (*p == '{' || *p == ';')
    {
        const char *c_keyword = is_union ? "union" : "struct";
        char *c_type = xmalloc(strlen(c_keyword) + 1 + tag_len + 1);
        sprintf(c_type, "%s %s", c_keyword, tag_name);
        register_type_alias(ctx, tag_name, c_type, NULL, 1, NULL);
        register_extern_symbol(ctx, tag_name);
        free(c_type);
    }

    // If typedef: also check for alias after '}'
    // (handled by the '}' branch on subsequent lines)

    free(tag_name);
}

/**
 * @brief Recursively scan a C header file for declarations.
 *
 * Scans the given header file for:
 *   - #define macros (via try_parse_macro_const)
 *   - Function prototypes (via try_parse_c_function_decl)
 *   - Struct/union declarations (via try_parse_c_struct_decl)
 *   - Nested #include "..." directives (recursively scanned)
 *
 * System includes (#include <...>) are skipped.
 * Already-scanned files are tracked to prevent infinite cycles.
 *
 * @param ctx     Parser context
 * @param path    Path to the header file
 * @param depth   Current recursion depth (capped at 16)
 */
void scan_c_header_contents(ParserContext *ctx, const char *path, int depth)
{
    // Safety: cap recursion depth
    if (depth > 16)
    {
        return;
    }

    // Prevent re-scanning the same header (handles include guards / cycles)
    if (is_file_imported(ctx, path))
    {
        return;
    }
    mark_file_imported(ctx, path);

    char *src = load_file(path);
    if (!src)
    {
        return;
    }

    // Compute directory of the current header for resolving relative includes
    char header_dir[1024];
    header_dir[0] = 0;
    const char *last_slash = z_path_last_sep(path);
    if (last_slash)
    {
        int dir_len = (int)(last_slash - path);
        if (dir_len >= (int)sizeof(header_dir))
        {
            dir_len = (int)sizeof(header_dir) - 1;
        }
        strncpy(header_dir, path, dir_len);
        header_dir[dir_len] = 0;
    }

    char *ptr = src;
    while (*ptr)
    {
        char *line_start = ptr;
        char *line_end = ptr;
        while (*line_end)
        {
            if (*line_end == '\n')
            {
                // Check for line continuation (simplistic)
                if (line_end > line_start && *(line_end - 1) == '\\')
                {
                    line_end++;
                    continue;
                }
                break;
            }
            line_end++;
        }

        int len = line_end - line_start;
        if (len > 0)
        {
            char *line_buf = xmalloc(len + 1);
            strncpy(line_buf, line_start, len);
            line_buf[len] = 0;

            char *p = line_buf;
            while (*p && isspace(*p))
            {
                p++;
            }
            if (*p == '#')
            {
                try_parse_macro_const(ctx, line_buf);

                // Check for nested #include "..." directives
                const char *inc = p + 1;
                while (*inc && isspace(*inc))
                {
                    inc++;
                }
                if (strncmp(inc, "include", 7) == 0 && !isalnum(inc[7]) && inc[7] != '_')
                {
                    inc += 7;
                    while (*inc && isspace(*inc))
                    {
                        inc++;
                    }
                    if (*inc == '"')
                    {
                        inc++; // skip opening quote
                        const char *end_quote = strchr(inc, '"');
                        if (end_quote && end_quote > inc)
                        {
                            int inc_len = (int)(end_quote - inc);
                            if (inc_len > 255)
                            {
                                inc_len = 255; // Sanity limit for include paths
                            }
                            char inc_name[256];
                            memcpy(inc_name, inc, inc_len);
                            inc_name[inc_len] = '\0';

                            char nested_path[1280];
                            if (header_dir[0])
                            {
                                snprintf(nested_path, sizeof(nested_path), "%s/%s", header_dir,
                                         inc_name);
                            }
                            else
                            {
                                snprintf(nested_path, sizeof(nested_path), "%s", inc_name);
                            }

                            // Recursively scan the nested header
                            scan_c_header_contents(ctx, nested_path, depth + 1);
                        }
                    }
                }
            }
            else
            {
                try_parse_c_function_decl(ctx, line_buf);
                try_parse_c_struct_decl(ctx, line_buf);
            }
            free(line_buf);
        }

        ptr = line_end;
        if (*ptr == '\n')
        {
            ptr++;
        }
    }
    free(src);
}

ASTNode *parse_include(ParserContext *ctx, Lexer *l)
{
    lexer_next(l); // eat 'include'
    Token t = lexer_next(l);
    char *path = NULL;
    int is_system = 0;

    if (t.type == TOK_LANGLE)
    {
        // System include: include <raylib.h>
        is_system = 1;
        char buf[256];
        buf[0] = 0;
        while (1)
        {
            Token i = lexer_next(l);
            if (i.type == TOK_RANGLE)
            {
                break;
            }
            strncat(buf, i.start, i.len);
        }
        path = xstrdup(buf);

        // Mark that this file has external includes (suppress undefined warnings)
        ctx->has_external_includes = 1;
    }
    else
    {
        // Local include: include "file.h"
        is_system = 0;
        int len = t.len - 2;
        path = xmalloc(len + 1);
        strncpy(path, t.start + 1, len);
        path[len] = 0;
    }

    ASTNode *n = ast_create(NODE_INCLUDE);
    n->include.path = path;
    n->include.is_system = is_system;

    if (!is_system && path)
    {
        scan_c_header_contents(ctx, path, 0);
    }

    return n;
}
ASTNode *parse_import(ParserContext *ctx, Lexer *l)
{
    lexer_next(l); // eat 'import'

    // Check for 'plugin' keyword
    Token next = lexer_peek(l);
    if (next.type == TOK_IDENT && next.len == 6 && strncmp(next.start, "plugin", 6) == 0)
    {
        lexer_next(l); // consume "plugin"

        // Expect string literal with plugin name
        Token plugin_tok = lexer_next(l);
        if (plugin_tok.type != TOK_STRING)
        {
            zpanic_at(plugin_tok, "Expected string literal after 'import plugin'");
        }

        // Extract plugin name (strip quotes)
        int name_len = plugin_tok.len - 2;
        char *plugin_name = xmalloc(name_len + 1);
        strncpy(plugin_name, plugin_tok.start + 1, name_len);
        plugin_name[name_len] = '\0';

        if (plugin_name[0] == '.' &&
            (plugin_name[1] == '/' || (plugin_name[1] == '.' && plugin_name[2] == '/')))
        {
            char *current_dir = xstrdup(g_current_filename);
            char *last_slash = z_path_last_sep(current_dir);
            if (last_slash)
            {
                *last_slash = 0;
                char resolved_path[1024];
                snprintf(resolved_path, sizeof(resolved_path), "%s/%s", current_dir, plugin_name);
                free(plugin_name);
                plugin_name = xstrdup(resolved_path);
            }
            free(current_dir);
        }

        // Check for optional "as alias"
        char *alias = NULL;
        Token as_tok = lexer_peek(l);
        if (as_tok.type == TOK_IDENT && as_tok.len == 2 && strncmp(as_tok.start, "as", 2) == 0)
        {
            lexer_next(l); // consume "as"
            Token alias_tok = lexer_next(l);
            if (alias_tok.type != TOK_IDENT)
            {
                zpanic_at(alias_tok, "Expected identifier after 'as'");
            }
            alias = token_strdup(alias_tok);
        }

        // Register the plugin
        register_plugin(ctx, plugin_name, alias);

        // Consume optional semicolon
        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        // Return NULL - no AST node needed for imports
        return NULL;
    }

    // Regular module import handling follows...
    // Check if this is selective import: import { ... } from "file"
    int is_selective = 0;
    char *symbols[32]; // Max 32 selective imports
    char *aliases[32];
    int symbol_count = 0;

    if (lexer_peek(l).type == TOK_LBRACE)
    {
        is_selective = 1;
        lexer_next(l); // eat {

        // Parse symbol list
        while (lexer_peek(l).type != TOK_RBRACE)
        {
            if (symbol_count > 0 && lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l); // eat comma
            }

            Token sym_tok = lexer_next(l);
            if (sym_tok.type != TOK_IDENT)
            {
                zpanic_at(sym_tok, "Expected identifier in selective import");
            }

            symbols[symbol_count] = xmalloc(sym_tok.len + 1);
            strncpy(symbols[symbol_count], sym_tok.start, sym_tok.len);
            symbols[symbol_count][sym_tok.len] = 0;

            // Check for 'as alias'
            Token inner_next = lexer_peek(l);
            if (inner_next.type == TOK_IDENT && inner_next.len == 2 &&
                strncmp(inner_next.start, "as", 2) == 0)
            {
                lexer_next(l); // eat 'as'
                Token alias_tok = lexer_next(l);
                if (alias_tok.type != TOK_IDENT)
                {
                    zpanic_at(alias_tok, "Expected identifier after 'as'");
                }

                aliases[symbol_count] = xmalloc(alias_tok.len + 1);
                strncpy(aliases[symbol_count], alias_tok.start, alias_tok.len);
                aliases[symbol_count][alias_tok.len] = 0;
            }
            else
            {
                aliases[symbol_count] = NULL; // No alias
            }

            symbol_count++;
        }

        lexer_next(l); // eat }

        // Expect 'from'
        Token from_tok = lexer_next(l);
        if (from_tok.type != TOK_IDENT || from_tok.len != 4 ||
            strncmp(from_tok.start, "from", 4) != 0)
        {
            zpanic_at(from_tok, "Expected 'from' after selective import list, got type=%d",
                      from_tok.type);
        }
    }

    // Parse filename
    Token t = lexer_next(l);
    if (t.type != TOK_STRING)
    {
        zpanic_at(t,
                  "Expected string (filename) after 'from' in selective import, got "
                  "type %d",
                  t.type);
    }
    int ln = t.len - 2; // Remove quotes
    char *fn = xmalloc(ln + 1);
    strncpy(fn, t.start + 1, ln);
    fn[ln] = 0;

    // Resolve paths
    char *resolved = z_resolve_path(fn, g_current_filename);
    if (!resolved)
    {
        // Fallback for C headers: allow them to be "not found" locally (they might be system
        // headers)
        if (strlen(fn) > 2 && strcmp(fn + strlen(fn) - 2, ".h") == 0)
        {
            resolved = xstrdup(fn);
        }
        else
        {
            zpanic_at(t, "Could not find module: %s", fn);
        }
    }
    free(fn);
    fn = resolved;

    if (is_file_imported(ctx, fn))
    {
        free(fn);
        return NULL;
    }
    mark_file_imported(ctx, fn);

    // For selective imports, register them BEFORE parsing the file
    char *module_base_name = NULL;
    if (is_selective)
    {
        module_base_name = extract_module_name(fn);
        for (int i = 0; i < symbol_count; i++)
        {
            register_selective_import(ctx, symbols[i], aliases[i], module_base_name);
        }
    }

    // Check for 'as alias' syntax (for namespaced imports)
    char *alias = NULL;
    if (!is_selective)
    {
        Token next_tok = lexer_peek(l);
        if (next_tok.type == TOK_IDENT && next_tok.len == 2 &&
            strncmp(next_tok.start, "as", 2) == 0)
        {
            lexer_next(l); // eat 'as'
            Token alias_tok = lexer_next(l);
            if (alias_tok.type != TOK_IDENT)
            {
                zpanic_at(alias_tok, "Expected identifier after 'as'");
            }

            alias = xmalloc(alias_tok.len + 1);
            strncpy(alias, alias_tok.start, alias_tok.len);
            alias[alias_tok.len] = 0;

            // Register the module

            // Check if C header
            int is_header = 0;
            if (strlen(fn) > 2 && strcmp(fn + strlen(fn) - 2, ".h") == 0)
            {
                is_header = 1;
            }

            // Register the module
            Module *m = xmalloc(sizeof(Module));
            m->alias = xstrdup(alias);
            m->path = xstrdup(fn);
            m->base_name = extract_module_name(fn);
            m->is_c_header = is_header;
            m->next = ctx->modules;
            ctx->modules = m;
        }
    }

    // C Header: Emit include and return (don't parse)
    if (strlen(fn) > 2 && strcmp(fn + strlen(fn) - 2, ".h") == 0)
    {
        ASTNode *n = ast_create(NODE_INCLUDE);
        n->include.path = xstrdup(fn); // Store exact path
        n->include.is_system = 0;      // Double quotes
        return n;
    }

    // Load and parse the file
    char *src = load_file(fn);
    if (!src)
    {
        if (!src)
        {
            if (g_config.mode_lsp)
            {
                // In LSP mode, just warn and return error node or similar
                // For now, let's return a dummy ERROR node or NULL to avoid crashing
                zwarn_at(t, "LSP: Import not found: %s", fn);
                ASTNode *dummy = ast_create(NODE_BLOCK);
                dummy->block.statements = NULL;
                return dummy;
            }
            zpanic_at(t, "Not found: %s", fn);
        }
    }

    Lexer i;
    lexer_init(&i, src);

    // If this is a namespaced import or selective import, set the module prefix
    char *prev_module_prefix = ctx->current_module_prefix;
    char *temp_module_prefix = NULL;

    if (alias)
    { // For 'import "file" as alias'
        temp_module_prefix = extract_module_name(fn);
        ctx->current_module_prefix = temp_module_prefix;
    }
    else if (is_selective)
    { // For 'import {sym} from "file"'
        temp_module_prefix = extract_module_name(fn);
        ctx->current_module_prefix = temp_module_prefix;
    }

    // Update global filename context for relative imports inside the new file
    const char *saved_fn = g_current_filename;
    g_current_filename = fn;

    ASTNode *r = parse_program_nodes(ctx, &i);

    // Restore filename context
    g_current_filename = (char *)saved_fn;

    // Restore previous module context
    if (temp_module_prefix)
    {
        free(temp_module_prefix);
        ctx->current_module_prefix = prev_module_prefix;
    }

    // Free selective import symbols and aliases
    if (is_selective)
    {
        for (int k = 0; k < symbol_count; k++)
        {
            free(symbols[k]);
            if (aliases[k])
            {
                free(aliases[k]);
            }
        }
    }

    if (alias)
    {
        free(alias);
    }

    if (module_base_name)
    { // This was only used for selective import
      // registration, not for ctx->current_module_prefix
        free(module_base_name);
    }

    free(fn);
    return r;
}

// Helper: Execute comptime block and return generated source
char *run_comptime_block(ParserContext *ctx, Lexer *l)
{
    (void)ctx;
    expect(l, TOK_COMPTIME, "comptime");
    expect(l, TOK_LBRACE, "expected { after comptime");

    const char *start = l->src + l->pos;
    int depth = 1;
    while (depth > 0)
    {
        Token t = lexer_next(l);
        if (t.type == TOK_EOF)
        {
            zpanic_at(t, "Unexpected EOF in comptime block");
        }
        if (t.type == TOK_LBRACE)
        {
            depth++;
        }
        if (t.type == TOK_RBRACE)
        {
            depth--;
        }
    }
    // End is passed the closing brace, so pos points after it.
    // The code block is between start and (current pos - 1)
    int len = (l->src + l->pos - 1) - start;
    char *code = xmalloc(len + 1);
    strncpy(code, start, len);
    code[len] = 0;

    // Wrap in block to parse mixed statements/declarations
    int wrapped_len = len + 4; // "{ " + code + " }"
    char *wrapped_code = xmalloc(wrapped_len + 1);
    sprintf(wrapped_code, "{ %s }", code);

    Lexer cl;
    lexer_init(&cl, wrapped_code);
    ParserContext cctx;
    memset(&cctx, 0, sizeof(cctx));
    enter_scope(&cctx); // Global scope
    register_builtins(&cctx);
    register_comptime_builtins(&cctx);
    cctx.has_external_includes = 1; // Suppress undefined warnings for comptime helpers

    ASTNode *block = parse_block(&cctx, &cl);
    ASTNode *nodes = block ? block->block.statements : NULL;

    free(wrapped_code);

    char filename[64];
    sprintf(filename, "_tmp_comptime_%d.c", rand());
    FILE *f = fopen(filename, "w");
    if (!f)
    {
        zpanic_at(lexer_peek(l), "Could not create temp file %s", filename);
    }

    emit_preamble(ctx, f);
    fprintf(
        f,
        "size_t _z_check_bounds(size_t index, size_t size) { if (index >= size) { fprintf(stderr, "
        "\"Index out of bounds: %%zu >= %%zu\\n\", index, size); exit(1); } return index; }\n");

    // Comptime helper functions
    fprintf(f, "void yield(const char* s) { printf(\"%%s\", s); }\n");
    fprintf(f, "void code(const char* s) { printf(\"%%s\", s); }\n"); // Alias for yield
    fprintf(f, "void compile_error(const char* s) { "
               "fprintf(stderr, \"Compile-time error: %%s\\n\", s); exit(1); }\n");
    fprintf(f, "void compile_warn(const char* s) { "
               "fprintf(stderr, \"Compile-time warning: %%s\\n\", s); }\n");

    // Build metadata constants
    fprintf(f, "#define __COMPTIME_TARGET__ \"%s\"\n", z_get_system_name());
    fprintf(f, "#define __COMPTIME_FILE__ \"%s\"\n", g_current_filename);

    ASTNode *curr = nodes;
    ASTNode *stmts = NULL;
    ASTNode *stmts_tail = NULL;

    while (curr)
    {
        ASTNode *next = curr->next;
        curr->next = NULL;

        if (curr->type == NODE_INCLUDE)
        {
            emit_includes_and_aliases(curr, f);
        }
        else if (curr->type == NODE_STRUCT)
        {
            emit_struct_defs(&cctx, curr, f);
        }
        else if (curr->type == NODE_ENUM)
        {
            emit_enum_protos(curr, f);
        }
        else if (curr->type == NODE_CONST)
        {
            emit_globals(&cctx, curr, f);
        }
        else if (curr->type == NODE_FUNCTION)
        {
            codegen_node_single(&cctx, curr, f);
        }
        else if (curr->type == NODE_IMPL)
        {
            // Impl support pending
        }
        else
        {
            // Statement or expression -> main
            if (!stmts)
            {
                stmts = curr;
            }
            else
            {
                stmts_tail->next = curr;
            }
            stmts_tail = curr;
        }
        curr = next;
    }

    {
        StructRef *ref = ctx->parsed_funcs_list;
        while (ref)
        {
            ASTNode *fn = ref->node;
            if (fn && fn->type == NODE_FUNCTION && fn->func.is_comptime)
            {
                emit_func_signature(ctx, f, fn, NULL);
                fprintf(f, ";\n");
                codegen_node_single(ctx, fn, f);
            }
            ref = ref->next;
        }
    }

    fprintf(f, "int main() {\n");
    curr = stmts;
    while (curr)
    {
        if (curr->type >= NODE_EXPR_BINARY && curr->type <= NODE_EXPR_SLICE)
        {
            codegen_expression(&cctx, curr, f);
            fprintf(f, ";\n");
        }
        else
        {
            codegen_node_single(&cctx, curr, f);
        }
        curr = curr->next;
    }
    fprintf(f, "return 0;\n}\n");
    fclose(f);

    char cmdbuf[4096];
    char bin[1024];

    sprintf(bin, "%s%s", filename, z_get_exe_ext());

    // Use quotes for paths to prevent injection/errors with spaces
#if ZC_OS_WINDOWS
    // On Windows, system() uses cmd.exe /c. If the command starts with a quote and has multiple
    // quotes, cmd.exe strips the first and last quote. Wrapping the whole thing in another pair of
    // quotes fixes this.
    snprintf(cmdbuf, sizeof(cmdbuf),
             "\"%s \"%s\" -o \"%s\" -Istd -Istd/third-party/tre/include %s\"", g_config.cc,
             filename, bin, z_get_comptime_link_flags());
#else
    snprintf(cmdbuf, sizeof(cmdbuf), "%s \"%s\" -o \"%s\" -Istd -Istd/third-party/tre/include %s",
             g_config.cc, filename, bin, z_get_comptime_link_flags());
#endif

    if (!g_config.verbose)
    {
        strcat(cmdbuf, z_get_null_redirect());
    }

    int res = system(cmdbuf);
    if (res != 0)
    {
        zpanic_at(lexer_peek(l), "Comptime compilation failed for:\n%s", code);
    }

    char out_file[1024];
    sprintf(out_file, "%s.out", filename);

    // Execution command
#if ZC_OS_WINDOWS
    snprintf(cmdbuf, sizeof(cmdbuf), "\"%s\"%s\" > \"%s\"\"", z_get_run_prefix(), bin, out_file);
#else
    snprintf(cmdbuf, sizeof(cmdbuf), "%s\"%s\" > \"%s\"", z_get_run_prefix(), bin, out_file);
#endif

    if (system(cmdbuf) != 0)
    {
        zpanic_at(lexer_peek(l), "Comptime execution failed");
    }

    char *output_src = load_file(out_file);
    if (!output_src)
    {
        output_src = xstrdup(""); // Empty output is valid
    }

    remove(filename);
    remove(bin);
    remove(out_file);
    free(code);

    return output_src;
}

ASTNode *parse_comptime(ParserContext *ctx, Lexer *l)
{
    char *output_src = run_comptime_block(ctx, l);

    Lexer new_l;
    lexer_init(&new_l, output_src);
    return parse_program_nodes(ctx, &new_l);
}

// Parse plugin block: plugin name ... end
ASTNode *parse_plugin(ParserContext *ctx, Lexer *l)
{
    (void)ctx;

    // Expect 'plugin' keyword (already consumed by caller)
    // Next should be plugin name
    Token tk = lexer_next(l);
    if (tk.type != TOK_IDENT)
    {
        zpanic_at(tk, "Expected plugin name after 'plugin' keyword");
    }

    // Extract plugin name
    char *plugin_name = xmalloc(tk.len + 1);
    strncpy(plugin_name, tk.start, tk.len);
    plugin_name[tk.len] = '\0';

    // Collect everything until 'end'
    char *body = xmalloc(8192);
    body[0] = '\0';
    int body_len = 0;

    while (1)
    {
        Token t = lexer_peek(l);
        if (t.type == TOK_EOF)
        {
            zpanic_at(t, "Unexpected EOF in plugin block, expected 'end'");
        }

        // Check for 'end'
        if (t.type == TOK_IDENT && t.len == 3 && strncmp(t.start, "end", 3) == 0)
        {
            lexer_next(l); // consume 'end'
            break;
        }

        // Append token to body
        if (body_len + t.len + 2 < 8192)
        {
            strncat(body, t.start, t.len);
            body[body_len + t.len] = ' ';
            body[body_len + t.len + 1] = '\0';
            body_len += t.len + 1;
        }

        lexer_next(l);
    }

    // Create plugin node
    ASTNode *n = ast_create(NODE_PLUGIN);
    n->plugin_stmt.plugin_name = plugin_name;
    n->plugin_stmt.body = body;

    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
    }
    return n;
}

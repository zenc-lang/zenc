// SPDX-License-Identifier: MIT

#include "parser.h"
#include "../constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../ast/ast.h"
#include "../utils/format_expr.h"
#include "../plugins/plugin_manager.h"
#include "../zen/zen_facts.h"
#include "zprep_plugin.h"
#include "analysis/move_check.h"

ASTNode *parse_statement(ParserContext *ctx, Lexer *l)
{
    int prev_emit = l->emit_comments;
    if (ctx->config->keep_comments)
    {
        l->emit_comments = 1;
    }
    Token tk = lexer_peek(l);
    l->emit_comments = prev_emit;

    if (tk.kind == TOK_COMMENT)
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

    if (tk.kind == TOK_SEMICOLON)
    {
        lexer_next(l);
        ASTNode *nop = ast_create(NODE_BLOCK); // Empty block as NOP
        nop->block.statements = NULL;
        return nop;
    }

    if (tk.kind == TOK_PREPROC)
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

    if (tk.kind == TOK_STRING || tk.kind == TOK_FSTRING || tk.kind == TOK_RAW_STRING)
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token next = lexer_peek(&lookahead);
        ZenTokenType next_type = next.kind;

        if (next_type == TOK_SEMICOLON || next_type == TOK_DOTDOT || next_type == TOK_RBRACE)
        {
            Token t = lexer_next(l); // consume string
            char *inner = token_get_string_content(t);

            int is_ln = (next_type == TOK_SEMICOLON || next_type == TOK_RBRACE);
            char **used_syms = NULL;
            int used_count = 0;
            char *code = process_printf_sugar(ctx, next, inner, is_ln, "stdout", &used_syms,
                                              &used_count, 1, (t.kind == TOK_RAW_STRING), 0);
            if (!code)
            {
                return NULL;
            }

            if (next_type == TOK_SEMICOLON)
            {
                lexer_next(l); // consume ;
            }
            else if (next_type == TOK_DOTDOT)
            {
                lexer_next(l); // consume ..
                if (lexer_peek(l).kind == TOK_SEMICOLON)
                {
                    lexer_next(l); // consume optional ;
                }
            }
            // If TOK_RBRACE, do not consume it, so parse_block can see it and terminate loop.

            ASTNode *n = ast_create(NODE_RAW_STMT);
            n->token = tk;
            // Append semicolon to Statement Expression to make it a valid statement
            char *stmt_code = xmalloc(strlen(code) + 2);
            sprintf(stmt_code, "%s;", code); /* safe */
            zfree(code);
            n->raw_stmt.content = stmt_code;
            n->raw_stmt.used_symbols = used_syms;
            n->raw_stmt.used_symbol_count = used_count;
            zfree(inner);
            return n;
        }
    }

    // Block
    if (tk.kind == TOK_LBRACE)
    {
        return parse_block(ctx, l);
    }

    // Keywords / Special
    if (tk.kind == TOK_DO)
    {
        Token do_tok = lexer_next(l); // eat 'do'
        ctx->cg.loop_depth++;
        ASTNode *body = parse_block(ctx, l);
        ctx->cg.loop_depth--;

        // Expect 'while'
        Token while_tok = lexer_peek(l);
        if (while_tok.kind != TOK_IDENT || strncmp(while_tok.start, "while", 5) != 0 ||
            while_tok.len != 5)
        {
            zpanic_at(while_tok, "Expected 'while' after do block");
        }
        lexer_next(l); // eat 'while'

        ASTNode *cond = parse_expression(ctx, l);
        if (lexer_peek(l).kind == TOK_SEMICOLON)
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
    if (tk.kind == TOK_TRAIT)
    {
        return parse_trait(ctx, l);
    }
    if (tk.kind == TOK_IMPL)
    {
        return parse_impl(ctx, l);
    }
    if (tk.kind == TOK_AUTOFREE)
    {
        lexer_next(l);
        if (lexer_peek(l).kind != TOK_IDENT || strncmp(lexer_peek(l).start, "let", 3) != 0)
        {
            zpanic_at(lexer_peek(l), "Expected 'let' after autofree");
        }
        s = parse_var_decl(ctx, l, 0);
        if (!s)
        {
            return NULL;
        }
        s->var_decl.is_autofree = 1;
        // Mark symbol as autofree to suppress unused variable warning
        ZenSymbol *sym = find_symbol_entry(ctx, s->var_decl.name);
        if (sym)
        {
            sym->is_autofree = 1;
        }
        return s;
    }
    if (tk.kind == TOK_TEST)
    {
        return parse_test(ctx, l);
    }
    if (tk.kind == TOK_COMPTIME)
    {
        ASTNode *body = parse_comptime_body(ctx, l);
        ASTNode *ct = ast_create(NODE_COMPTIME);
        ct->comptime.body = body;
        ct->comptime.generated = NULL;
        return ct;
    }
    if (tk.kind == TOK_EXPECT)
    {
        return parse_expect(ctx, l);
    }
    if (tk.kind == TOK_ASSERT)
    {
        return parse_assert(ctx, l);
    }
    if (tk.kind == TOK_DEFER)
    {
        return parse_defer(ctx, l);
    }
    if (tk.kind == TOK_ASM)
    {
        return parse_asm(ctx, l);
    }
    if (tk.kind == TOK_DEF)
    {
        return parse_def(ctx, l, 0);
    }

    // Thread-local variable: @thread_local static let x = 0;
    if (tk.kind == TOK_AT)
    {
        lexer_next(l);
        Token id = lexer_peek(l);
        if (id.kind == TOK_IDENT && id.len == 12 && strncmp(id.start, "thread_local", 12) == 0)
        {
            lexer_next(l);
            Token next = lexer_peek(l);
            if (next.kind == TOK_IDENT && next.len == 6 && strncmp(next.start, "static", 6) == 0)
            {
                lexer_next(l);
                next = lexer_peek(l);
                if (next.kind == TOK_IDENT && next.len == 3 && strncmp(next.start, "let", 3) == 0)
                {
                    ASTNode *v = parse_var_decl(ctx, l, 0);
                    v->var_decl.is_static = 1;
                    v->var_decl.is_thread_local = 1;
                    return v;
                }
                zpanic_at(next, "Expected 'let' after 'static'");
            }
            zpanic_at(next, "Expected 'static' after '@thread_local'");
        }
    }

    // Identifiers (Keywords or Expressions)
    if (tk.kind == TOK_IDENT)
    {
        // Check for macro invocation: identifier! { code }
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token exclaim = lexer_peek(&lookahead);
        lexer_next(&lookahead);
        Token lbrace = lexer_peek(&lookahead);
        if (exclaim.kind == TOK_OP && exclaim.len == 1 && exclaim.start[0] == '!' &&
            lbrace.kind == TOK_LBRACE)
        {
            // This is a macro invocation
            char *macro_name = token_strdup(tk);
            lexer_next(l); // consume identifier

            ASTNode *n = parse_macro_call(ctx, l, macro_name);
            zfree(macro_name);
            return n;
        }

        // Check for raw blocks
        if (strncmp(tk.start, "raw", 3) == 0 && tk.len == 3)
        {
            lexer_next(l); // eat raw
            if (lexer_peek(l).kind != TOK_LBRACE)
            {
                zpanic_at(lexer_peek(l), "Expected { after raw");
            }
            lexer_next(l);

            const char *start = l->src + l->pos;
            int depth = 1;
            while (depth > 0)
            {
                Token t = lexer_next(l);
                if (t.kind == TOK_EOF)
                {
                    zpanic_at(t, "Unexpected EOF in raw block");
                    return NULL;
                }
                if (t.kind == TOK_LBRACE)
                {
                    depth++;
                }
                if (t.kind == TOK_RBRACE)
                {
                    depth--;
                }
            }
            const char *end = l->src + l->pos - 1;
            size_t len = (size_t)(end - start);

            char *content = xmalloc(len + 1);
            memcpy(content, start, (size_t)(len));
            content[len] = 0;

            ASTNode *raw_s = ast_create(NODE_RAW_STMT);
            raw_s->token = tk;
            raw_s->raw_stmt.content = normalize_raw_content(content);
            zfree(content);
            return raw_s;
        }

        // Check for plugin blocks
        if (strncmp(tk.start, "plugin", 6) == 0 && tk.len == 6)
        {
            lexer_next(l); // consume 'plugin'
            return parse_plugin(ctx, l, tk);
        }

        if (strncmp(tk.start, "let", 3) == 0 && tk.len == 3)
        {
            return parse_var_decl(ctx, l, 0);
        }

        // Static local variable: static let x = 0;
        if (strncmp(tk.start, "static", 6) == 0 && tk.len == 6)
        {
            lexer_next(l); // eat 'static'
            Token next = lexer_peek(l);
            if (strncmp(next.start, "let", 3) == 0 && next.len == 3)
            {
                ASTNode *v = parse_var_decl(ctx, l, 0);
                v->var_decl.is_static = 1;
                return v;
            }
            zpanic_at(next, "Expected 'let' after 'static'");
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
            if (ctx->cg.in_defer_block)
            {
                zpanic_at(break_token, "'break' is not allowed inside a 'defer' block");
            }

            // Error if break is not inside any loop
            if (ctx->cg.loop_depth == 0)
            {
                zpanic_at(break_token, "'break' outside a loop");
            }

            ASTNode *n = ast_create(NODE_BREAK);
            n->token = break_token;
            n->break_stmt.target_label = NULL;
            // Check for 'label or label
            if (lexer_peek(l).kind == TOK_CHAR || lexer_peek(l).kind == TOK_IDENT)
            {
                Token label_tok = lexer_next(l);
                if (label_tok.kind == TOK_CHAR)
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
            if (lexer_peek(l).kind == TOK_SEMICOLON)
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
            if (ctx->cg.in_defer_block)
            {
                zpanic_at(continue_token, "'continue' is not allowed inside a 'defer' block");
            }

            if (ctx->cg.loop_depth == 0)
            {
                zpanic_at(continue_token, "'continue' outside a loop");
            }

            ASTNode *n = ast_create(NODE_CONTINUE);
            n->token = continue_token;
            n->continue_stmt.target_label = NULL;
            if (lexer_peek(l).kind == TOK_CHAR || lexer_peek(l).kind == TOK_IDENT)
            {
                Token label_tok = lexer_next(l);
                if (label_tok.kind == TOK_CHAR)
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
            if (lexer_peek(l).kind == TOK_SEMICOLON)
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
            if (!call || call->kind != NODE_EXPR_CALL)
            {
                zpanic_at(launch_tok, "Expected kernel call after 'launch'");
            }

            // Expect 'with'
            Token with_tok = lexer_peek(l);
            if (with_tok.kind != TOK_IDENT || strncmp(with_tok.start, "with", 4) != 0 ||
                with_tok.len != 4)
            {
                zpanic_at(with_tok, "Expected 'with' after kernel call in launch statement");
            }
            lexer_next(l); // eat 'with'

            // Expect '{' for configuration block
            if (lexer_peek(l).kind != TOK_LBRACE)
            {
                zpanic_at(lexer_peek(l), "Expected '{' after 'with' in launch statement");
            }
            lexer_next(l); // eat '{'

            ASTNode *grid = NULL;
            ASTNode *block = NULL;
            ASTNode *shared_mem = NULL;
            ASTNode *stream = NULL;

            // Parse configuration fields
            while (lexer_peek(l).kind != TOK_RBRACE && lexer_peek(l).kind != TOK_EOF)
            {
                Token field_name = lexer_next(l);
                if (field_name.kind != TOK_IDENT)
                {
                    zpanic_at(field_name, "Expected field name in launch configuration");
                }

                // Expect ':'
                if (lexer_peek(l).kind != TOK_COLON)
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
                if (lexer_peek(l).kind == TOK_COMMA)
                {
                    lexer_next(l);
                }
            }

            // Expect '}'
            if (lexer_peek(l).kind != TOK_RBRACE)
            {
                zpanic_at(lexer_peek(l), "Expected '}' to close launch configuration");
            }
            lexer_next(l); // eat '}'

            // Expect ';'
            if (lexer_peek(l).kind == TOK_SEMICOLON)
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
            if (ctx->cg.in_defer_block)
            {
                zpanic_at(goto_tok, "'goto' is not allowed inside a 'defer' block");
            }

            Token next = lexer_peek(l);

            // Computed goto: goto *ptr;
            if (next.kind == TOK_OP && next.start[0] == '*')
            {
                lexer_next(l); // eat '*'
                ASTNode *target = parse_expression(ctx, l);
                if (lexer_peek(l).kind == TOK_SEMICOLON)
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
            if (label.kind != TOK_IDENT)
            {
                zpanic_at(label, "Expected label name after goto");
            }
            if (lexer_peek(l).kind == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
            ASTNode *n = ast_create(NODE_GOTO);
            n->goto_stmt.label_name = token_strdup(label);
            n->token = goto_tok;
            if (ctx->hook_zen_trigger)
            {
                ctx->hook_zen_trigger(TRIGGER_GOTO, goto_tok, ctx->config);
            }
            return n;
        }

        // Label detection: identifier followed by : (but not ::)
        {
            Lexer inner_lookahead = *l;
            Token ident = lexer_next(&inner_lookahead);
            Token maybe_colon = lexer_peek(&inner_lookahead);
            if (maybe_colon.kind == TOK_COLON)
            {
                // Check it's not :: (double colon for namespaces)
                lexer_next(&inner_lookahead);
                Token after_colon = lexer_peek(&inner_lookahead);
                if (after_colon.kind != TOK_COLON)
                {
                    // This is a label!
                    lexer_next(l); // eat identifier
                    lexer_next(l); // eat :

                    char *label_name = token_strdup(ident);
                    ASTNode *next = parse_statement(ctx, l);

                    if (next)
                    {
                        if (next->kind == NODE_WHILE)
                        {
                            next->while_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->kind == NODE_FOR)
                        {
                            next->for_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->kind == NODE_FOR_RANGE)
                        {
                            next->for_range.loop_label = label_name;
                            return next;
                        }
                        else if (next->kind == NODE_LOOP)
                        {
                            next->loop_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->kind == NODE_REPEAT)
                        {
                            next->repeat_stmt.loop_label = label_name;
                            return next;
                        }
                        else if (next->kind == NODE_DO_WHILE)
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
            if (t.kind != TOK_STRING && t.kind != TOK_FSTRING && t.kind != TOK_RAW_STRING)
            {
                if (t.kind == TOK_LPAREN)
                {
                    zpanic_at(t,
                              "Expected string literal after '%.*s'. Note: '%.*s' is a keyword and "
                              "does NOT use parentheses ().",
                              (int)tk.len, tk.start, (int)tk.len, tk.start);
                }
                zpanic_at(t, "Expected string literal after print/eprint");
            }

            char *inner = token_get_string_content(t);
            char **used_syms = NULL;
            int used_count = 0;
            char *code = process_printf_sugar(ctx, t, inner, is_ln, target, &used_syms, &used_count,
                                              1, (t.kind == TOK_RAW_STRING), 0);
            zfree(inner);
            if (!code)
            {
                return NULL;
            }

            if (lexer_peek(l).kind == TOK_SEMICOLON)
            {
                lexer_next(l);
            }

            ASTNode *n = ast_create(NODE_RAW_STMT);
            n->token = t;
            // Append semicolon to Statement Expression to make it a valid statement
            char *stmt_code = xmalloc(strlen(code) + 2);
            sprintf(stmt_code, "%s;", code); /* safe */
            zfree(code);
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
    if (lexer_peek(l).kind == TOK_SEMICOLON)
    {
        lexer_next(l);
        has_semi = 1;
    }

    // Auto-print in REPL: If no semicolon (implicit expr at block end)
    // and not an assignment, print it.
    if (ctx->cg.is_repl && s && !has_semi)
    {
        int is_assign = 0;
        if (s->kind == NODE_EXPR_BINARY)
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
    if (s && s->kind == NODE_EXPR_CALL)
    {
        ASTNode *callee = s->call.callee;
        if (callee && callee->kind == NODE_EXPR_VAR)
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
    z_parse_expect(l, TOK_LBRACE, "Expected '{' to start a block");
    enter_scope(ctx);
    ASTNode *head = 0, *tail = 0;
    Token t = lexer_peek(l);
    int unreachable = 0;
    while (1)
    {
        // Granular resync: if a statement sub-parser failed, skip until ';' or '}'
        if (ctx->is_fault_tolerant && ctx->had_error)
        {
            ctx->had_error = 0;
            while (1)
            {
                Token r = lexer_peek(l);
                if (r.kind == TOK_EOF || r.kind == TOK_RBRACE || r.kind == TOK_SEMICOLON)
                {
                    if (r.kind == TOK_SEMICOLON)
                    {
                        lexer_next(l); // consume the semicolon
                    }
                    break;
                }
                lexer_next(l);
            }
            continue;
        }

        skip_comments(l);
        Token tk = lexer_peek(l);
        if (tk.kind == TOK_RBRACE)
        {
            lexer_next(l);
            break;
        }

        if (tk.kind == TOK_EOF)
        {
            break;
        }

        if (tk.kind == TOK_COMPTIME)
        {
            ASTNode *body = parse_comptime_body(ctx, l);
            ASTNode *ct = ast_create(NODE_COMPTIME);
            ct->comptime.body = body;
            ct->comptime.generated = NULL;
            if (!head)
            {
                head = ct;
            }
            else
            {
                tail->next = ct;
            }
            tail = ct;
            continue;
        }

        Token prev_t = lexer_peek(l);
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
            if (s->kind == NODE_RETURN || s->kind == NODE_BREAK || s->kind == NODE_CONTINUE)
            {
                if (unreachable == 0)
                {
                    unreachable = 1;
                }
            }
        }
        else
        {
            // If we didn't get a statement and didn't consume any tokens, we're stuck.
            // Consume at least one token to avoid infinite loop.
            Token cur_t = lexer_peek(l);
            if (cur_t.start == prev_t.start)
            {
                lexer_next(l);
            }
        }
    }

    // Check for unused variables in this block scope
    if (ctx->current_scope && !ctx->cg.is_repl)
    {
        ZenSymbol *sym = ctx->current_scope->symbols;
        while (sym)
        {
            // Skip special names, non-variables, and already warned
            if (!sym->is_used && (sym->kind == SYM_VARIABLE) && sym->name[0] != '_' &&
                strcmp(sym->name, "it") != 0 && strcmp(sym->name, "self") != 0)
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

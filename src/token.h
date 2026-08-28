#include <stddef.h>
// SPDX-License-Identifier: MIT

/**
 * @file token.h
 * @brief Token types, lexer state, and lexer API.
 */

#ifndef TOKEN_H
#define TOKEN_H

typedef struct CompilerConfig CompilerConfig;

/**
 * @brief Token types for the Lexer.
 */
typedef enum
{
    TOK_EOF = 0,    ///< End of File.
    TOK_IDENT,      ///< Identifier (variable, function name).
    TOK_INT,        ///< Integer literal.
    TOK_FLOAT,      ///< Float literal.
    TOK_STRING,     ///< String literal.
    TOK_FSTRING,    ///< Formatted string literal (f"val is {x}").
    TOK_RAW_STRING, ///< Raw string literal (r"..." - no interpolation).
    TOK_CHAR,       ///< Character literal.
    TOK_LPAREN,     ///< (
    TOK_RPAREN,     ///< )
    TOK_LBRACE,     ///< {
    TOK_RBRACE,     ///< }
    TOK_LBRACKET,   ///< [
    TOK_RBRACKET,   ///< ]
    TOK_LANGLE,     ///< <
    TOK_RANGLE,     ///< >
    TOK_COMMA,      ///< ,
    TOK_COLON,      ///< :
    TOK_SEMICOLON,  ///< ;
    TOK_OP,         ///< General operator (e.g. +, *, /).
    TOK_AT,         ///< @
    TOK_DOTDOT,     ///< ..
    TOK_DOTDOT_EQ,  ///< ..= (inclusive range).
    TOK_DOTDOT_LT,  ///< ..< (exclusive range, explicit).
    TOK_ARROW,      ///< -> or =>
    TOK_PIPE,       ///< |> (pipe operator).
    TOK_TEST,       ///< 'test' keyword.
    TOK_ASSERT,     ///< 'assert' keyword.
    TOK_EXPECT,     ///< 'expect' keyword.
    TOK_SIZEOF,     ///< 'sizeof' keyword.
    TOK_DEF,        ///< 'def' keyword.
    TOK_DEFER,      ///< 'defer' keyword.
    TOK_AUTOFREE,   ///< 'autofree' keyword.
    TOK_QUESTION,   ///< ?
    TOK_USE,        ///< 'use' keyword.
    TOK_QQ,         ///< ?? (null coalescing).
    TOK_QQ_EQ,      ///< ??=
    TOK_Q_DOT,      ///< ?. (optional chaining).
    TOK_DCOLON,     ///< ::
    TOK_TRAIT,      ///< 'trait' keyword.
    TOK_IMPL,       ///< 'impl' keyword.
    TOK_AND,        ///< 'and' keyword.
    TOK_OR,         ///< 'or' keyword.
    TOK_NOT,        ///< 'not' keyword.
    TOK_FOR,        ///< 'for' keyword.
    TOK_DO,         ///< 'do' keyword.
    TOK_COMPTIME,   ///< 'comptime' keyword.
    TOK_ELLIPSIS,   ///< ...
    TOK_UNION,      ///< 'union' keyword.
    TOK_ASM,        ///< 'asm' keyword.
    TOK_VOLATILE,   ///< 'volatile' keyword.
    TOK_ASYNC,      ///< 'async' keyword.
    TOK_AWAIT,      ///< 'await' keyword.
    TOK_PREPROC,    ///< Preprocessor directive (#...).
    TOK_ALIAS,      ///< 'alias' keyword.
    TOK_COMMENT,    ///< Comment (usually skipped).
    TOK_OPAQUE,     ///< 'opaque' keyword.
    TOK_UNKNOWN     ///< Unknown token.
} ZenTokenType;

/**
 * @brief Represents a source token.
 */
typedef struct
{
    ZenTokenType kind; ///< Type of the token.
    const char *start; ///< Pointer to start of token in source buffer.
    size_t len;        ///< Length of the token text.
    int line;          ///< Line number (1-based).
    int col;           ///< Column number (1-based).
    const char *file;  ///< Name of file with source code.
} Token;

/** Sentinel for diagnostics with no source location. */
#define TOKEN_UNKNOWN ((Token){0})

/**
 * @brief Lexer state.
 */
typedef struct
{
    const char *src;        ///< Source code buffer.
    int pos;                ///< Current position index.
    int line;               ///< Current line number.
    int col;                ///< Current column number.
    int emit_comments;      ///< 1 if comments should be emitted as tokens.
    CompilerConfig *config; ///< Compiler config (for MISRA mode checks).
    const char *filename;   ///< Name of file being lexed (for token attribution).
} Lexer;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Initialize the lexer.
     */
    void lexer_init(Lexer *l, const char *src, CompilerConfig *cfg, const char *filename);

    /**
     * @brief Get the next token.
     */
    Token lexer_next(Lexer *l);

    /**
     * @brief Get the next token without advancing.
     */
    Token lexer_peek(Lexer *l);

    /**
     * @brief Get the next token without advancing (2 look ahead).
     */
    Token lexer_peek2(Lexer *l);

#ifdef __cplusplus
}
#endif

#endif // TOKEN_H

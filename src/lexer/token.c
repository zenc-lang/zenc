// SPDX-License-Identifier: MIT

#include "zprep.h"

void lexer_init(Lexer *l, const char *src, CompilerConfig *cfg, const char *filename)
{
    l->src = src;
    l->pos = 0;
    l->line = 1;
    l->col = 1;
    l->emit_comments = 0;
    l->config = cfg;
    l->filename = filename;
}

static int is_ident_start(char c)
{
    return isalpha(c) || c == '_';
}

static int is_ident_char(char c)
{
    return isalnum(c) || c == '_';
}

static int lexer_scan_string_internal(Lexer *l, const char *s, char quote, int is_raw,
                                      int prefix_len)
{
    int is_multi = 0;
    if (quote == '"' && s[prefix_len + 1] == '"' && s[prefix_len + 2] == '"')
    {
        is_multi = 1;
    }

    int len = prefix_len + (is_multi ? 3 : 1);
    while (s[len])
    {
        if (is_multi && s[len] == quote && s[len + 1] == quote && s[len + 2] == quote)
        {
            len += 3;
            break;
        }
        else if (!is_multi && s[len] == quote)
        {
            len++;
            break;
        }

        if (s[len] == '\\' && !is_multi)
        {
            if (is_raw)
            {
                if (s[len + 1] == quote)
                {
                    len++; // Skip escaped quote
                }
            }
            else
            {
                len++; // Skip escape char
            }
        }
        len++;
    }

    // Update lexer line/col
    for (int i = 0; i < len; i++)
    {
        if (s[i] == '\n')
        {
            l->line++;
            l->col = 1;
        }
        else
        {
            l->col++;
        }
    }
    return len;
}

Token lexer_next(Lexer *l)
{
    const char *s = l->src + l->pos;
    int start_line = l->line;
    int start_col = l->col;

    while (isspace(*s))
    {
        if (*s == '\n')
        {
            l->line++;
            l->col = 1;
        }
        else
        {
            l->col++;
        }
        l->pos++;
        s++;
        start_line = l->line;
        start_col = l->col;
    }

    // Check for EOF.
    if (!*s)
    {
        return (Token){TOK_EOF, s, 0, start_line, start_col, l->filename};
    }

    // C preprocessor directives.
    if (*s == '#')
    {
        int len = 0;
        while (s[len] && s[len] != '\n')
        {
            if (s[len] == '\\' && s[len + 1] == '\n')
            {
                len += 2;
                l->line++;
            }
            else if (s[len] == '\\' && s[len + 1] == '\r' && s[len + 2] == '\n')
            {
                len += 3;
                l->line++;
            }
            else
            {
                len++;
            }
        }
        l->pos += len;

        return (Token){TOK_PREPROC, s, (size_t)len, start_line, start_col, l->filename};
    }

    // Comments.
    if (s[0] == '/' && s[1] == '/')
    {
        int len = 2;
        while (s[len] && s[len] != '\n')
        {
            if (l->config->misra_mode)
            {
                if ((s[len] == '/' && s[len + 1] == '/') || (s[len] == '/' && s[len + 1] == '*'))
                {
                    zerror_at((Token){TOK_COMMENT, s, (size_t)(len + 2), start_line, start_col,
                                      l->filename},
                              "MISRA Rule 3.1: '//' or '/*' within a comment");
                }
            }
            len++;
        }

        if (l->emit_comments)
        {
            l->pos += len;
            l->col += len;
            return (Token){TOK_COMMENT, s, (size_t)len, start_line, start_col, l->filename};
        }

        l->pos += len;
        l->col += len;
        return lexer_next(l);
    }

    // Block Comments.
    if (s[0] == '/' && s[1] == '*')
    {
        const char *comment_start = s;
        // skip two start chars
        l->pos += 2;
        s += 2;

        while (s[0])
        {
            if (l->config->misra_mode)
            {
                // Check for nested /* or //
                if ((s[0] == '/' && s[1] == '*') || (s[0] == '/' && s[1] == '/'))
                {
                    zerror_at((Token){TOK_COMMENT, comment_start, (size_t)(s - comment_start) + 2,
                                      start_line, start_col, l->filename},
                              "MISRA Rule 3.1: '/*' or '//' within a comment");
                }
            }

            // s[len+1] can be at most the null terminator
            if (s[0] == '*' && s[1] == '/')
            {
                // go over */
                l->pos += 2;
                s += 2;
                break;
            }

            if (s[0] == '\n')
            {
                l->line++;
                l->col = 1;
            }
            else
            {
                l->col++;
            }

            l->pos++;
            s++;
        }

        if (l->emit_comments)
        {
            size_t len = (size_t)(s - comment_start);
            return (Token){TOK_COMMENT, comment_start, (size_t)len,
                           start_line,  start_col,     l->filename};
        }

        return lexer_next(l);
    }

    // Identifiers.
    if (is_ident_start(*s))
    {
        int len = 0;
        while (is_ident_char(s[len]))
        {
            len++;
        }

        l->pos += len;
        l->col += len;

        if (len == 4 && strncmp(s, "test", 4) == 0)
        {
            return (Token){TOK_TEST, s, 4, start_line, start_col, l->filename};
        }
        if (len == 6 && strncmp(s, "assert", 6) == 0)
        {
            return (Token){TOK_ASSERT, s, 6, start_line, start_col, l->filename};
        }
        if (len == 6 && strncmp(s, "expect", 6) == 0)
        {
            return (Token){TOK_EXPECT, s, 6, start_line, start_col, l->filename};
        }
        if (len == 6 && strncmp(s, "sizeof", 6) == 0)
        {
            return (Token){TOK_SIZEOF, s, 6, start_line, start_col, l->filename};
        }
        if (len == 5 && strncmp(s, "defer", 5) == 0)
        {
            return (Token){TOK_DEFER, s, 5, start_line, start_col, l->filename};
        }
        if (len == 3 && strncmp(s, "def", 3) == 0)
        {
            return (Token){TOK_DEF, s, 3, start_line, start_col, l->filename};
        }
        if (len == 5 && strncmp(s, "trait", 5) == 0)
        {
            return (Token){TOK_TRAIT, s, 5, start_line, start_col, l->filename};
        }
        if (len == 4 && strncmp(s, "impl", 4) == 0)
        {
            return (Token){TOK_IMPL, s, 4, start_line, start_col, l->filename};
        }
        if (len == 8 && strncmp(s, "autofree", 8) == 0)
        {
            return (Token){TOK_AUTOFREE, s, 8, start_line, start_col, l->filename};
        }
        if (len == 5 && strncmp(s, "alias", 5) == 0)
        {
            return (Token){TOK_ALIAS, s, 5, start_line, start_col, l->filename};
        }
        if (len == 3 && strncmp(s, "use", 3) == 0)
        {
            return (Token){TOK_USE, s, 3, start_line, start_col, l->filename};
        }
        if (len == 8 && strncmp(s, "comptime", 8) == 0)
        {
            return (Token){TOK_COMPTIME, s, 8, start_line, start_col, l->filename};
        }
        if (len == 5 && strncmp(s, "union", 5) == 0)
        {
            return (Token){TOK_UNION, s, 5, start_line, start_col, l->filename};
        }
        if (len == 3 && strncmp(s, "asm", 3) == 0)
        {
            return (Token){TOK_ASM, s, 3, start_line, start_col, l->filename};
        }
        if (len == 8 && strncmp(s, "volatile", 8) == 0)
        {
            return (Token){TOK_VOLATILE, s, 8, start_line, start_col, l->filename};
        }
        if (len == 5 && strncmp(s, "async", 5) == 0)
        {
            return (Token){TOK_ASYNC, s, 5, start_line, start_col, l->filename};
        }
        if (len == 5 && strncmp(s, "await", 5) == 0)
        {
            return (Token){TOK_AWAIT, s, 5, start_line, start_col, l->filename};
        }
        if (len == 3 && strncmp(s, "and", 3) == 0)
        {
            return (Token){TOK_AND, s, 3, start_line, start_col, l->filename};
        }
        if (len == 2 && strncmp(s, "or", 2) == 0)
        {
            return (Token){TOK_OR, s, 2, start_line, start_col, l->filename};
        }
        if (len == 3 && strncmp(s, "not", 3) == 0)
        {
            return (Token){TOK_NOT, s, 3, start_line, start_col, l->filename};
        }
        if (len == 6 && strncmp(s, "opaque", 6) == 0)
        {
            return (Token){TOK_OPAQUE, s, 6, start_line, start_col, l->filename};
        }
        if (len == 2 && strncmp(s, "do", 2) == 0)
        {
            return (Token){TOK_DO, s, 2, start_line, start_col, l->filename};
        }

        // F-Strings
        if (len == 1 && s[0] == 'f' && s[1] == '"')
        {
            // Reset pos/col because we want to parse string
            l->pos -= len;
            l->col -= len;
        }
        // Raw Strings
        else if (len == 1 && s[0] == 'r' && (s[1] == '"' || s[1] == '\''))
        {
            // Reset pos/col because we want to parse string
            l->pos -= len;
            l->col -= len;
        }
        else
        {
            return (Token){TOK_IDENT, s, (size_t)len, start_line, start_col, l->filename};
        }
    }

    if (s[0] == 'f' && s[1] == '"')
    {
        int len = lexer_scan_string_internal(l, s, '"', 0, 1);
        l->pos += len;
        return (Token){TOK_FSTRING, s, (size_t)len, start_line, start_col, l->filename};
    }

    // Raw Strings (r"..." or r'...' or r"""...""")
    if (s[0] == 'r' && (s[1] == '"' || s[1] == '\''))
    {
        char quote = s[1];
        int len = lexer_scan_string_internal(l, s, quote, 1, 1);
        l->pos += len;
        return (Token){TOK_RAW_STRING, s, (size_t)len, start_line, start_col, l->filename};
    }

    // Numbers
    if (isdigit(*s))
    {
        int len = 0;
        int is_hex = 0;
        int is_bin = 0;
        int is_oct = 0;

        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        {
            is_hex = 1;
            len = 2;
            while (isxdigit(s[len]) || s[len] == '_')
            {
                len++;
            }
        }
        else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
        {
            is_bin = 1;
            len = 2;
            while (s[len] == '0' || s[len] == '1' || s[len] == '_')
            {
                len++;
            }
        }
        else if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
        {
            is_oct = 1;
            len = 2;
            while ((s[len] >= '0' && s[len] <= '7') || s[len] == '_')
            {
                len++;
            }
        }
        else
        {
            if (s[0] == '0' && isdigit(s[1]) && l->config->misra_mode)
            {
                // Rule 7.1: Octal constants shall not be used (and leading zeros are disallowed).
                zerror_at((Token){TOK_INT, s, 2, start_line, start_col, l->filename},
                          "MISRA Rule 7.1");
            }
            while (isdigit(s[len]) || s[len] == '_')
            {
                len++;
            }
        }

        if (!is_hex && !is_bin && !is_oct)
        {
            int is_float = 0;
            if (s[len] == '.' && isdigit(s[len + 1]))
            {
                if (s[len + 1] != '.')
                {
                    is_float = 1;
                    len++;
                    while (isdigit(s[len]) || s[len] == '_')
                    {
                        len++;
                    }
                }
            }

            if (s[len] == 'e' || s[len] == 'E')
            {
                is_float = 1;
                len++;
                if (s[len] == '+' || s[len] == '-')
                {
                    len++;
                }
                while (isdigit(s[len]) || s[len] == '_')
                {
                    len++;
                }
            }

            if (is_float)
            {
                if (is_ident_start(s[len]))
                {
                    while (is_ident_char(s[len]))
                    {
                        len++;
                    }
                }
                l->pos += len;
                l->col += len;
                return (Token){TOK_FLOAT, s, (size_t)len, start_line, start_col, l->filename};
            }
        }

        if (is_ident_start(s[len]))
        {
            while (is_ident_char(s[len]))
            {
                len++;
            }
        }

        l->pos += len;
        l->col += len;
        return (Token){TOK_INT, s, (size_t)len, start_line, start_col, l->filename};
    }

    // Strings
    if (*s == '"')
    {
        int len = lexer_scan_string_internal(l, s, '"', 0, 0);
        l->pos += len;
        return (Token){TOK_STRING, s, (size_t)len, start_line, start_col, l->filename};
    }

    if (*s == '\'')
    {
        int len = 1;
        if (s[len] == '\\')
        {
            len++;
            if ((s[len] == 'u' || s[len] == 'U') && s[len + 1] == '{')
            {
                len += 2;
                while ((s[len] >= '0' && s[len] <= '9') || (s[len] >= 'a' && s[len] <= 'f') ||
                       (s[len] >= 'A' && s[len] <= 'F'))
                {
                    len++;
                }
                if (s[len] == '}')
                {
                    len++;
                }
            }
            else
            {
                len++;
            }
        }
        else
        {
            unsigned char first = (unsigned char)s[len];
            if ((first & 0x80) == 0)
            {
                len++;
            }
            else if ((first & 0xE0) == 0xC0)
            {
                len += 2;
            }
            else if ((first & 0xF0) == 0xE0)
            {
                len += 3;
            }
            else if ((first & 0xF8) == 0xF0)
            {
                len += 4;
            }
            else
            {
                len++;
            }
        }
        if (s[len] == '\'')
        {
            len++;
        }

        l->pos += len;
        l->col += len;
        return (Token){TOK_CHAR, s, (size_t)len, start_line, start_col, l->filename};
    }

    // Operators.
    int len = 1;
    ZenTokenType type = TOK_OP;

    if (s[0] == '?' && s[1] == '.')
    {
        len = 2;
        type = TOK_Q_DOT;
    }
    else if (s[0] == '?' && s[1] == '?')
    {
        if (s[2] == '=')
        {
            len = 3;
            type = TOK_QQ_EQ;
        }
        else
        {
            len = 2;
            type = TOK_QQ;
        }
    }
    else if (*s == '?')
    {
        type = TOK_QUESTION;
    }
    else if (s[0] == '|' && s[1] == '>')
    {
        len = 2;
        type = TOK_PIPE;
    }
    else if (s[0] == ':' && s[1] == ':')
    {
        len = 2;
        type = TOK_DCOLON;
    }
    else if (s[0] == '.' && s[1] == '.' && s[2] == '.')
    {
        len = 3;
        type = TOK_ELLIPSIS;
    }
    else if (s[0] == '.' && s[1] == '.')
    {
        if (s[2] == '=')
        {
            len = 3;
            type = TOK_DOTDOT_EQ;
        }
        else if (s[2] == '<')
        {
            len = 3;
            type = TOK_DOTDOT_LT;
        }
        else
        {
            len = 2;
            type = TOK_DOTDOT;
        }
    }
    else if ((s[0] == '-' && s[1] == '>') || (s[0] == '=' && s[1] == '>'))
    {
        len = 2;
        type = TOK_ARROW;
    }

    else if ((s[0] == '<' && s[1] == '<') || (s[0] == '>' && s[1] == '>'))
    {
        len = 2;
        if (s[2] == '=')
        {
            len = 3; // Handle <<= and >>=
        }
    }
    else if ((s[0] == '&' && s[1] == '&') || (s[0] == '|' && s[1] == '|') ||
             (s[0] == '+' && s[1] == '+') || (s[0] == '-' && s[1] == '-') ||
             (s[0] == '*' && s[1] == '*'))
    {
        len = 2;
        if (s[0] == '*' && s[1] == '*' && s[2] == '=')
        {
            len = 3;
        }
    }
    else if (s[1] == '=' && strchr("=!<>+-*/%|&^", s[0]))
    {
        len = 2;
    }

    else
    {
        switch (*s)
        {

        case '(':
            type = TOK_LPAREN;
            break;
        case ')':
            type = TOK_RPAREN;
            break;
        case '{':
            type = TOK_LBRACE;
            break;
        case '}':
            type = TOK_RBRACE;
            break;
        case '[':
            type = TOK_LBRACKET;
            break;
        case ']':
            type = TOK_RBRACKET;
            break;
        case '<':
            type = TOK_LANGLE;
            break;
        case '>':
            type = TOK_RANGLE;
            break;
        case ',':
            type = TOK_COMMA;
            break;
        case ':':
            type = TOK_COLON;
            break;
        case ';':
            type = TOK_SEMICOLON;
            break;
        case '@':
            type = TOK_AT;
            break;
        default:
            type = TOK_OP;
            break;
        }
    }

    l->pos += len;
    l->col += len;
    return (Token){type, s, (size_t)len, start_line, start_col, l->filename};
}

Token lexer_peek(Lexer *l)
{
    Lexer saved = *l;
    return lexer_next(&saved);
}

Token lexer_peek2(Lexer *l)
{
    Lexer saved = *l;
    lexer_next(&saved);
    return lexer_next(&saved);
}

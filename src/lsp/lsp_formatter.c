// SPDX-License-Identifier: MIT
#include "../constants.h"
#include "lsp_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../zprep.h"
#include <ctype.h>

char *lsp_format_source(const char *src)
{
    if (!src)
    {
        return NULL;
    }

    size_t len = strlen(src);
    char *out = libc_malloc(len * 2 + 1); // Extra space for added characters
    if (!out)
    {
        return NULL;
    }
    char *dst = out;
    const char *s = src;

    int indent_level = 0;
    int at_start_of_line = 1;

    int in_string = 0;
    int in_char = 0;
    int in_single_comment = 0;
    int in_multi_comment = 0;
    int escape_next = 0;

    while (*s)
    {
        if (at_start_of_line)
        {
            if (!in_string && !in_multi_comment && !in_char)
            {
                while (*s == ' ' || *s == '\t')
                {
                    s++;
                }
                if (!*s)
                {
                    break;
                }

                if (*s != '\n' && *s != '\r')
                {
                    int temp_indent = indent_level;
                    if (*s == '}')
                    {
                        temp_indent--;
                    }
                    if (temp_indent < 0)
                    {
                        temp_indent = 0;
                    }

                    for (int i = 0; i < temp_indent * 4; i++)
                    {
                        *dst++ = ' ';
                    }
                }
            }
            at_start_of_line = 0;
        }

        if (escape_next)
        {
            *dst++ = *s;
            escape_next = 0;
            s++;
            continue;
        }

        if (in_single_comment)
        {
            // do nothing special, let loop continue to standard \n logic at bottom
        }
        else if (in_multi_comment)
        {
            if (*s == '*' && *(s + 1) == '/')
            {
                *dst++ = *s++;
                *dst++ = *s;
                in_multi_comment = 0;
                s++;
                continue;
            }
        }
        else if (in_string)
        {
            if (*s == '\\')
            {
                escape_next = 1;
            }
            else if (*s == '"')
            {
                in_string = 0;
            }
        }
        else if (in_char)
        {
            if (*s == '\\')
            {
                escape_next = 1;
            }
            else if (*s == '\'')
            {
                in_char = 0;
            }
        }
        else
        {
            // Normal token parsing
            if (*s == '/' && *(s + 1) == '/')
            {
                in_single_comment = 1;
                *dst++ = *s++;
                *dst++ = *s;
                s++;
                continue;
            }
            else if (*s == '/' && *(s + 1) == '*')
            {
                in_multi_comment = 1;
                *dst++ = *s++;
                *dst++ = *s;
                s++;
                continue;
            }
            else if (*s == '"')
            {
                in_string = 1;
            }
            else if (*s == '\'')
            {
                in_char = 1;
            }
            else if (*s == '{')
            {
                *dst++ = '{';
                indent_level++;
                s++;
                continue;
            }
            else if (*s == '}')
            {
                *dst++ = '}';
                if (indent_level > 0)
                {
                    indent_level--;
                }
                s++;
                continue;
            }
        }

        if (*s == '\n')
        {
            *dst++ = '\n';
            at_start_of_line = 1;
            if (in_single_comment)
            {
                in_single_comment = 0;
            }
        }
        else
        {
            *dst++ = *s;
        }

        s++;
    }

    *dst = 0;
    return out;
}

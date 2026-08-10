// SPDX-License-Identifier: MIT

#include "emitter.h"
#include <stdlib.h>
#include <string.h>

static int emitter_grow(Emitter *e, size_t needed)
{
    if (needed <= e->buffer.cap)
    {
        return 1;
    }
    size_t new_cap = needed + (needed >> 1);
    char *new_buf = realloc(e->buffer.buf, new_cap);
    if (!new_buf)
    {
        return 0;
    }
    e->buffer.buf = new_buf;
    e->buffer.cap = new_cap;
    return 1;
}

static void emitter_write_indent(Emitter *e)
{
    if (!e || e->indent_level <= 0)
    {
        return;
    }
    int spaces = e->indent_level * EMITTER_INDENT_SIZE;
    if (e->mode == EMITTER_FILE)
    {
        if (e->file)
        {
            for (int i = 0; i < spaces; i++)
            {
                fputc(' ', e->file);
            }
        }
    }
    else
    {
        size_t needed = e->buffer.len + (size_t)spaces + 1;
        if (!emitter_grow(e, needed))
        {
            return;
        }
        memset(e->buffer.buf + e->buffer.len, ' ', (size_t)spaces);
        e->buffer.len += (size_t)spaces;
        e->buffer.buf[e->buffer.len] = '\0';
    }
}

static void emitter_flush_buffered(Emitter *e, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (e->pending_indent && data[i] != '\n')
        {
            emitter_write_indent(e);
            e->pending_indent = 0;
        }
        if (e->mode == EMITTER_FILE)
        {
            if (e->file)
            {
                fputc(data[i], e->file);
            }
        }
        else
        {
            size_t needed = e->buffer.len + 2;
            if (!emitter_grow(e, needed))
            {
                return;
            }
            e->buffer.buf[e->buffer.len++] = data[i];
            e->buffer.buf[e->buffer.len] = '\0';
        }
        if (data[i] == '\n')
        {
            e->pending_indent = 1;
            e->output_line++;
        }
    }
}

void emitter_init_file(Emitter *e, FILE *file)
{
    if (!e)
    {
        return;
    }
    e->mode = EMITTER_FILE;
    e->file = file;
    e->indent_level = 0;
    e->pending_indent = 1;
    e->output_line = 1;
    e->saved_count = 0;
}

void emitter_init_buffer(Emitter *e)
{
    if (!e)
    {
        return;
    }
    e->mode = EMITTER_BUFFER;
    e->buffer.buf = NULL;
    e->buffer.len = 0;
    e->buffer.cap = 0;
    e->indent_level = 0;
    e->pending_indent = 1;
    e->output_line = 1;
    e->saved_count = 0;
}

void emitter_printf(Emitter *e, const char *fmt, ...)
{
    if (!e)
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    emitter_vprintf(e, fmt, args);
    va_end(args);
}

void emitter_vprintf(Emitter *e, const char *fmt, va_list args)
{
    if (!e || !fmt)
    {
        return;
    }
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (len < 0)
    {
        return;
    }

    if (len == 0)
    {
        if (e->pending_indent)
        {
            emitter_write_indent(e);
            e->pending_indent = 0;
        }
        return;
    }

    char stack_buf[4096];
    char *buf = stack_buf;
    if ((size_t)len + 1 > sizeof(stack_buf))
    {
        buf = malloc((size_t)len + 1);
        if (!buf)
        {
            return;
        }
    }
    vsnprintf(buf, (size_t)len + 1, fmt, args);
    emitter_flush_buffered(e, buf, (size_t)len);
    if (buf != stack_buf)
    {
        free(buf);
    }
}

void emitter_write(Emitter *e, const void *ptr, size_t size)
{
    if (!e || !ptr || size == 0)
    {
        return;
    }
    if (e->pending_indent)
    {
        emitter_write_indent(e);
        e->pending_indent = 0;
    }
    if (e->mode == EMITTER_FILE)
    {
        if (e->file)
        {
            fwrite(ptr, 1, size, e->file);
        }
    }
    else
    {
        size_t needed = e->buffer.len + size + 1;
        if (!emitter_grow(e, needed))
        {
            return;
        }
        memcpy(e->buffer.buf + e->buffer.len, ptr, (size_t)(size));
        e->buffer.len += size;
        e->buffer.buf[e->buffer.len] = '\0';
    }
}

char *emitter_take_string(Emitter *e)
{
    if (!e || e->mode != EMITTER_BUFFER)
    {
        return NULL;
    }
    char *result = e->buffer.buf;
    e->buffer.buf = NULL;
    e->buffer.len = 0;
    e->buffer.cap = 0;
    return result;
}

void emitter_indent(Emitter *e)
{
    if (e)
    {
        e->indent_level++;
    }
}

void emitter_dedent(Emitter *e)
{
    if (e && e->indent_level > 0)
    {
        e->indent_level--;
    }
}

int emitter_push(Emitter *e)
{
    if (!e || e->saved_count >= EMITTER_SAVED_STACK_MAX)
    {
        return 0;
    }
    EmitterSavedState *s = &e->saved_stack[e->saved_count++];
    s->mode = e->mode;
    if (e->mode == EMITTER_FILE)
    {
        s->file = e->file;
    }
    else
    {
        s->buffer.buf = e->buffer.buf;
        s->buffer.len = e->buffer.len;
        s->buffer.cap = e->buffer.cap;
    }
    s->indent_level = e->indent_level;
    s->output_line = e->output_line;
    return 1;
}

int emitter_pop(Emitter *e)
{
    if (!e || e->saved_count <= 0)
    {
        return 0;
    }
    EmitterSavedState *s = &e->saved_stack[--e->saved_count];
    if (e->mode == EMITTER_BUFFER)
    {
        free(e->buffer.buf);
    }
    e->mode = s->mode;
    if (e->mode == EMITTER_FILE)
    {
        e->file = s->file;
    }
    else
    {
        e->buffer.buf = s->buffer.buf;
        e->buffer.len = s->buffer.len;
        e->buffer.cap = s->buffer.cap;
    }
    e->indent_level = s->indent_level;
    e->output_line = s->output_line;
    return 1;
}

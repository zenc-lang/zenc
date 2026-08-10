// SPDX-License-Identifier: MIT

/**
 * @file arena.h
 * @brief Arena allocator -- xmalloc, xstrdup, zfree macros.
 */

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdlib.h>

// Need to pull in the zarena type definition
#include "utils/zalloc.h"

// Allocation functions declared in zprep.h:
void *xmalloc(size_t size) __attribute__((returns_nonnull));
void *xrealloc(void *ptr, size_t new_size) __attribute__((returns_nonnull));
void *xcalloc(size_t n, size_t size) __attribute__((returns_nonnull));
char *xstrdup(const char *s) __attribute__((returns_nonnull));

/// Arena allocator: allocations via malloc/realloc/calloc go to the arena
/// and are reclaimed all at once (never individually freed). Use zfree()
/// to document no-op frees on arena memory. Use libc_free/libc_malloc etc.
/// for short-lived allocations that need proper heap management.
#define zfree(ptr) ((void)0)
#define malloc(sz) xmalloc(sz)
#define realloc(p, s) xrealloc(p, s)
#define calloc(n, s) xcalloc(n, s)

#endif // ARENA_H

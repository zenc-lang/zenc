// SPDX-License-Identifier: MIT

#ifndef ZC_ALLOW_INTERNAL
#error "utils/zalloc.h is internal to Zen C. Include the appropriate public header instead."
#endif

/*
 * zalloc.h — Modern memory management for C (Arenas, Pools, Debug)
 * Part of Zen Development Kit (ZDK)
 *
 * Repository: https://github.com/z-libs/zalloc.h
 * License: MIT
 * Author: Zuhaitz
 */

#ifndef ZALLOC_H
#define ZALLOC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* libc escape hatches: use these for short-lived allocations that bypass the arena.
   Unlike malloc/free (which are redirected to xmalloc/zfree), these always call
   the real libc malloc/free/realloc. */
static inline void *libc_malloc(size_t size)
{
    return malloc(size);
}
static inline void libc_free(void *ptr)
{
    free(ptr);
}
static inline void *libc_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

/* Feature Flags */
#if !defined(ZALLOC_ARENA_ONLY) && !defined(ZALLOC_POOL_ONLY) && !defined(ZALLOC_DEBUG_ONLY)
#define ZALLOC_ENABLE_ARENA
#define ZALLOC_ENABLE_POOL
#define ZALLOC_ENABLE_DEBUG
#else
#ifdef ZALLOC_ARENA_ONLY
#define ZALLOC_ENABLE_ARENA
#endif
#ifdef ZALLOC_POOL_ONLY
#define ZALLOC_ENABLE_POOL
#endif
#ifdef ZALLOC_DEBUG_ONLY
#define ZALLOC_ENABLE_DEBUG
#endif
#endif

#ifndef ZALLOC_API
#ifdef __cplusplus
#define ZALLOC_API static inline
#else
#define ZALLOC_API static inline
#endif
#endif

/* ASAN redzone poisoning for the arena.
 *
 * Arena allocations are offsets into a single large block, so ASAN cannot
 * detect writes past one sub-allocation into the next. When building under
 * AddressSanitizer, hand each allocation a poisoned 64-byte redzone so an
 * intra-arena overflow is reported at the exact write site. This is compiled
 * out (ZARENA_RZ = 0) in non-sanitizer builds. */
#if defined(__SANITIZE_ADDRESS__)
#define ZARENA_ASAN_REDZONE 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ZARENA_ASAN_REDZONE 1
#endif
#endif
#ifndef ZARENA_ASAN_REDZONE
#define ZARENA_ASAN_REDZONE 0
#endif
#if ZARENA_ASAN_REDZONE
#define ZARENA_RZ 64
void __asan_poison_memory_region(void const volatile *addr, size_t size);
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
#else
#define ZARENA_RZ 0
#endif

/* Backend customization */
#ifndef Z_MALLOC
#define Z_MALLOC(sz) malloc(sz)
#define Z_REALLOC(p, sz) realloc(p, sz)
#define Z_FREE(p) free(p)
#endif

/* --- Z-ARENA --- */
#ifdef ZALLOC_ENABLE_ARENA

#ifndef ZARENA_MAX_ALIGN
#define ZARENA_MAX_ALIGN 16
#endif

#ifndef ZARENA_DEFAULT_BLOCK_SIZE
#define ZARENA_DEFAULT_BLOCK_SIZE 4096
#endif

typedef struct zarena_block zarena_block;

typedef struct zarena
{
    zarena_block *head;
    zarena_block *first;
    size_t total_alloc;
} zarena;

struct zarena_block
{
    struct zarena_block *next;
    size_t capacity;
    size_t used;
    uint8_t data[];
};

ZALLOC_API void zarena_init(zarena *a);
ZALLOC_API void zarena_free(zarena *a);
ZALLOC_API void zarena_reset(zarena *a);
ZALLOC_API void *zarena_alloc(zarena *a, size_t size);
ZALLOC_API void *zarena_alloc_align(zarena *a, size_t size, size_t align);
ZALLOC_API void *zarena_alloc_zero(zarena *a, size_t size);
ZALLOC_API void *zarena_realloc(zarena *a, void *old_ptr, size_t old_size, size_t new_size);

#if defined(__GNUC__) || defined(__clang__)
ZALLOC_API void zarena_free_ptr(zarena **a);
#define ZARENA_AUTO(name)                                                                          \
    zarena name __attribute__((cleanup(zarena_free_ptr)));                                         \
    zarena_init(&name)
#endif

#endif /* ZALLOC_ENABLE_ARENA */

/* --- Z-POOL --- */
#ifdef ZALLOC_ENABLE_POOL

typedef struct zpool zpool;
typedef struct zpool_node
{
    struct zpool_node *next;
} zpool_node;

struct zpool
{
    size_t item_size;
    size_t count_per_block;
    zpool_node *head;
    void **blocks;
    size_t block_count;
    size_t block_cap;
};

ZALLOC_API void zpool_init(zpool *p, size_t item_size, size_t items_per_block);
ZALLOC_API void zpool_free(zpool *p);
ZALLOC_API void *zpool_alloc(zpool *p);
ZALLOC_API void zpool_recycle(zpool *p, void *ptr);

#endif /* ZALLOC_ENABLE_POOL */

/* --- IMPLEMENTATION --- */

#ifdef ZALLOC_ENABLE_ARENA

static inline uintptr_t _zarena_align_ptr(uintptr_t ptr, size_t align)
{
    assert((align & (align - 1)) == 0);
    return (ptr + (align - 1)) & ~(align - 1);
}

static zarena_block *_zarena_new_block(size_t cap)
{
    size_t total_sz = sizeof(zarena_block) + cap;
    zarena_block *b = (zarena_block *)Z_MALLOC(total_sz);
    if (!b)
    {
        return NULL;
    }
    b->next = NULL;
    b->capacity = cap;
    b->used = 0;
    return b;
}

ZALLOC_API void zarena_init(zarena *a)
{
    memset(a, 0, sizeof(zarena));
}

ZALLOC_API void zarena_free(zarena *a)
{
    zarena_block *curr = a->first;
    while (curr)
    {
        zarena_block *next = curr->next;
        Z_FREE(curr);
        curr = next;
    }
    memset(a, 0, sizeof(zarena));
}

#if defined(__GNUC__) || defined(__clang__)
ZALLOC_API void zarena_free_ptr(zarena **a)
{
    zarena_free(*a);
}
#endif

ZALLOC_API void zarena_reset(zarena *a)
{
    zarena_block *curr = a->first;
    while (curr)
    {
        curr->used = 0;
        curr = curr->next;
    }
    a->head = a->first;
    a->total_alloc = 0;
}

typedef struct
{
    zarena_block *block;
    size_t used;
} ZarenaMark;

/// Save current arena position. Use with zarena_restore() to rewind
/// all allocations made after this point.
static inline ZarenaMark zarena_save(zarena *a)
{
    ZarenaMark m = {a->head, a->head ? a->head->used : 0};
    return m;
}

/// Rewind arena to a saved position. Blocks after the saved block are
/// reset; the saved block's used counter is restored. Subsequent
/// allocations overwrite the rewound region.
static inline void zarena_restore(zarena *a, ZarenaMark mark)
{
    if (!mark.block)
    {
        return;
    }
    zarena_block *curr = mark.block->next;
    while (curr)
    {
        curr->used = 0;
        curr = curr->next;
    }
    mark.block->used = mark.used;
    a->head = mark.block;
    a->total_alloc = 0;
    curr = a->first;
    while (curr)
    {
        a->total_alloc += curr->used;
        if (curr == mark.block)
        {
            break;
        }
        curr = curr->next;
    }
}

ZALLOC_API void *zarena_alloc_align(zarena *a, size_t size, size_t align)
{
    if (size == 0)
    {
        return NULL;
    }

    if (a->head)
    {
        uintptr_t base = (uintptr_t)a->head->data;
        uintptr_t curr = base + a->head->used;
        uintptr_t next = _zarena_align_ptr(curr, align);
        size_t padding = next - curr;
        size_t needed = size + padding;

        if (a->head->used + needed + ZARENA_RZ <= a->head->capacity)
        {
            a->head->used += needed + ZARENA_RZ;
            a->total_alloc += size;
            void *p = (char *)a->head->data + (next - base);
#if ZARENA_ASAN_REDZONE
            __asan_unpoison_memory_region(p, size);
            __asan_poison_memory_region((char *)p + size, ZARENA_RZ);
#endif
            return p;
        }
    }

    if (a->head && a->head->next)
    {
        zarena_block *next_blk = a->head->next;
        uintptr_t base = (uintptr_t)next_blk->data;
        uintptr_t start = _zarena_align_ptr(base, align);
        size_t padding = start - base;

        if (size + padding + ZARENA_RZ <= next_blk->capacity)
        {
            a->head = next_blk;
            a->head->used = size + padding + ZARENA_RZ;
            a->total_alloc += size;
            void *p = (char *)next_blk->data + (start - base);
#if ZARENA_ASAN_REDZONE
            __asan_unpoison_memory_region(p, size);
            __asan_poison_memory_region((char *)p + size, ZARENA_RZ);
#endif
            return p;
        }
    }

    size_t next_cap = (a->head ? a->head->capacity * 2 : ZARENA_DEFAULT_BLOCK_SIZE);
    if (next_cap < size + align)
    {
        next_cap = size + align;
    }

    zarena_block *b = _zarena_new_block(next_cap);
    if (!b)
    {
        return NULL;
    }

    if (a->head)
    {
        b->next = a->head->next;
        a->head->next = b;
    }
    else
    {
        a->first = b;
    }
    a->head = b;

    uintptr_t base = (uintptr_t)b->data;
    uintptr_t start = _zarena_align_ptr(base, align);
    size_t padding = start - base;

    b->used = size + padding + ZARENA_RZ;
    a->total_alloc += size;
    void *p = (char *)b->data + (start - base);
#if ZARENA_ASAN_REDZONE
    __asan_unpoison_memory_region(p, size);
    __asan_poison_memory_region((char *)p + size, ZARENA_RZ);
#endif
    return p;
}

ZALLOC_API void *zarena_alloc(zarena *a, size_t size)
{
    return zarena_alloc_align(a, size, ZARENA_MAX_ALIGN);
}
ZALLOC_API void *zarena_alloc_zero(zarena *a, size_t size)
{
    void *p = zarena_alloc(a, size);
    if (p)
    {
        memset(p, 0, size);
    }
    return p;
}

ZALLOC_API void *zarena_realloc(zarena *a, void *old_ptr, size_t old_size, size_t new_size)
{
    if (!old_ptr)
    {
        return zarena_alloc(a, new_size);
    }
    if (new_size <= old_size)
    {
        return old_ptr;
    }

    if (a->head)
    {
        uintptr_t old_p = (uintptr_t)old_ptr;
        uintptr_t data_end = (uintptr_t)a->head->data + a->head->used;
        if (old_p + old_size == data_end &&
            a->head->used + (new_size - old_size) <= a->head->capacity)
        {
            a->head->used += (new_size - old_size);
            a->total_alloc += (new_size - old_size);
            return old_ptr;
        }
    }

    void *new_ptr = zarena_alloc(a, new_size);
    if (new_ptr)
    {
        memcpy(new_ptr, old_ptr, old_size);
    }
    return new_ptr;
}

#endif /* ZALLOC_ENABLE_ARENA */

#ifdef ZALLOC_ENABLE_POOL

ZALLOC_API void zpool_init(zpool *p, size_t item_size, size_t items_per_block)
{
    memset(p, 0, sizeof(zpool));
    size_t align = sizeof(void *);
    if (item_size < sizeof(zpool_node *))
    {
        item_size = sizeof(zpool_node *);
    }
    p->item_size = (item_size + (align - 1)) & ~(align - 1);
    p->count_per_block = (items_per_block < 1) ? 64 : items_per_block;
}

ZALLOC_API void zpool_free(zpool *p)
{
    for (size_t i = 0; i < p->block_count; i++)
    {
        Z_FREE(p->blocks[i]);
    }
    if (p->blocks)
    {
        Z_FREE(p->blocks);
    }
    memset(p, 0, sizeof(zpool));
}

static void _zpool_grow(zpool *p)
{
    size_t block_mem_size = p->item_size * p->count_per_block;
    uint8_t *block = (uint8_t *)Z_MALLOC(block_mem_size);
    if (!block)
    {
        return;
    }

    if (p->block_count == p->block_cap)
    {
        size_t new_cap = (p->block_cap == 0) ? 8 : p->block_cap * 2;
        void **new_list = (void **)Z_REALLOC(p->blocks, new_cap * sizeof(void *));
        if (!new_list)
        {
            Z_FREE(block);
            return;
        }
        p->blocks = new_list;
        p->block_cap = new_cap;
    }
    p->blocks[p->block_count++] = block;

    for (size_t i = 0; i < p->count_per_block - 1; i++)
    {
        zpool_node *node = (zpool_node *)(void *)(block + (i * p->item_size));
        node->next = (zpool_node *)(void *)(block + ((i + 1) * p->item_size));
    }
    zpool_node *last = (zpool_node *)(void *)(block + ((p->count_per_block - 1) * p->item_size));
    last->next = p->head;
    p->head = (zpool_node *)(void *)block;
}

ZALLOC_API void *zpool_alloc(zpool *p)
{
    if (!p->head)
    {
        _zpool_grow(p);
        if (!p->head)
        {
            return NULL;
        }
    }
    zpool_node *node = p->head;
    p->head = node->next;
    return (void *)node;
}

ZALLOC_API void zpool_recycle(zpool *p, void *ptr)
{
    if (!ptr)
    {
        return;
    }
    zpool_node *node = (zpool_node *)ptr;
    node->next = p->head;
    p->head = node;
}

#endif /* ZALLOC_ENABLE_POOL */

#endif /* ZALLOC_H */

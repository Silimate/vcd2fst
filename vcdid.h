/*
 * VCD identifier code -> FST handle, for vcd2fst.
 *
 * A VCD can declare 100M signals or more, so this keeps a few bytes per
 * signal instead of a tree node per id.  An id of up to 9 chars in '!'..'~'
 * reads as a bijective base-94 number, first char least significant: "!" is
 * 1, "~" is 94, "!!" is 95.  Then, cheapest first:
 *
 * - While each new id is the next number (as fst2vcd writes them), the
 *   number is the handle, and only the declared length is kept: 4 bytes a
 *   signal.
 * - While the numbers stay under three a handle (as from writers that count
 *   up by some other rule, or skip some), an array indexed by number holds
 *   the handles: up to 16 bytes a signal, 8-12 for typical writers (and
 *   while it grows, the old copy too).
 * - Otherwise each handle keeps a key (its number, or for an id that has
 *   none, its offset in a string arena with the top bit set) and an
 *   open-addressed table finds it: about 17 bytes a signal.
 *
 * Handles count 1, 2, 3... in the order ids are added, as fstWriterCreateVar
 * numbers them.
 */

#ifndef VCDID_H
#define VCDID_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VCDID_STR (1ULL << 63)

struct vcdid {
    uint32_t count; /* handles 1..count */
    uint64_t cap; /* entries allocated in lens and keys */
    uint32_t *lens; /* declared length of each handle */
    uint32_t *dir; /* handle of each number below dirs; NULL while a number is its handle */
    uint64_t dirs;
    uint64_t *keys; /* key of each handle, once the numbers are too sparse for dir */
    uint32_t *slots; /* handles by hash of their key, 0 for none */
    int bits; /* 1 << bits slots */
    char *arena; /* ids without a number, NUL-terminated */
    uint64_t used, size;
};

/* realloc(p, n), or with p NULL and zero set, calloc(n, 1) */
static inline void *vcdid_alloc(void *p, size_t n, int zero)
{
    p = (zero && !p) ? calloc(n, 1) : realloc(p, n);
    if (!p) {
        fprintf(stderr, "ERROR: Out of memory in vcd2fst id table, exiting!\n");
        exit(255);
    }
    return p;
}

/* The id's number, or 0 when it has none. */
static inline uint64_t vcdid_code(const char *s, size_t n)
{
    uint64_t v = 0;
    if (n < 1 || n > 9) {
        return 0;
    }
    while (n--) {
        unsigned char c = (unsigned char)s[n];
        if (c < '!' || c > '~') {
            return 0;
        }
        v = v * 94 + (c - 32);
    }
    return v;
}

static inline uint64_t vcdid_slot(const struct vcdid *m, uint64_t code, const char *s, size_t n)
{
    if (!code) { /* FNV-1a of the id */
        code = 0xcbf29ce484222325ULL;
        while (n--) {
            code = (code ^ (unsigned char)*s++) * 0x100000001b3ULL;
        }
    }
    return (code * 0x9e3779b97f4a7c15ULL) >> (64 - m->bits);
}

static inline void vcdid_put(struct vcdid *m, uint32_t h)
{
    uint64_t k = m->keys[h];
    const char *s = (k & VCDID_STR) ? m->arena + (k & ~VCDID_STR) : NULL;
    uint64_t mask = (1ULL << m->bits) - 1;
    uint64_t i = vcdid_slot(m, s ? 0 : k, s, s ? strlen(s) : 0);
    while (m->slots[i]) {
        i = (i + 1) & mask;
    }
    m->slots[i] = h;
}

/* The handle of id s[0..n), or 0 when it was never added. */
static inline uint32_t vcdid_find(const struct vcdid *m, const char *s, size_t n)
{
    uint64_t code = vcdid_code(s, n);
    if (!m->keys) {
        if (!m->dir) {
            return (code && code <= m->count) ? (uint32_t)code : 0;
        }
        return code < m->dirs ? m->dir[code] : 0;
    }
    uint64_t mask = (1ULL << m->bits) - 1;
    for (uint64_t i = vcdid_slot(m, code, s, n);; i = (i + 1) & mask) {
        uint32_t h = m->slots[i];
        if (!h) {
            return 0;
        }
        uint64_t k = m->keys[h];
        if (code ? (k == code)
                 : ((k & VCDID_STR) && !strncmp(m->arena + (k & ~VCDID_STR), s, n) &&
                    !m->arena[(k & ~VCDID_STR) + n])) {
            return h;
        }
    }
}

/* Adds id s[0..n), which vcdid_find doesn't know, as the next handle. */
static inline uint32_t vcdid_add(struct vcdid *m, const char *s, size_t n, uint32_t len)
{
    uint64_t code = vcdid_code(s, n);
    uint32_t h = m->count + 1;
    if (h >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 1024;
        m->lens = (uint32_t *)vcdid_alloc(m->lens, m->cap * sizeof(*m->lens), 0);
        if (m->keys) {
            m->keys = (uint64_t *)vcdid_alloc(m->keys, m->cap * sizeof(*m->keys), 0);
        }
    }
    m->lens[h] = len;
    m->count = h;
    if (!m->keys) {
        if (!m->dir && code == h) {
            return h;
        }
        if (code && code < 3ULL * h + 65536) {
            if (code >= m->dirs) { /* grow dir past code; untouched pages stay unmapped */
                uint64_t want = m->dirs ? m->dirs * 2 : 65536;
                while (want <= code) {
                    want *= 2;
                }
                uint32_t *d = (uint32_t *)vcdid_alloc(NULL, want * sizeof(*d), 1);
                if (m->dir) {
                    memcpy(d, m->dir, m->dirs * sizeof(*d));
                } else {
                    for (uint32_t i = 1; i < h; i++) {
                        d[i] = i;
                    }
                }
                free(m->dir);
                m->dir = d;
                m->dirs = want;
            }
            m->dir[code] = h;
            return h;
        }
        /* numbers too sparse, or none: each handle gets a key, and a table */
        m->keys = (uint64_t *)vcdid_alloc(NULL, m->cap * sizeof(*m->keys), 0);
        if (m->dir) {
            for (uint64_t c = 1; c < m->dirs; c++) {
                if (m->dir[c]) {
                    m->keys[m->dir[c]] = c;
                }
            }
            free(m->dir);
            m->dir = NULL;
            m->dirs = 0;
        } else {
            for (uint32_t i = 1; i < h; i++) {
                m->keys[i] = i;
            }
        }
    }
    if (!code) {
        if (m->used + n + 1 > m->size) {
            m->size = (m->used + n + 1) * 2;
            m->arena = (char *)vcdid_alloc(m->arena, m->size, 0);
        }
        memcpy(m->arena + m->used, s, n);
        m->arena[m->used + n] = 0;
        code = VCDID_STR | m->used;
        m->used += n + 1;
    }
    m->keys[h] = code;
    if (m->slots && (uint64_t)h * 4 < (3ULL << m->bits)) {
        vcdid_put(m, h);
        return h;
    }
    /* past 3/4 full: grow the table to at most 3/8 full, and rehash */
    m->bits = m->bits ? m->bits : 10;
    while ((3ULL << m->bits) < (uint64_t)h * 8) {
        m->bits++;
    }
    free(m->slots);
    m->slots = (uint32_t *)vcdid_alloc(NULL, (1ULL << m->bits) * sizeof(*m->slots), 1);
    for (uint32_t i = 1; i <= h; i++) {
        vcdid_put(m, i);
    }
    return h;
}

static inline void vcdid_free(struct vcdid *m)
{
    free(m->lens);
    free(m->dir);
    free(m->keys);
    free(m->slots);
    free(m->arena);
    memset(m, 0, sizeof(*m));
}

#endif

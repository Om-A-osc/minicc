#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- allocation ---------------- */

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) diag_fatal("out of memory");
    return p;
}

void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) diag_fatal("out of memory");
    return p;
}

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = xmalloc(n);
    memcpy(d, s, n);
    return d;
}

char *xstrfmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return s;
}

/* ---------------- vector ---------------- */

Vec *vec_new(void) {
    return xcalloc(1, sizeof(Vec));
}

void vec_push(Vec *v, void *item) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->items = realloc(v->items, sizeof(void *) * (size_t)v->cap);
        if (!v->items) diag_fatal("out of memory");
    }
    v->items[v->len++] = item;
}

void *vec_get(const Vec *v, int i) {
    return v->items[i];
}

void vec_set(Vec *v, int i, void *item) {
    v->items[i] = item;
}

void vec_remove(Vec *v, int i) {
    memmove(&v->items[i], &v->items[i + 1], sizeof(void *) * (size_t)(v->len - i - 1));
    v->len--;
}

/* ---------------- string map ---------------- */

static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

StrMap *strmap_new(void) {
    StrMap *m = xcalloc(1, sizeof(StrMap));
    m->nbuckets = 64;
    m->buckets = xcalloc((size_t)m->nbuckets, sizeof(StrMapEntry *));
    return m;
}

static StrMapEntry *strmap_find(const StrMap *m, const char *key) {
    unsigned long h = hash_str(key) % (unsigned long)m->nbuckets;
    for (StrMapEntry *e = m->buckets[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e;
    return NULL;
}

void *strmap_get(const StrMap *m, const char *key) {
    StrMapEntry *e = strmap_find(m, key);
    return e ? e->val : NULL;
}

int strmap_has(const StrMap *m, const char *key) {
    return strmap_find(m, key) != NULL;
}

static void strmap_grow(StrMap *m) {
    int nb = m->nbuckets * 2;
    StrMapEntry **b = xcalloc((size_t)nb, sizeof(StrMapEntry *));
    for (StrMapEntry *e = m->first; e; e = e->order) {
        unsigned long h = hash_str(e->key) % (unsigned long)nb;
        e->next = b[h];
        b[h] = e;
    }
    free(m->buckets);
    m->buckets = b;
    m->nbuckets = nb;
}

void strmap_put(StrMap *m, const char *key, void *val) {
    StrMapEntry *e = strmap_find(m, key);
    if (e) {
        e->val = val;
        return;
    }
    if (m->count * 4 >= m->nbuckets * 3) strmap_grow(m);
    e = xcalloc(1, sizeof(StrMapEntry));
    e->key = xstrdup(key);
    e->val = val;
    unsigned long h = hash_str(key) % (unsigned long)m->nbuckets;
    e->next = m->buckets[h];
    m->buckets[h] = e;
    if (m->last) m->last->order = e; else m->first = e;
    m->last = e;
    m->count++;
}

/* ---------------- string builder ---------------- */

void sb_init(StrBuf *sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->buf = xmalloc(sb->cap);
    sb->buf[0] = '\0';
}

static void sb_reserve(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 > sb->cap) {
        while (sb->len + extra + 1 > sb->cap) sb->cap *= 2;
        sb->buf = realloc(sb->buf, sb->cap);
        if (!sb->buf) diag_fatal("out of memory");
    }
}

void sb_putc(StrBuf *sb, char c) {
    sb_reserve(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

void sb_puts(StrBuf *sb, const char *s) {
    size_t n = strlen(s);
    sb_reserve(sb, n);
    memcpy(sb->buf + sb->len, s, n + 1);
    sb->len += n;
}

void sb_printf(StrBuf *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->buf + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
}

char *sb_finish(StrBuf *sb) {
    return sb->buf;
}

/* ---------------- diagnostics ---------------- */

const char *g_source_path = "<stdin>";
int g_error_count = 0;
int g_warning_count = 0;

static void vdiag(const char *kind, int line, int col, const char *fmt, va_list ap) {
    fflush(stdout);
    if (line > 0 && col > 0)
        fprintf(stderr, "%s:%d:%d: %s: ", g_source_path, line, col, kind);
    else if (line > 0)
        fprintf(stderr, "%s:%d: %s: ", g_source_path, line, kind);
    else
        fprintf(stderr, "%s: %s: ", g_source_path, kind);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void diag_error(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vdiag("error", line, col, fmt, ap);
    va_end(ap);
    g_error_count++;
}

void diag_warning(int line, int col, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vdiag("warning", line, col, fmt, ap);
    va_end(ap);
    g_warning_count++;
}

void diag_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fflush(stdout);
    fprintf(stderr, "minicc: fatal error: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

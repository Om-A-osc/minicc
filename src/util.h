/* util.h - small shared helpers: arena allocation, growable vectors,
 * string maps, and diagnostics. */
#ifndef MINICC_UTIL_H
#define MINICC_UTIL_H

#include <stdarg.h>
#include <stddef.h>

/* ---- allocation (never freed: a compiler run is short-lived) ---- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
char *xstrdup(const char *s);
char *xstrfmt(const char *fmt, ...);

/* ---- growable pointer vector ---- */
typedef struct {
    void **items;
    int len, cap;
} Vec;

Vec  *vec_new(void);
void  vec_push(Vec *v, void *item);
void *vec_get(const Vec *v, int i);
void  vec_set(Vec *v, int i, void *item);
void  vec_remove(Vec *v, int i);      /* O(n), keeps order */
#define VEC_FOREACH(T, var, vec) \
    for (int _i_##var = 0; _i_##var < (vec)->len; _i_##var++) \
        for (T var = (T)(vec)->items[_i_##var]; var; var = NULL)

/* ---- string -> pointer hash map (insertion ordered iteration) ---- */
typedef struct StrMapEntry {
    char *key;
    void *val;
    struct StrMapEntry *next;   /* chain */
    struct StrMapEntry *order;  /* insertion order list */
} StrMapEntry;

typedef struct {
    StrMapEntry **buckets;
    int nbuckets, count;
    StrMapEntry *first, *last;
} StrMap;

StrMap *strmap_new(void);
void   *strmap_get(const StrMap *m, const char *key);
int     strmap_has(const StrMap *m, const char *key);
void    strmap_put(StrMap *m, const char *key, void *val);  /* overwrite */

/* ---- string builder ---- */
typedef struct {
    char *buf;
    size_t len, cap;
} StrBuf;

void  sb_init(StrBuf *sb);
void  sb_putc(StrBuf *sb, char c);
void  sb_puts(StrBuf *sb, const char *s);
void  sb_printf(StrBuf *sb, const char *fmt, ...);
char *sb_finish(StrBuf *sb);

/* ---- diagnostics ---- */
extern const char *g_source_path;
extern int g_error_count;
extern int g_warning_count;

void diag_error(int line, int col, const char *fmt, ...);
void diag_warning(int line, int col, const char *fmt, ...);
void diag_fatal(const char *fmt, ...);

#endif

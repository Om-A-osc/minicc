/* runtime.c - MiniCC language runtime.
 *
 * Linked into every compiled program. Provides allocation, arrays, string
 * operations, console I/O, interface method lookup, runtime error reporting
 * and the C `main` that calls the compiled program's entry point.
 *
 * Object layout:   [vtable ptr][fields...]
 * Vtable layout:   [class info ptr][method slot 0][method slot 1]...
 * Class info:      [count][id0][fn0][id1][fn1]...   (all 8-byte words)
 * Array layout:    [int32 length, padded to 8][elements...]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void runtime_fail(const char *fmt, int a, int b, int c) {
    fflush(stdout);
    fputs("runtime error: ", stderr);
    fprintf(stderr, fmt, a, b, c);
    fputc('\n', stderr);
    exit(1);
}

void *__mcc_alloc(int size) {
    void *p = calloc(1, size > 0 ? (size_t)size : 1);
    if (!p) runtime_fail("out of memory allocating %d bytes", size, 0, 0);
    return p;
}

void *__mcc_newarray(int n, int elem_size) {
    if (n < 0) runtime_fail("negative array size %d", n, 0, 0);
    size_t bytes = 8 + (size_t)n * (size_t)elem_size;
    char *p = calloc(1, bytes ? bytes : 1);
    if (!p) runtime_fail("out of memory allocating array of %d elements", n, 0, 0);
    *(int32_t *)p = n;
    return p;
}

/* Interface dispatch: find the implementation of method `id` for `obj`. */
void *__mcc_lookup(void *obj, int id) {
    void **vtable = *(void ***)obj;
    int64_t *info = vtable[0];
    int64_t count = info[0];
    for (int64_t i = 0; i < count; i++)
        if (info[1 + 2 * i] == id) return (void *)(intptr_t)info[2 + 2 * i];
    runtime_fail("object does not implement method #%d", id, 0, 0);
    return NULL;
}

void __mcc_print_int(int v) {
    printf("%d", v);
}

void __mcc_print_bool(int b) {
    fputs(b ? "true" : "false", stdout);
}

void __mcc_print_double(double d) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.6g", d);
    if (!strpbrk(buf, ".einEIN")) strcat(buf, ".0");   /* 12 -> 12.0, like most languages */
    fputs(buf, stdout);
}

void __mcc_print_string(const char *s) {
    fputs(s ? s : "(null)", stdout);
}

static char *read_line_raw(void) {
    size_t cap = 128, len = 0;
    char *buf = malloc(cap);
    if (!buf) runtime_fail("out of memory", 0, 0, 0);
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (len + 2 > cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) runtime_fail("out of memory", 0, 0, 0);
        }
        buf[len++] = (char)c;
    }
    if (len && buf[len - 1] == '\r') len--;
    buf[len] = '\0';
    return buf;
}

int __mcc_read_int(void) {
    char *line = read_line_raw();
    long v = strtol(line, NULL, 10);
    free(line);
    return (int)v;
}

char *__mcc_read_line(void) {
    return read_line_raw();
}

char *__mcc_strcat(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    char *r = malloc(la + lb + 1);
    if (!r) runtime_fail("out of memory", 0, 0, 0);
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}

int __mcc_streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

void __mcc_null_fail(int line) {
    runtime_fail("null dereference at line %d", line, 0, 0);
}

void __mcc_bounds_fail(int idx, int len, int line) {
    runtime_fail("array index %d out of bounds (length %d) at line %d", idx, len, line);
}

void __mcc_div_zero_fail(int line) {
    runtime_fail("division by zero at line %d", line, 0, 0);
}

int __mcc_entry(void);

int main(void) {
    int r = __mcc_entry();
    fflush(stdout);
    return r;
}

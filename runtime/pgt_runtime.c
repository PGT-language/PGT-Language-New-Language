// PGT runtime: the support library every compiled PGT program is linked with.
//
// Conventions shared with the code generator:
//   string -> const char* (NUL-terminated, immutable, NULL means "")
//   []T    -> PgtArray passed by pointer
//   bool   -> C bool (i1 zeroext in LLVM)
// Memory is never reclaimed automatically: strings produced here are heap allocated
// and live until the program exits.

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct
{
    void *data;
    int64_t len;
    int64_t cap;
} PgtArray;

static int g_argc;
static char **g_argv;

void pgt_rt_init(int argc, char **argv)
{
    g_argc = argc;
    g_argv = argv;
    srand((unsigned)time(NULL));
}

// ---------------------------------------------------------------- errors

_Noreturn void pgt_panic(const char *msg)
{
    fflush(stdout);
    fprintf(stderr, "panic: %s\n", msg ? msg : "");
    exit(2);
}

_Noreturn void pgt_panic_index(int64_t index, int64_t len)
{
    fflush(stdout);
    fprintf(stderr, "panic: index out of range [%lld] with length %lld\n", (long long)index,
            (long long)len);
    exit(2);
}

_Noreturn void pgt_panic_nil(void)
{
    pgt_panic("nil pointer dereference");
}

_Noreturn void pgt_panic_div(void)
{
    pgt_panic("integer divide by zero");
}

// ---------------------------------------------------------------- memory

void *pgt_alloc(int64_t size)
{
    void *p = calloc(1, size > 0 ? (size_t)size : 1);
    if (!p)
        pgt_panic("out of memory");
    return p;
}

void pgt_free(void *p)
{
    free(p);
}

void pgt_array_reserve(PgtArray *a, int64_t need, int64_t elemSize)
{
    if (need <= a->cap)
        return;
    int64_t cap = a->cap ? a->cap * 2 : 4;
    while (cap < need)
        cap *= 2;
    void *data = realloc(a->data, (size_t)(cap * elemSize));
    if (!data)
        pgt_panic("out of memory");
    memset((char *)data + a->len * elemSize, 0, (size_t)((cap - a->len) * elemSize));
    a->data = data;
    a->cap = cap;
}

void pgt_array_make(PgtArray *out, int64_t len, int64_t elemSize)
{
    if (len < 0)
        pgt_panic("make: negative length");
    out->data = len ? pgt_alloc(len * elemSize) : NULL;
    out->len = len;
    out->cap = len;
}

// ---------------------------------------------------------------- strings

static const char *S(const char *s)
{
    return s ? s : "";
}

static char *newString(size_t n)
{
    char *p = malloc(n + 1);
    if (!p)
        pgt_panic("out of memory");
    p[n] = '\0';
    return p;
}

static char *dupRange(const char *s, size_t n)
{
    char *p = newString(n);
    memcpy(p, s, n);
    return p;
}

int32_t pgt_strlen(const char *s)
{
    return (int32_t)strlen(S(s));
}

int32_t pgt_strcmp(const char *a, const char *b)
{
    int r = strcmp(S(a), S(b));
    return r < 0 ? -1 : r > 0;
}

const char *pgt_concat(const char *a, const char *b)
{
    a = S(a);
    b = S(b);
    size_t la = strlen(a), lb = strlen(b);
    char *p = newString(la + lb);
    memcpy(p, a, la);
    memcpy(p + la, b, lb);
    return p;
}

const char *pgt_str_int(int32_t v)
{
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%d", v);
    return dupRange(buf, (size_t)n);
}

const char *pgt_str_int64(int64_t v)
{
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%lld", (long long)v);
    return dupRange(buf, (size_t)n);
}

const char *pgt_str_float(double v)
{
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%g", v);
    return dupRange(buf, (size_t)n);
}

const char *pgt_str_float_prec(double v, int32_t prec)
{
    if (prec < 0)
        prec = 0;
    if (prec > 30)
        prec = 30;
    char buf[400];
    int n = snprintf(buf, sizeof buf, "%.*f", prec, v);
    return dupRange(buf, (size_t)n);
}

const char *pgt_str_char(char c)
{
    return dupRange(&c, 1);
}

const char *pgt_str_bool(bool b)
{
    return b ? "true" : "false";
}

const char *pgt_substr(const char *s, int32_t from, int32_t n)
{
    s = S(s);
    int32_t len = (int32_t)strlen(s);
    if (from < 0)
        from = 0;
    if (from > len)
        from = len;
    if (n < 0 || from + n > len)
        n = len - from;
    return dupRange(s + from, (size_t)n);
}

int32_t pgt_index_of(const char *s, const char *sub)
{
    s = S(s);
    const char *p = strstr(s, S(sub));
    return p ? (int32_t)(p - s) : -1;
}

int32_t pgt_last_index_of(const char *s, const char *sub)
{
    s = S(s);
    sub = S(sub);
    size_t ls = strlen(s), lsub = strlen(sub);
    if (lsub > ls)
        return -1;
    for (size_t i = ls - lsub + 1; i-- > 0;)
        if (memcmp(s + i, sub, lsub) == 0)
            return (int32_t)i;
    return -1;
}

bool pgt_contains(const char *s, const char *sub)
{
    return strstr(S(s), S(sub)) != NULL;
}

bool pgt_starts_with(const char *s, const char *prefix)
{
    s = S(s);
    prefix = S(prefix);
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool pgt_ends_with(const char *s, const char *suffix)
{
    s = S(s);
    suffix = S(suffix);
    size_t ls = strlen(s), lx = strlen(suffix);
    return lx <= ls && memcmp(s + ls - lx, suffix, lx) == 0;
}

const char *pgt_upper(const char *s)
{
    s = S(s);
    size_t n = strlen(s);
    char *p = newString(n);
    for (size_t i = 0; i < n; ++i)
        p[i] = (char)toupper((unsigned char)s[i]);
    return p;
}

const char *pgt_lower(const char *s)
{
    s = S(s);
    size_t n = strlen(s);
    char *p = newString(n);
    for (size_t i = 0; i < n; ++i)
        p[i] = (char)tolower((unsigned char)s[i]);
    return p;
}

const char *pgt_replace(const char *s, const char *old, const char *with, int32_t limit)
{
    s = S(s);
    old = S(old);
    with = S(with);
    size_t lo = strlen(old), lw = strlen(with);
    if (lo == 0 || limit == 0)
        return s;
    size_t count = 0;
    for (const char *p = s; (p = strstr(p, old)) && (limit < 0 || (int32_t)count < limit); p += lo)
        ++count;
    char *out = newString(strlen(s) + count * lw - count * lo);
    char *w = out;
    const char *p = s;
    for (size_t i = 0; i < count; ++i)
    {
        const char *hit = strstr(p, old);
        memcpy(w, p, (size_t)(hit - p));
        w += hit - p;
        memcpy(w, with, lw);
        w += lw;
        p = hit + lo;
    }
    strcpy(w, p);
    return out;
}

const char *pgt_repeat(const char *s, int32_t n)
{
    s = S(s);
    if (n <= 0)
        return "";
    size_t ls = strlen(s);
    char *out = newString(ls * (size_t)n);
    for (int32_t i = 0; i < n; ++i)
        memcpy(out + ls * (size_t)i, s, ls);
    return out;
}

const char *pgt_join(const PgtArray *parts, const char *sep)
{
    sep = S(sep);
    const char **items = parts->data;
    size_t lsep = strlen(sep), total = 0;
    for (int64_t i = 0; i < parts->len; ++i)
        total += strlen(S(items[i])) + (i ? lsep : 0);
    char *out = newString(total);
    char *w = out;
    for (int64_t i = 0; i < parts->len; ++i)
    {
        if (i)
        {
            memcpy(w, sep, lsep);
            w += lsep;
        }
        size_t l = strlen(S(items[i]));
        memcpy(w, S(items[i]), l);
        w += l;
    }
    return out;
}

const char *pgt_from_chars(const PgtArray *chars)
{
    return dupRange(chars->data ? chars->data : "", (size_t)chars->len);
}

void pgt_to_chars(PgtArray *out, const char *s)
{
    s = S(s);
    int64_t n = (int64_t)strlen(s);
    pgt_array_make(out, n, 1);
    if (n)
        memcpy(out->data, s, (size_t)n);
}

int32_t pgt_parse_int(const char *s)
{
    return (int32_t)strtol(S(s), NULL, 10);
}

int64_t pgt_parse_int64(const char *s)
{
    return (int64_t)strtoll(S(s), NULL, 10);
}

double pgt_parse_float(const char *s)
{
    return strtod(S(s), NULL);
}

bool pgt_is_int(const char *s)
{
    s = S(s);
    while (isspace((unsigned char)*s))
        ++s;
    if (!*s)
        return false;
    char *end;
    errno = 0;
    strtoll(s, &end, 10);
    while (isspace((unsigned char)*end))
        ++end;
    return errno == 0 && *end == '\0';
}

bool pgt_is_float(const char *s)
{
    s = S(s);
    while (isspace((unsigned char)*s))
        ++s;
    if (!*s)
        return false;
    char *end;
    strtod(s, &end);
    while (isspace((unsigned char)*end))
        ++end;
    return *end == '\0';
}

// ---------------------------------------------------------------- output

void pgt_print_str(const char *s)
{
    fputs(S(s), stdout);
}
void pgt_print_int(int32_t v)
{
    printf("%d", v);
}
void pgt_print_int64(int64_t v)
{
    printf("%lld", (long long)v);
}
void pgt_print_float(double v)
{
    printf("%g", v);
}
void pgt_print_char(char c)
{
    putchar((unsigned char)c);
}
void pgt_print_bool(bool b)
{
    fputs(b ? "true" : "false", stdout);
}
void pgt_print_ptr(const void *p)
{
    if (p)
        printf("%p", p);
    else
        fputs("<nil>", stdout);
}

// ---------------------------------------------------------------- input
// Each reader returns the current value unchanged when nothing could be read.

static void flushOut(void)
{
    fflush(stdout);
}

int32_t pgt_read_int(int32_t cur)
{
    flushOut();
    int v;
    return scanf("%d", &v) == 1 ? v : cur;
}

int64_t pgt_read_int64(int64_t cur)
{
    flushOut();
    long long v;
    return scanf("%lld", &v) == 1 ? (int64_t)v : cur;
}

double pgt_read_float(double cur)
{
    flushOut();
    double v;
    return scanf("%lf", &v) == 1 ? v : cur;
}

char pgt_read_char(char cur)
{
    flushOut();
    char c;
    return scanf(" %c", &c) == 1 ? c : cur;
}

const char *pgt_read_word(const char *cur)
{
    flushOut();
    char buf[4096];
    return scanf(" %4095s", buf) == 1 ? dupRange(buf, strlen(buf)) : cur;
}

bool pgt_read_bool(bool cur)
{
    const char *w = pgt_read_word(NULL);
    if (!w)
        return cur;
    if (strcmp(w, "true") == 0 || strcmp(w, "1") == 0 || strcmp(w, "yes") == 0 || strcmp(w, "y") == 0)
        return true;
    if (strcmp(w, "false") == 0 || strcmp(w, "0") == 0 || strcmp(w, "no") == 0 || strcmp(w, "n") == 0)
        return false;
    return cur;
}

const char *pgt_read_line(const char *cur)
{
    flushOut();
    char buf[4096];
    return scanf(" %4095[^\n]", buf) == 1 ? dupRange(buf, strlen(buf)) : cur;
}

// Reads one raw line (without skipping blank lines); `ok` tells whether the input had one.
const char *pgt_read_raw_line(bool *ok)
{
    flushOut();
    size_t cap = 128, len = 0;
    char *buf = malloc(cap);
    int c;
    while ((c = getchar()) != EOF && c != '\n')
    {
        if (len + 1 >= cap)
            buf = realloc(buf, cap *= 2);
        buf[len++] = (char)c;
    }
    *ok = !(c == EOF && len == 0);
    if (len && buf[len - 1] == '\r')
        --len;
    buf[len] = '\0';
    return buf;
}

// ---------------------------------------------------------------- files

const char *pgt_read_file(const char *path)
{
    FILE *f = fopen(S(path), "rb");
    if (!f)
        return "";
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0)
    {
        len += n;
        if (cap - len - 1 == 0)
            buf = realloc(buf, cap *= 2);
    }
    fclose(f);
    buf[len] = '\0';
    return buf;
}

static bool writeMode(const char *path, const char *s, const char *mode)
{
    FILE *f = fopen(S(path), mode);
    if (!f)
        return false;
    s = S(s);
    size_t n = strlen(s);
    bool ok = fwrite(s, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

bool pgt_write_file(const char *path, const char *s)
{
    return writeMode(path, s, "wb");
}

bool pgt_append_file(const char *path, const char *s)
{
    return writeMode(path, s, "ab");
}

bool pgt_file_exists(const char *path)
{
    struct stat st;
    return stat(S(path), &st) == 0;
}

bool pgt_is_dir(const char *path)
{
    struct stat st;
    return stat(S(path), &st) == 0 && S_ISDIR(st.st_mode);
}

bool pgt_remove_file(const char *path)
{
    return remove(S(path)) == 0;
}

// ---------------------------------------------------------------- system

int32_t pgt_rand(void)
{
    return (int32_t)rand();
}

void pgt_srand(int32_t seed)
{
    srand((unsigned)seed);
}

int32_t pgt_argc(void)
{
    return g_argc;
}

const char *pgt_argv(int32_t i)
{
    return i >= 0 && i < g_argc ? g_argv[i] : "";
}

const char *pgt_getenv(const char *name)
{
    const char *v = getenv(S(name));
    return v ? v : "";
}

int32_t pgt_system(const char *cmd)
{
    fflush(stdout);
    return (int32_t)system(S(cmd));
}

int64_t pgt_time_unix(void)
{
    return (int64_t)time(NULL);
}

int64_t pgt_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t pgt_clock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void pgt_sleep_ms(int32_t ms)
{
    fflush(stdout);
    if (ms <= 0)
        return;
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
    {
    }
}

const char *pgt_format_time(int64_t unix, const char *layout)
{
    time_t t = (time_t)unix;
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[512];
    size_t n = strftime(buf, sizeof buf, S(layout), &tm);
    return dupRange(buf, n);
}

void pgt_exit(int32_t code)
{
    fflush(stdout);
    exit(code);
}

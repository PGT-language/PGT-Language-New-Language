#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PGT_KERNEL_HEAP_SIZE
#define PGT_KERNEL_HEAP_SIZE (16u * 1024u * 1024u)
#endif

typedef struct
{
    void *data;
    int64_t len;
    int64_t cap;
    void *base;
} PgtArray;

typedef struct
{
    int64_t rc;
    void (*drop)(void *);
    int64_t count;
    int64_t stride;
} Hdr;

extern _Noreturn void pgt_panic(const char *msg) __asm__("pgt.runtime.panicString");

_Noreturn void pgt_halt(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static bool g_serial;

static void serialInit(void)
{
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
    g_serial = true;
}

void pgt_serial_putc(char c)
{
    if (!g_serial)
        serialInit();
    for (int i = 0; i < 100000 && !(inb(0x3FD) & 0x20); ++i)
        ;
    outb(0x3F8, (uint8_t)c);
}

int64_t write(int32_t fd, const char *data, int64_t n)
{
    (void)fd;
    for (int64_t i = 0; i < n; ++i)
        pgt_serial_putc(data[i]);
    return n;
}

int32_t fflush(void *stream)
{
    (void)stream;
    return 0;
}

_Noreturn void pgt_exit(int32_t code)
{
    (void)code;
    pgt_halt();
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s)
        while (n--)
            *d++ = *s++;
    else
        while (n--)
            d[n] = s[n];
    return dst;
}

void *memset(void *dst, int v, size_t n)
{
    uint8_t *d = dst;
    while (n--)
        *d++ = (uint8_t)v;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return (uint8_t)*a - (uint8_t)*b;
}

static const char *findStr(const char *s, const char *sub)
{
    size_t m = strlen(sub);
    if (m == 0)
        return s;
    for (; *s; ++s)
        if (memcmp(s, sub, m) == 0)
            return s;
    return 0;
}

#define SMALL_LIMIT 1024
#define SMALL_CLASSES (SMALL_LIMIT / 16)
#define LARGE_CLASSES 48

static uint8_t g_arena[PGT_KERNEL_HEAP_SIZE] __attribute__((aligned(4096)));
static uintptr_t g_base;
static size_t g_size;
static uintptr_t g_top;
static uint8_t *g_bits;
static void *g_free[SMALL_CLASSES + LARGE_CLASSES];
static int g_errno;

int *__errno_location(void)
{
    return &g_errno;
}

void pgt_heap_provide(void *base, size_t size)
{
    size_t bits = size >> 7;
    g_bits = base;
    memset(g_bits, 0, bits);
    g_base = ((uintptr_t)base + bits + 15) & ~(uintptr_t)15;
    g_size = (uintptr_t)base + size - g_base;
    g_top = g_base;
}

static void heapInit(void)
{
    if (!g_size)
        pgt_heap_provide(g_arena, sizeof g_arena);
}

static inline bool isManaged(const void *p)
{
    uintptr_t off = (uintptr_t)p - g_base;
    return g_size && off < g_size && !(off & 15) && ((g_bits[off >> 7] >> ((off >> 4) & 7)) & 1);
}

static size_t classSize(size_t total, int *cls)
{
    if (total <= SMALL_LIMIT)
    {
        size_t c = (total + 15) / 16;
        *cls = (int)c - 1;
        return c * 16;
    }
    int k = 64 - __builtin_clzll((unsigned long long)total - 1);
    *cls = SMALL_CLASSES + k;
    return (size_t)1 << k;
}

void *pgt_alloc_n(int64_t count, int64_t stride, void (*drop)(void *))
{
    size_t bytes;
    if (count < 0 || stride < 0 || __builtin_mul_overflow((size_t)count, (size_t)stride, &bytes) ||
        bytes > ((size_t)1 << 40))
        pgt_panic("out of memory");
    heapInit();
    size_t total = sizeof(Hdr) + (bytes ? bytes : 1);
    int cls;
    size_t sz = classSize(total, &cls);
    Hdr *h = g_free[cls];
    if (h)
    {
        g_free[cls] = *(void **)h;
        memset(h, 0, total);
    }
    else
    {
        if (sz > g_size - (g_top - g_base))
            pgt_panic("out of memory");
        h = (Hdr *)g_top;
        g_top += sz;
        memset(h, 0, sz);
    }
    h->rc = 1;
    h->drop = drop;
    h->count = count;
    h->stride = stride;
    void *p = h + 1;
    uintptr_t off = (uintptr_t)p - g_base;
    g_bits[off >> 7] |= (uint8_t)(1u << ((off >> 4) & 7));
    return p;
}

void *pgt_alloc(int64_t size, void (*drop)(void *))
{
    return pgt_alloc_n(1, size, drop);
}

static void heapFree(Hdr *h)
{
    void *p = h + 1;
    uintptr_t off = (uintptr_t)p - g_base;
    g_bits[off >> 7] &= (uint8_t) ~(1u << ((off >> 4) & 7));
    size_t bytes = (size_t)(h->count * h->stride);
    size_t total = sizeof(Hdr) + (bytes ? bytes : 1);
    int cls;
    classSize(total, &cls);
    *(void **)h = g_free[cls];
    g_free[cls] = h;
}

void pgt_retain(void *p)
{
    if (p && isManaged(p))
        ++((Hdr *)p - 1)->rc;
}

static Hdr **g_pending;
static size_t g_pendingLen, g_pendingCap;
static bool g_destroying;

static void destroyOne(Hdr *h)
{
    void *p = h + 1;
    if (h->drop)
        for (int64_t i = 0; i < h->count; ++i)
            h->drop((char *)p + i * h->stride);
    heapFree(h);
}

static void destroy(Hdr *h)
{
    if (g_destroying)
    {
        if (g_pendingLen == g_pendingCap)
        {
            size_t cap = g_pendingCap ? g_pendingCap * 2 : 256;
            Hdr **np = pgt_alloc_n((int64_t)cap, sizeof *np, 0);
            memcpy(np, g_pending, g_pendingLen * sizeof *np);
            g_pending = np;
            g_pendingCap = cap;
        }
        g_pending[g_pendingLen++] = h;
        return;
    }
    g_destroying = true;
    destroyOne(h);
    while (g_pendingLen)
        destroyOne(g_pending[--g_pendingLen]);
    g_destroying = false;
}

void pgt_release(void *p)
{
    if (!p || !isManaged(p))
        return;
    Hdr *h = (Hdr *)p - 1;
    if (--h->rc == 0)
        destroy(h);
}

void pgt_free(void *p)
{
    if (p && isManaged(p))
        destroy((Hdr *)p - 1);
}

void *malloc(size_t n)
{
    return pgt_alloc_n((int64_t)n, 1, 0);
}

void *calloc(size_t count, size_t size)
{
    return pgt_alloc_n((int64_t)count, (int64_t)size, 0);
}

void *realloc(void *p, size_t n)
{
    if (!p)
        return malloc(n);
    Hdr *h = (Hdr *)p - 1;
    size_t old = (size_t)(h->count * h->stride);
    void *q = malloc(n);
    memcpy(q, p, old < n ? old : n);
    pgt_free(p);
    return q;
}

void free(void *p)
{
    pgt_free(p);
}

void pgt_array_reserve(PgtArray *a, int64_t need, int64_t elemSize, void (*dropElem)(void *),
                       void (*retainElem)(void *))
{
    if (need <= a->cap)
    {
        pgt_retain(a->base);
        return;
    }
    int64_t cap = a->cap ? a->cap * 2 : 4;
    while (cap < need)
        cap *= 2;
    void *data = pgt_alloc_n(cap, elemSize, dropElem);
    if (a->len > 0)
    {
        memcpy(data, a->data, (size_t)(a->len * elemSize));
        if (retainElem)
            for (int64_t i = 0; i < a->len; ++i)
                retainElem((char *)data + i * elemSize);
    }
    a->data = data;
    a->base = data;
    a->cap = cap;
}

void pgt_array_make(PgtArray *out, int64_t len, int64_t cap, int64_t elemSize, void (*dropElem)(void *))
{
    if (len < 0)
        pgt_panic("make: negative length");
    if (cap < len)
        cap = len;
    out->data = cap ? pgt_alloc_n(cap, elemSize, dropElem) : 0;
    out->base = out->data;
    out->len = len;
    out->cap = cap;
}

static const char *S(const char *s)
{
    return s ? s : "";
}

static char *newString(size_t n)
{
    char *p = pgt_alloc_n((int64_t)n + 1, 1, 0);
    p[n] = '\0';
    return p;
}

static char *dupRange(const char *s, size_t n)
{
    char *p = newString(n);
    memcpy(p, s, n);
    return p;
}

const char *pgt_cstr_dup(const char *s)
{
    if (!s)
        return dupRange("", 0);
    return dupRange(s, strlen(s));
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

static const char *fromU64(uint64_t v, bool neg)
{
    char buf[32];
    int i = 32;
    do
    {
        buf[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    if (neg)
        buf[--i] = '-';
    return dupRange(buf + i, (size_t)(32 - i));
}

const char *pgt_str_int(int32_t v)
{
    return v < 0 ? fromU64((uint64_t)(-(int64_t)v), true) : fromU64((uint64_t)v, false);
}

const char *pgt_str_int64(int64_t v)
{
    return v < 0 ? fromU64(0 - (uint64_t)v, true) : fromU64((uint64_t)v, false);
}

const char *pgt_str_uint64(uint64_t v)
{
    return fromU64(v, false);
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
    const char *p = findStr(s, S(sub));
    return p ? (int32_t)(p - s) : -1;
}

const char *pgt_from_chars(const PgtArray *chars)
{
    return dupRange(chars->data ? chars->data : "", (size_t)chars->len);
}

void pgt_to_chars(PgtArray *out, const char *s)
{
    s = S(s);
    int64_t n = (int64_t)strlen(s);
    pgt_array_make(out, n, n, 1, 0);
    if (n)
        memcpy(out->data, s, (size_t)n);
}

_Noreturn void pgt_assert_fail(const char *dyn, const char *want)
{
    char buf[256];
    const char *parts[] = {"interface conversion: ", dyn, " is not ", want};
    size_t pos = 0;
    for (int i = 0; i < 4; ++i)
        for (const char *p = parts[i]; *p && pos < sizeof buf - 1; ++p)
            buf[pos++] = *p;
    buf[pos] = 0;
    pgt_panic(buf);
}

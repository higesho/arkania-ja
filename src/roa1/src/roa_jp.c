/*
 * Realms of Arkania 1 (Blade of Destiny) Japanese patch - runtime part.
 * Loaded into DOSBox.exe as winmm.dll proxy.
 *  - redirects fopen() of game files to patched copies built in %LOCALAPPDATA%\RoA1_JP
 *  - installs DOSBox callbacks that the patched game code calls for text rendering
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "resource.h"

void proxy_init(void);

/* ---------------------------------------------------------------- logging */
static char g_dir[MAX_PATH];
static FILE *g_log;
static int g_logon;
static void dbg(const char *fmt, ...)
{
    va_list ap;
    if (!g_logon) return;
    if (!g_log) {
        char p[MAX_PATH];
        wsprintfA(p, "%s\\log.txt", g_dir);
        g_log = fopen(p, "a");
        if (!g_log) return;
    }
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

/* ------------------------------------------------- DOSBox 0.74 internals */
/* addresses valid for the DOSBox.exe shipped with RoA1 Classic (Steam/GOG) */
#define A_CB_HANDLERS   0x74b440u
#define A_CB_ILLEGAL    0x416050u
#define A_REGS          0x74b1c0u
#define A_SEGPHYS       0x74b100u
#define A_TLB_READ      0x74b6b0u
#define A_TLB_WRITE     0xb4b6b0u
#define A_TLB_RH        0xf4b6b0u
#define A_TLB_WH        0x134b6b0u

typedef uint32_t (__cdecl *cb_fn)(void);
typedef uint32_t (__cdecl *ph_rb)(void *self, uint32_t a);
typedef void (__cdecl *ph_wb)(void *self, uint32_t a, uint32_t v);

static int verify_dosbox(void)
{
    static const uint8_t sig1[] = {0x81,0x3c,0x85,0x40,0xb4,0x74,0x00,0x50,0x60,0x41,0x00}; /* 0x4171fb */
    static const uint8_t sig2[] = {0x66,0xa3,0xc0,0xb1,0x74,0x00};                          /* 0x56271f */
    static const uint8_t sig3[] = {0x8b,0x04,0x95,0xb0,0xb6,0x74,0x00};                     /* 0x4ec5c2 */
    HMODULE h = GetModuleHandleA(NULL);
    if ((uintptr_t)h != 0x400000) return 0;
    if (memcmp((void*)0x4171fb, sig1, sizeof sig1)) return 0;
    if (memcmp((void*)0x56271f, sig2, sizeof sig2)) return 0;
    if (memcmp((void*)0x4ec5c2, sig3, sizeof sig3)) return 0;
    return 1;
}

static __inline uint8_t rb(uint32_t a)
{
    uint8_t *p = ((uint8_t**)A_TLB_READ)[a >> 12];
    void *h;
    if (p) return p[a];
    h = ((void**)A_TLB_RH)[a >> 12];
    return (uint8_t)((ph_rb)((*(void***)h)[2]))(h, a);
}
static __inline void wb(uint32_t a, uint8_t v)
{
    uint8_t *p = ((uint8_t**)A_TLB_WRITE)[a >> 12];
    void *h;
    if (p) { p[a] = v; return; }
    h = ((void**)A_TLB_WH)[a >> 12];
    ((ph_wb)((*(void***)h)[5]))(h, a, v);
}
static __inline uint16_t rw(uint32_t a) { return (uint16_t)(rb(a) | (rb(a + 1) << 8)); }
static __inline void ww(uint32_t a, uint16_t v) { wb(a, (uint8_t)v); wb(a + 1, (uint8_t)(v >> 8)); }
static __inline int16_t rws(uint32_t a) { return (int16_t)rw(a); }
#define REG32(i) (((uint32_t*)A_REGS)[i])
#define SET_AX(v) (*(uint16_t*)&REG32(0) = (uint16_t)(v))
#define R_SP ((uint16_t)REG32(4))
#define R_BP ((uint16_t)REG32(5))
#define PHYS_SS (((uint32_t*)A_SEGPHYS)[2])
#define PHYS_DS (((uint32_t*)A_SEGPHYS)[3])
static __inline uint32_t farptr(uint32_t a) /* read far pointer stored at linear a */
{
    return ((uint32_t)rw(a + 2) << 4) + rw(a);
}

/* ------------------------------------------------------- patch blob */
#pragma pack(push, 1)
typedef struct { char magic[4]; uint32_t version; uint32_t nsect; } blob_hdr;
typedef struct { uint32_t type; uint32_t len; } sect_hdr;
#pragma pack(pop)

static const uint8_t *g_blob;
static uint32_t g_blob_len, g_blob_crc;

/* encoding / font */
enum { C_ASCII = 0, C_LEAD = 1, C_WIDE = 2 };
static uint8_t g_cls[256];
static uint16_t *g_glyph_index;  /* 65536 entries -> glyph number+1 */
static const uint8_t *g_glyphs;  /* 8 bytes each */
static const uint8_t *g_gflags;  /* per glyph: 1 = no line start, 2 = no line end */
static int g_line_h = 7;
static int g_cb_base = 0x7c;
static int g_enabled;

/* game data segment offsets: profile 0 = BLADEM.EXE (English 3.x), profile 1 = GEN.EXE */
typedef struct {
    uint16_t char_tab, ntab, defw, colon, print_char, font_ptr, screen_ptr, textcolor, color_bg, color_fg,
             centered, posx, posy, maxlen, indent_w, indent_h, tabpos;
} gamevars;
static const gamevars PROFILES[2] = {
    { 0xaa8f, 75, 5, 1, 0xe3cc, 0xd1b5, 0xd1ef, 0xd1b9, 0xd1bb, 0xd1bd,
      0xd1c5, 0xd1cd, 0xd1cb, 0xd1c9, 0xe3cf, 0xe3cd, 0xd207 },
    { 0x1ac3, 74, 6, 0, 0,      0x46b9, 0x4707, 0x46bd, 0x46bf, 0x46c1,
      0x46c9, 0x46d1, 0x46cf, 0x46cd, 0,      0,      0x4723 },
};
static gamevars GV;
static __inline int gvar(uint16_t off) { return off ? (int16_t)rw(PHYS_DS + off) : 0; }

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xffffffffu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static int load_blob(HMODULE self)
{
    HRSRC r = FindResourceA(self, MAKEINTRESOURCEA(IDR_PATCH), (LPCSTR)RT_RCDATA);
    HGLOBAL g;
    const blob_hdr *h;
    const uint8_t *p;
    uint32_t i;
    if (!r) return 0;
    g = LoadResource(self, r);
    g_blob = (const uint8_t*)LockResource(g);
    g_blob_len = SizeofResource(self, r);
    if (!g_blob || g_blob_len < sizeof(blob_hdr)) return 0;
    h = (const blob_hdr*)g_blob;
    if (memcmp(h->magic, "RJP1", 4)) return 0;
    g_blob_crc = crc32(g_blob, g_blob_len);
    p = g_blob + sizeof(blob_hdr);
    for (i = 0; i < h->nsect; i++) {
        const sect_hdr *s = (const sect_hdr*)p;
        const uint8_t *d = p + sizeof(sect_hdr);
        if (s->type == 'E') {            /* encoding: cls[256] */
            memcpy(g_cls, d, 256);
        } else if (s->type == 'F') {     /* font: u32 n; n*u16 code; n*8 bitmap; n flags */
            uint32_t n = *(const uint32_t*)d, k;
            const uint16_t *codes = (const uint16_t*)(d + 4);
            g_glyphs = d + 4 + n * 2;
            g_gflags = g_glyphs + n * 8;
            g_glyph_index = (uint16_t*)calloc(65536, 2);
            for (k = 0; k < n; k++) g_glyph_index[codes[k]] = (uint16_t)(k + 1);
        } else if (s->type == 'P') {     /* params */
            const uint32_t *v = (const uint32_t*)d;
            g_line_h = (int)v[0];
            g_cb_base = (int)v[1];
            g_logon = (int)v[2];
        }
        p = d + s->len;
    }
    return 1;
}

/* iterate file-patch sections for a given target name */
static const uint8_t *find_sect(uint32_t type, const char *name, uint32_t *len)
{
    const blob_hdr *h = (const blob_hdr*)g_blob;
    const uint8_t *p = g_blob + sizeof(blob_hdr);
    uint32_t i;
    for (i = 0; i < h->nsect; i++) {
        const sect_hdr *s = (const sect_hdr*)p;
        const uint8_t *d = p + sizeof(sect_hdr);
        if (s->type == type && !_stricmp((const char*)d, name)) {
            *len = s->len - 16;
            return d + 16;
        }
        p = d + s->len;
    }
    return NULL;
}

/* ------------------------------------------------ text engine (host side) */
static int glyph_of(uint16_t code) { return g_glyph_index ? g_glyph_index[code] : 0; }
static int gflags(uint16_t code) { int g = glyph_of(code); return g ? g_gflags[g - 1] : 0; }

static int lookup_width(uint8_t c, int *width)
{
    uint32_t t = PHYS_DS + GV.char_tab;
    int i;
    if (g_cls[c] == C_LEAD) { *width = 0; return 0; }
    if (g_cls[c] == C_WIDE) { *width = 8; return 0; }
    for (i = 0; i != GV.ntab * 3; i += 3) {
        if (rb(t + i) == c) {
            *width = rb(t + i + 2);
            return rb(t + i + 1);
        }
    }
    if (c == '~' || c == 0xf0 || c == 0xf1 || c == 0xf2 || c == 0xf3) { *width = 0; return 0; }
    *width = GV.defw;
    return 0;
}

static void draw_cell(int x, int y, const uint8_t *bits, int w, int bits_in_guest, uint32_t gaddr)
{
    uint32_t sp = PHYS_DS + GV.screen_ptr;
    uint16_t off = rw(sp), seg = rw(sp + 2);
    uint16_t tc = rw(PHYS_DS + GV.textcolor);
    uint8_t fg = rb(PHYS_DS + GV.color_fg + tc * 2);
    uint8_t bg = rb(PHYS_DS + GV.color_bg);
    uint16_t o = (uint16_t)(off + y * 320 + x);
    int r, c;
    for (r = 0; r < 7; r++) {
        uint8_t row = bits_in_guest ? rb(gaddr + r) : bits[r];
        for (c = 0; c < w; c++) {
            uint16_t oo = (uint16_t)(o + r * 320 + c);
            wb(((uint32_t)seg << 4) + oo, (row & (0x80 >> c)) ? fg : bg);
        }
    }
}

static uint8_t g_lead;

static int print_char(uint8_t c, int x, int y)
{
    int w, idx;
    if (GV.print_char) wb(PHYS_DS + GV.print_char, c);
    if (g_cls[c] == C_LEAD) { g_lead = c; return 0; }
    if (g_cls[c] == C_WIDE) {
        uint16_t code = g_lead ? (uint16_t)((g_lead << 8) | c) : c;
        int g = glyph_of(code);
        static const uint8_t tofu[8] = {0xfe,0x82,0x82,0x82,0x82,0x82,0xfe,0};
        g_lead = 0;
        draw_cell(x, y, g ? g_glyphs + (g - 1) * 8 : tofu, 8, 0, 0);
        return 8;
    }
    g_lead = 0;
    idx = lookup_width(c, &w);
    {
        uint32_t f = farptr(PHYS_DS + GV.font_ptr) + idx * 8;
        if (c == ':' && GV.colon) f++;
        draw_cell(x, y, NULL, w, 1, f);
    }
    return w;
}

/* ---- line breaking ---- */
#define MAXBRK 128
typedef struct { uint32_t addr, hash; int n; uint16_t off[MAXBRK]; } brkent;
static brkent g_brk[64];
static int g_brk_next;

static uint32_t str_hash(uint32_t s)
{
    uint32_t h = 2166136261u, i;
    uint8_t c;
    for (i = 0; i < 8192 && (c = rb(s + i)); i++) h = (h ^ c) * 16777619u;
    return h;
}

/* char at p: returns byte length, code, jp flag */
static int char_at(uint32_t p, uint16_t *code, int *jp)
{
    uint8_t c = rb(p);
    if (g_cls[c] == C_LEAD) {
        uint8_t t = rb(p + 1);
        if (g_cls[t] == C_WIDE) { *code = (uint16_t)((c << 8) | t); *jp = 1; return 2; }
        *code = c; *jp = 1; return 1;
    }
    *code = c;
    *jp = (g_cls[c] == C_WIDE);
    return 1;
}

static int ascii_no_start(uint8_t c) { return c && strchr(",.!?)]:;'%>", c) != NULL; }
static int ascii_no_end(uint8_t c) { return c && strchr("([<", c) != NULL; }

/* Wrap string at s. modify: write CR into spaces like the original game.
 * Returns number of lines; fills brk with offsets where a new line begins without CR. */
static int wrap_string(uint32_t s, int modify, uint16_t *brk, int *nbrk)
{
    int maxlen = rws(PHYS_DS + GV.maxlen);
    int iw = gvar(GV.indent_w), ih = gvar(GV.indent_h);
    int lines = 0, width = 0, prev_jp = 0, prev_noend = 0, prev_alnum = 0;
    uint32_t p, line_start, last_ws = 0, last_jp = 0;
    int have_ws = 0, have_jp = 0;
    *nbrk = 0;
    if (iw) maxlen -= iw;
    p = line_start = s;
    for (;;) {
        uint16_t code;
        int jp, len, w = 0, wtmp;
        uint8_t c = rb(p);
        if (!c) break;
        len = char_at(p, &code, &jp);
        if (p != line_start) {
            int nostart = jp ? (gflags(code) & 1) : ascii_no_start(c);
            int cur_alnum = !jp && ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'));
            /* keep ASCII words/numbers (hero names, amounts) glued to adjacent Japanese */
            if ((jp || prev_jp) && !nostart && !prev_noend && !prev_alnum && !cur_alnum) { last_jp = p; have_jp = 1; }
        }
        if (c == '@' || c == 0x0d) {
            if (++lines == ih) maxlen += iw;
            p += 1; line_start = p; width = 0; have_ws = have_jp = 0; prev_jp = 0; prev_noend = 0; prev_alnum = 0;
            continue;
        }
        lookup_width(c, &wtmp); w += wtmp;
        if (len == 2) { lookup_width(rb(p + 1), &wtmp); w += wtmp; }
        width += w;
        if (width >= maxlen) {
            if (have_ws && (!have_jp || last_ws + 1 >= last_jp)) {
                if (modify) wb(last_ws, 0x0d);
                else if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(last_ws + 1 - s);
                line_start = last_ws + 1;
            } else if (have_jp) {
                if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(last_jp - s);
                line_start = last_jp;
            } else if (jp && p != line_start) {
                if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(p - s);
                line_start = p;
            } else {
                if (modify) { wb(p, 0x0d); line_start = p + 1; }
                else { line_start = p + len; if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(line_start - s); }
            }
            if (++lines == ih) maxlen += iw;
            p = line_start; width = 0; have_ws = have_jp = 0; prev_jp = 0; prev_noend = 0; prev_alnum = 0;
            continue;
        }
        if (c == ' ' && p > line_start) { last_ws = p; have_ws = 1; }
        prev_jp = jp;
        prev_alnum = !jp && ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'));
        prev_noend = jp ? (gflags(code) & 2) : ascii_no_end(c);
        p += len;
    }
    return lines + 1;
}

static int has_jp(uint32_t s)
{
    uint32_t i; uint8_t c;
    for (i = 0; i < 8192 && (c = rb(s + i)); i++) if (g_cls[c] != C_ASCII) return 1;
    return 0;
}

static uint32_t __cdecl cb_count_lines(void)
{
    uint32_t sp = PHYS_SS + R_SP;
    uint16_t off = rw(sp + 4), seg = rw(sp + 6);
    uint32_t s = ((uint32_t)seg << 4) + off, i;
    brkent *e;
    int lines;
    uint8_t c;
    if (!off && !seg) { SET_AX(0); return 0; }
    for (i = 0; i < 8192 && (c = rb(s + i)); i++)
        if (c == 0x0d || c == 0x0a) wb(s + i, ' ');
    e = &g_brk[g_brk_next];
    lines = wrap_string(s, 1, e->off, &e->n);
    e->addr = s;
    e->hash = str_hash(s);
    g_brk_next = (g_brk_next + 1) & 63;
    SET_AX(lines);
    return 0;
}

static int measure_line(uint32_t p, const uint16_t *brk, int nbrk, uint32_t s)
{
    int sum = 0, w, k;
    uint32_t p0 = p;
    uint8_t c;
    for (; (c = rb(p)) && c != '@' && c != 0x0d; p++) {
        if (p != p0) for (k = 0; k < nbrk; k++) if (s + brk[k] == p) return sum;
        lookup_width(c, &w);
        sum += w;
    }
    return sum;
}
static int centered_x(uint32_t p, int x, int maxlen, const uint16_t *brk, int nbrk, uint32_t s)
{
    int v = (int16_t)(maxlen - measure_line(p, brk, nbrk, s));
    v >>= 1;
    return (int16_t)(v + x);
}

static uint32_t __cdecl cb_print_string(void)
{
    uint32_t bp = PHYS_SS + R_BP;
    uint16_t off = rw(bp + 6), seg = rw(bp + 8);
    int x = rws(bp + 10), y = rws(bp + 12);
    uint32_t s = ((uint32_t)seg << 4) + off, p;
    const uint16_t *brk = NULL;
    uint16_t tmp[MAXBRK];
    int nbrk = 0, l1 = 0, l3, k;
    uint32_t h = str_hash(s);
    uint32_t ds = PHYS_DS;
    for (k = 0; k < 64; k++) {
        brkent *e = &g_brk[(g_brk_next - 1 - k) & 63];
        if (e->addr == s && e->hash == h) { brk = e->off; nbrk = e->n; break; }
    }
    if (!brk && has_jp(s)) {
        wrap_string(s, 0, tmp, &nbrk);
        brk = tmp;
    }
    g_lead = 0;
    if (rws(ds + GV.centered) == 1)
        x = centered_x(s, x, rws(ds + GV.maxlen), brk, nbrk, s);
    else if (gvar(GV.indent_w))
        x += gvar(GV.indent_w);
    l3 = x;
    for (p = s;; ) {
        uint8_t c = rb(p);
        int isbrk = 0;
        if (!c) break;
        for (k = 0; k < nbrk; k++) if (s + brk[k] == p && p != s) { isbrk = 1; break; }
        if (isbrk || c == 0x0d || c == 0x40) {
            if (GV.indent_h && ++l1 == gvar(GV.indent_h)) {
                ww(ds + GV.maxlen, (uint16_t)(rws(ds + GV.maxlen) + gvar(GV.indent_w)));
                l3 -= gvar(GV.indent_w);
            }
            y += g_line_h;
            if (rws(ds + GV.centered) == 1)
                x = centered_x(isbrk ? p : p + 1, rws(ds + GV.posx), rws(ds + GV.maxlen), brk, nbrk, s);
            else
                x = l3;
            g_lead = 0;
            if (!isbrk) { p++; continue; }
        }
        p++;
        if (c == '~') {
            int t;
            for (t = 0; t < 7; t++) {
                int tp = rws(ds + GV.tabpos + t * 2);
                if (x < tp) { x = tp; break; }
            }
        } else if (c >= 0xf0 && c <= 0xf3) {
            ww(ds + GV.textcolor, (uint16_t)(c - 0xf0));
        } else {
            if (c == '<') c = '>';
            x += print_char(c, x, y);
        }
    }
    return 0;
}

static uint32_t __cdecl cb_print_char(void)
{
    uint32_t sp = PHYS_SS + R_SP;
    uint8_t c = rb(sp + 4);
    int x = rws(sp + 6), y = rws(sp + 8);
    SET_AX(print_char(c, x, y));
    return 0;
}

static uint32_t __cdecl cb_lookup_width(void)
{
    uint32_t sp = PHYS_SS + R_SP;
    uint8_t c = rb(sp + 4);
    uint32_t p = ((uint32_t)rw(sp + 8) << 4) + rw(sp + 6);
    int w, idx = lookup_width(c, &w);
    ww(p, (uint16_t)w);
    SET_AX(idx);
    return 0;
}

static int g_ncalls;
#define WRAP(name, prof) static uint32_t __cdecl name##_##prof(void) { uint32_t r; GV = PROFILES[prof]; \
    if (g_ncalls < 40) dbg("cb %s %d sp=%x bp=%x\n", #name, prof, R_SP, R_BP); \
    r = name(); if (g_ncalls++ < 40) dbg("  -> ax=%x r=%u\n", (uint16_t)REG32(0), r); return r; }
WRAP(cb_count_lines, 0) WRAP(cb_print_string, 0) WRAP(cb_print_char, 0) WRAP(cb_lookup_width, 0)
WRAP(cb_count_lines, 1) WRAP(cb_print_string, 1) WRAP(cb_print_char, 1) WRAP(cb_lookup_width, 1)

static int g_cb_installed;
static int install_callbacks(void)
{
    uint32_t *tab = (uint32_t*)A_CB_HANDLERS;
    int b = g_cb_base, i;
    if (g_cb_installed) return 1;
    for (i = 0; i < 8; i++)
        if (tab[b + i] != A_CB_ILLEGAL) { dbg("callback slot %x busy (%x)\n", b + i, tab[b + i]); return 0; }
    /* slots base+0..3: GEN.EXE (profile 1), base+4..7: BLADEM.EXE (profile 0) */
    tab[b + 0] = (uint32_t)cb_count_lines_1;
    tab[b + 1] = (uint32_t)cb_print_string_1;
    tab[b + 2] = (uint32_t)cb_print_char_1;
    tab[b + 3] = (uint32_t)cb_lookup_width_1;
    tab[b + 4] = (uint32_t)cb_count_lines_0;
    tab[b + 5] = (uint32_t)cb_print_string_0;
    tab[b + 6] = (uint32_t)cb_print_char_0;
    tab[b + 7] = (uint32_t)cb_lookup_width_0;
    g_cb_installed = 1;
    dbg("callbacks installed at %x\n", b);
    return 1;
}

/* ----------------------------------------------------- file building */
typedef FILE *(__cdecl *fopen_t)(const char *, const char *);
static fopen_t real_fopen;

static uint8_t *read_all(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t*)malloc(n ? n : 1);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    *len = (uint32_t)n;
    return b;
}
static int write_all(const char *path, const uint8_t *b, uint32_t n)
{
    char tmp[MAX_PATH];
    FILE *f;
    wsprintfA(tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) return 0;
    if (fwrite(b, 1, n, f) != n) { fclose(f); return 0; }
    fclose(f);
    DeleteFileA(path);
    return MoveFileA(tmp, path);
}

/* 'X' section: u32 orig_size, u32 orig_crc, then records {u32 off, u32 len, bytes} */
static uint8_t *build_exe(const uint8_t *orig, uint32_t olen, const uint8_t *sec, uint32_t slen, uint32_t *outlen)
{
    uint32_t osz = ((const uint32_t*)sec)[0], ocrc = ((const uint32_t*)sec)[1];
    const uint8_t *p = sec + 8, *e = sec + slen;
    uint8_t *out;
    if (osz != olen || crc32(orig, olen) != ocrc) { dbg("exe mismatch\n"); return NULL; }
    out = (uint8_t*)malloc(olen);
    memcpy(out, orig, olen);
    while (p < e) {
        uint32_t off = ((const uint32_t*)p)[0], n = ((const uint32_t*)p)[1];
        p += 8;
        if (off + n > olen) { free(out); return NULL; }
        memcpy(out + off, p, n);
        p += n;
    }
    *outlen = olen;
    return out;
}

/* 'D' section: u32 orig_size, u32 orig_crc, u32 nrep, then per replaced entry:
 *   u32 index, u32 keep, u32 npatch, npatch * {u32 off, u32 len, bytes}, u32 len, bytes
 * The new entry = first `keep` bytes of the original entry (patched) + the new bytes,
 * so no original game data is shipped inside the DLL. */
typedef struct { const uint8_t *rec; uint32_t keep, npatch; const uint8_t *patches, *body; uint32_t blen; } datrep;
static uint8_t *build_dat(const uint8_t *orig, uint32_t olen, const uint8_t *sec, uint32_t slen, uint32_t *outlen)
{
    uint32_t osz = ((const uint32_t*)sec)[0], ocrc = ((const uint32_t*)sec)[1], nrep = ((const uint32_t*)sec)[2];
    const uint32_t *offs = (const uint32_t*)orig;
    uint32_t n = offs[0] / 4, i, k, total, pos;
    datrep *rep;
    const uint8_t *p = sec + 12;
    uint8_t *out;
    (void)slen;
    if (osz != olen || crc32(orig, olen) != ocrc) { dbg("dat mismatch\n"); return NULL; }
    rep = (datrep*)calloc(n, sizeof(datrep));
    for (i = 0; i < nrep; i++) {
        uint32_t idx = ((const uint32_t*)p)[0], keep = ((const uint32_t*)p)[1], np = ((const uint32_t*)p)[2];
        const uint8_t *pp = p + 12, *q = pp;
        for (k = 0; k < np; k++) q += 8 + ((const uint32_t*)q)[1];
        if (idx < n) {
            rep[idx].rec = p; rep[idx].keep = keep; rep[idx].npatch = np; rep[idx].patches = pp;
            rep[idx].blen = *(const uint32_t*)q; rep[idx].body = q + 4;
        }
        p = q + 4 + *(const uint32_t*)q;
    }
    total = n * 4;
    for (i = 0; i < n; i++) {
        uint32_t s = offs[i], e2 = olen, j;
        if (!s) continue;
        for (j = i + 1; j < n; j++) if (offs[j]) { e2 = offs[j]; break; }
        total += rep[i].rec ? rep[i].keep + rep[i].blen : e2 - s;
    }
    out = (uint8_t*)malloc(total);
    pos = n * 4;
    for (i = 0; i < n; i++) {
        uint32_t s = offs[i], e2 = olen, j, l;
        if (!s) { ((uint32_t*)out)[i] = 0; continue; }
        for (j = i + 1; j < n; j++) if (offs[j]) { e2 = offs[j]; break; }
        ((uint32_t*)out)[i] = pos;
        if (rep[i].rec) {
            const uint8_t *q = rep[i].patches;
            if (rep[i].keep > e2 - s) { free(rep); free(out); return NULL; }
            memcpy(out + pos, orig + s, rep[i].keep);
            for (k = 0; k < rep[i].npatch; k++) {
                uint32_t o = ((const uint32_t*)q)[0], pl = ((const uint32_t*)q)[1];
                if (o + pl <= rep[i].keep) memcpy(out + pos + o, q + 8, pl);
                q += 8 + pl;
            }
            memcpy(out + pos + rep[i].keep, rep[i].body, rep[i].blen);
            l = rep[i].keep + rep[i].blen;
        } else { l = e2 - s; memcpy(out + pos, orig + s, l); }
        pos += l;
    }
    free(rep);
    *outlen = total;
    return out;
}

/* 'N' section (named directory archive like DSAGEN.DAT): u32 orig_size, u32 orig_crc, u32 nrep,
 * then {char name[16], u32 len, bytes} */
static uint8_t *build_named(const uint8_t *orig, uint32_t olen, const uint8_t *sec, uint32_t slen, uint32_t *outlen)
{
    uint32_t osz = ((const uint32_t*)sec)[0], ocrc = ((const uint32_t*)sec)[1], nrep = ((const uint32_t*)sec)[2];
    uint32_t dirsize = *(const uint32_t*)(orig + 12), n = 0, i, k, total, pos;
    const uint8_t *p;
    uint8_t *out;
    (void)slen;
    if (osz != olen || crc32(orig, olen) != ocrc) { dbg("named dat mismatch\n"); return NULL; }
    while (n * 16 < dirsize && orig[n * 16]) n++;
    total = dirsize;
    for (i = 0; i < n; i++) {
        uint32_t s0 = *(const uint32_t*)(orig + i * 16 + 12), e0 = i + 1 < n ? *(const uint32_t*)(orig + i * 16 + 28) : olen;
        uint32_t l = e0 - s0;
        for (k = 0, p = sec + 12; k < nrep; k++) {
            uint32_t rl = *(const uint32_t*)(p + 16);
            if (!_strnicmp((const char*)p, (const char*)orig + i * 16, 12)) l = rl;
            p += 20 + rl;
        }
        total += l;
    }
    out = (uint8_t*)malloc(total);
    memcpy(out, orig, dirsize);
    pos = dirsize;
    for (i = 0; i < n; i++) {
        uint32_t s0 = *(const uint32_t*)(orig + i * 16 + 12), e0 = i + 1 < n ? *(const uint32_t*)(orig + i * 16 + 28) : olen;
        const uint8_t *src = orig + s0;
        uint32_t l = e0 - s0;
        for (k = 0, p = sec + 12; k < nrep; k++) {
            uint32_t rl = *(const uint32_t*)(p + 16);
            if (!_strnicmp((const char*)p, (const char*)orig + i * 16, 12)) { src = p + 20; l = rl; }
            p += 20 + rl;
        }
        *(uint32_t*)(out + i * 16 + 12) = pos;
        memcpy(out + pos, src, l);
        pos += l;
    }
    *outlen = total;
    return out;
}

static const char *base_name(const char *p)
{
    const char *b = p, *q;
    for (q = p; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
    return b;
}

static CRITICAL_SECTION g_cs;

/* returns path of patched copy or NULL */
static const char *patched_path(const char *orig_path, char *buf)
{
    const char *bn = base_name(orig_path);
    uint32_t slen, olen, nlen;
    const uint8_t *sx, *sd;
    uint8_t *orig, *out = NULL;
    if (!g_enabled) return NULL;
    sx = find_sect('X', bn, &slen);
    sd = sx ? NULL : find_sect('D', bn, &slen);
    {
        uint32_t nl;
        const uint8_t *sn = (sx || sd) ? NULL : find_sect('N', bn, &nl);
        if (sn) { sd = sn; slen = nl; }
        if (!sx && !sd) return NULL;
        wsprintfA(buf, "%s\\%08X_%s", g_dir, g_blob_crc, bn);
        if (GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES) return buf;
        orig = read_all(orig_path, &olen);
        if (!orig) return NULL;
        out = sx ? build_exe(orig, olen, sx, slen, &nlen) : sn ? build_named(orig, olen, sd, slen, &nlen) : build_dat(orig, olen, sd, slen, &nlen);
        free(orig);
        if (!out) return NULL;
        if (!write_all(buf, out, nlen)) { free(out); return NULL; }
        free(out);
        dbg("built %s\n", buf);
        return buf;
    }
}

static FILE *__cdecl hook_fopen(const char *name, const char *mode)
{
    char buf[MAX_PATH];
    const char *pp;
    FILE *f;
    if (!name || !g_enabled) return real_fopen(name, mode);
    EnterCriticalSection(&g_cs);
    pp = patched_path(name, buf);
    if (pp && !install_callbacks()) pp = NULL;
    LeaveCriticalSection(&g_cs);
    if (pp) {
        f = real_fopen(pp, mode);
        if (f) { dbg("redirect %s -> %s (%s)\n", name, pp, mode); return f; }
    }
    return real_fopen(name, mode);
}

static int hook_iat(void)
{
    uint8_t *base = (uint8_t*)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dh = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS *nh = (IMAGE_NT_HEADERS*)(base + dh->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *id = (IMAGE_IMPORT_DESCRIPTOR*)(base + dd->VirtualAddress);
    int done = 0;
    for (; id->Name; id++) {
        const char *dll = (const char*)(base + id->Name);
        IMAGE_THUNK_DATA *ot, *ft;
        if (_stricmp(dll, "msvcrt.dll")) continue;
        ot = (IMAGE_THUNK_DATA*)(base + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk));
        ft = (IMAGE_THUNK_DATA*)(base + id->FirstThunk);
        for (; ot->u1.AddressOfData; ot++, ft++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            DWORD old;
            if (ot->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            ibn = (IMAGE_IMPORT_BY_NAME*)(base + ot->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, "fopen")) continue;
            VirtualProtect(&ft->u1.Function, 4, PAGE_READWRITE, &old);
            ft->u1.Function = (DWORD)(uintptr_t)hook_fopen;
            VirtualProtect(&ft->u1.Function, 4, old, &old);
            done = 1;
        }
    }
    return done;
}

static void init(HMODULE self)
{
    char la[MAX_PATH];
    HMODULE crt;
    InitializeCriticalSection(&g_cs);
    if (!GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH)) return;
    wsprintfA(g_dir, "%s\\RoA1_JP", la);
    CreateDirectoryA(g_dir, NULL);
    if (!verify_dosbox()) return;
    if (!load_blob(self)) return;
    crt = GetModuleHandleA("msvcrt.dll");
    if (!crt) return;
    real_fopen = (fopen_t)GetProcAddress(crt, "fopen");
    if (!real_fopen) return;
    if (!hook_iat()) return;
    g_enabled = 1;
    dbg("RoA1 JP patch active, blob %08X\n", g_blob_crc);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        proxy_init();
        init(h);
    }
    return TRUE;
}

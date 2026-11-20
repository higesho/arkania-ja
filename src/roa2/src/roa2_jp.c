/*
 * Realms of Arkania 2 (Star Trail) Japanese patch - runtime part.
 * Loaded into DOSBox.exe (0.74, Steam/GOG build) as a winmm.dll proxy.
 *  - redirects fopen() of STAR.EXE / STAR.DAT to patched copies built in %LOCALAPPDATA%\RoA2_JP
 *  - the patched STAR.EXE calls DOSBox callbacks for its text routines; this DLL then
 *    measures, wraps and draws Japanese (Misaki 8x8 font) itself.
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
#define A_CB_HANDLERS   0x74b440u
#define A_CB_ILLEGAL    0x416050u
#define A_REGS          0x74b1c0u
#define A_SEGPHYS       0x74b100u
#define A_TLB_READ      0x74b6b0u
#define A_TLB_WRITE     0xb4b6b0u
#define A_TLB_RH        0xf4b6b0u
#define A_TLB_WH        0x134b6b0u

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
static __inline uint32_t farptr(uint32_t a) { return ((uint32_t)rw(a + 2) << 4) + rw(a); }

/* ------------------------------------------------------- patch blob */
#pragma pack(push, 1)
typedef struct { char magic[4]; uint32_t version; uint32_t nsect; } blob_hdr;
typedef struct { uint32_t type; uint32_t len; } sect_hdr;
#pragma pack(pop)

static const uint8_t *g_blob;
static uint32_t g_blob_len, g_blob_crc;

enum { C_ASCII = 0, C_LEAD = 1, C_WIDE = 2 };
static uint8_t g_cls[256];
static uint16_t *g_glyph_index;  /* 65536 entries -> glyph number+1 */
static const uint8_t *g_glyphs;  /* 8 bytes each (rows, bit7 = left) */
static const uint8_t *g_gflags;  /* per glyph: 1 = no line start, 2 = no line end */
static int g_cb_base = 0x70;
static int g_enabled;

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
    if (memcmp(h->magic, "RJP2", 4)) return 0;
    g_blob_crc = crc32(g_blob, g_blob_len);
    p = g_blob + sizeof(blob_hdr);
    for (i = 0; i < h->nsect; i++) {
        const sect_hdr *s = (const sect_hdr*)p;
        const uint8_t *d = p + sizeof(sect_hdr);
        if (s->type == 'E') {
            memcpy(g_cls, d, 256);
        } else if (s->type == 'F') {     /* font: u32 n; n*u16 code; n*8 bitmap; n flags */
            uint32_t n = *(const uint32_t*)d, k;
            const uint16_t *codes = (const uint16_t*)(d + 4);
            g_glyphs = d + 4 + n * 2;
            g_gflags = g_glyphs + n * 8;
            g_glyph_index = (uint16_t*)calloc(65536, 2);
            for (k = 0; k < n; k++) g_glyph_index[codes[k]] = (uint16_t)(k + 1);
        } else if (s->type == 'P') {
            const uint32_t *v = (const uint32_t*)d;
            g_cb_base = (int)v[0];
            g_logon = (int)v[1];
        }
        p = d + s->len;
    }
    return 1;
}

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

/* ----------------------------------------------- text context (guest object) */
/* far pointer `this` of the game's text renderer; field offsets */
#define T_NVF      0x04
#define T_TAB      0x08
#define T_SURF     0x10
#define T_GH       0x16
#define T_CURX     0x18
#define T_LINEH    0x1a
#define T_NGLYPH   0x1c
#define T_TRANSP   0x1d
#define T_BG       0x1e
#define T_COLIDX   0x29
#define T_HILITE   0x2a
#define T_INDW     0x2d
#define T_INDL     0x2f
#define T_CENTER   0x31
#define T_MAXW     0x33
#define T_POSX     0x35
#define T_TABS     0x3b

static int glyph_of(uint16_t code) { return g_glyph_index ? g_glyph_index[code] : 0; }
static int gflags(uint16_t code) { int g = glyph_of(code); return g ? g_gflags[g - 1] : 0; }
static int big_font(uint32_t t) { return rws(t + T_GH) >= 14; }
static int jp_w(uint32_t t) { return big_font(t) ? 15 : 7; }   /* glyph width without the 1px gap */

/* original get_width: returns glyph index, *w = width (without gap) */
static int orig_width(uint32_t t, uint8_t c, int *w)
{
    uint32_t e = farptr(t + T_TAB) + c * 4;
    uint32_t g = farptr(e);
    if (rw(e) || rw(e + 2)) {
        *w = rb(g + 3);
        return (int8_t)rb(g + 2);
    }
    if (c == '~' || (c >= 0xf0 && c <= 0xf5)) { *w = 0; return 0; }
    *w = 5;
    return 0;
}

/* JP-aware width of one byte: lead bytes are -1 so that callers adding the 1px gap get 0 */
static int byte_width(uint32_t t, uint8_t c, int *w)
{
    if (g_cls[c] == C_LEAD) { *w = -1; return 0; }
    if (g_cls[c] == C_WIDE) { *w = jp_w(t); return 0; }
    return orig_width(t, c, w);
}

static void put_px(uint32_t surf, int x, int y, uint8_t col)
{
    uint32_t seg = surf >> 4, off = surf & 15;   /* surf is linear; keep 16-bit wrap like the game */
    (void)seg; (void)off;
    wb(surf + (uint32_t)(y * 320 + x), col);
}

static void colors(uint32_t t, uint8_t *fg, uint8_t *bg, int *transp)
{
    uint8_t idx = rb(t + T_COLIDX);
    *fg = rb(t + T_BG + idx + 1);
    *bg = rb(t + T_BG);
    *transp = rb(t + T_TRANSP) != 0;
}

static void draw_jp(uint32_t t, uint16_t code, int x, int y)
{
    static const uint8_t tofu[8] = {0, 0xfe,0x82,0x82,0x82,0x82,0x82,0xfe};
    int g = glyph_of(code), r, c, gh = rws(t + T_GH), scale = big_font(t) ? 2 : 1;
    const uint8_t *bits = g ? g_glyphs + (g - 1) * 8 : tofu;
    uint32_t surf = farptr(t + T_SURF);
    uint8_t fg, bg;
    int transp, top;
    colors(t, &fg, &bg, &transp);
    /* Misaki glyph rows 0..6 are placed on cell rows 1..7 (Latin caps use rows 1..7) */
    top = scale == 1 ? 1 : (gh - 16 > 0 ? (gh - 16) / 2 : 0);
    for (r = 0; r < gh; r++) {
        int gr = (r - top) / scale;
        uint8_t row = (r - top >= 0 && gr < 8) ? bits[gr] : 0;
        for (c = 0; c < 7 * scale; c++) {
            int on = (row >> (7 - c / scale)) & 1;
            if (on) put_px(surf, x + c, y + r, fg);
            else if (!transp) put_px(surf, x + c, y + r, bg);
        }
    }
}

static void draw_orig(uint32_t t, uint8_t c, int w, int x, int y)
{
    uint32_t e = farptr(t + T_TAB) + c * 4, g, src, surf;
    int gh = rws(t + T_GH), r, k, transp;
    uint8_t bg, idx;
    if (!rw(e) && !rw(e + 2)) return;
    g = farptr(e);
    src = farptr(t + T_NVF) + rb(t + T_NGLYPH) * 4 + 2 + (uint32_t)rw(g) * gh + 1;
    surf = farptr(t + T_SURF);
    idx = rb(t + T_COLIDX);
    bg = rb(t + T_BG);
    transp = rb(t + T_TRANSP) != 0;
    for (r = 0; r < gh; r++)
        for (k = 0; k < w; k++) {
            uint8_t p = rb(src + r * w + k);
            if (p) put_px(surf, x + k, y + r, rb(t + T_BG + idx + p));
            else if (!transp) put_px(surf, x + k, y + r, bg);
        }
}

static uint8_t g_lead;

/* returns advance (incl. 1px gap) */
static int print_char(uint32_t t, uint8_t c, int x, int y)
{
    int w;
    if (g_cls[c] == C_LEAD) { g_lead = c; return 0; }
    if (g_cls[c] == C_WIDE) {
        uint16_t code = g_lead ? (uint16_t)((g_lead << 8) | c) : c;
        g_lead = 0;
        draw_jp(t, code, x, y);
        return jp_w(t) + 1;
    }
    g_lead = 0;
    orig_width(t, c, &w);
    draw_orig(t, c, w, x, y);
    return w + 1;
}

/* ---------------------------------------------------------- line breaking */
#define MAXBRK 160
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

static int is_jp_at(uint32_t p)  /* byte at p belongs to a Japanese character */
{
    return g_cls[rb(p)] != C_ASCII;
}
/* char at p: returns byte length, code, jp flag */
static int char_at(uint32_t p, uint16_t *code, int *jp)
{
    uint8_t c = rb(p);
    if (g_cls[c] == C_LEAD) {
        uint8_t t2 = rb(p + 1);
        if (g_cls[t2] == C_WIDE) { *code = (uint16_t)((c << 8) | t2); *jp = 1; return 2; }
        *code = c; *jp = 0; return 1;
    }
    *code = c;
    *jp = (g_cls[c] == C_WIDE);
    return 1;
}
/* an ASCII space glued between two Japanese characters (TLK continuation lines) is dropped */
static int jp_space(uint32_t s, uint32_t p)
{
    return rb(p) == ' ' && p > s && is_jp_at(p - 1) && is_jp_at(p + 1);
}

static int ascii_no_start(uint8_t c) { return c && strchr(",.!?)]:;'%>", c) != NULL; }
static int ascii_no_end(uint8_t c) { return c && strchr("([<", c) != NULL; }
static int is_alnum(uint8_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

static int cwidth(uint32_t t, uint32_t p, int len)
{
    int w, sum = 0;
    if (len == 2) return jp_w(t) + 1;
    byte_width(t, rb(p), &w);
    return w + 1;
}

/* Wrap string s using the context's max width. Writes CR into spaces (like the game),
 * Japanese breaks (no space available) are recorded in brk[]. Returns number of lines. */
static int wrap_string(uint32_t t, uint32_t s, uint16_t *brk, int *nbrk)
{
    int maxw = rws(t + T_MAXW), iw = rws(t + T_INDW), il = rws(t + T_INDL);
    int lines = 0, width = 0, prev_jp = 0, prev_noend = 0, prev_alnum = 0;
    uint32_t p, line_start, last_ws = 0, last_jp = 0;
    int have_ws = 0, have_jp = 0;
    *nbrk = 0;
    if (iw) maxw -= iw;
    p = line_start = s;
    for (;;) {
        uint16_t code;
        int jp, len, w;
        uint8_t c = rb(p);
        if (!c) break;
        if (jp_space(s, p)) { p++; continue; }
        len = char_at(p, &code, &jp);
        if (p != line_start) {
            int nostart = jp ? (gflags(code) & 1) : ascii_no_start(c);
            int cur_alnum = !jp && is_alnum(c);
            if ((jp || prev_jp) && !nostart && !prev_noend && !prev_alnum && !cur_alnum) { last_jp = p; have_jp = 1; }
        }
        if (c == '@') {
            if (++lines == il) maxw += iw;
            p += 1; line_start = p; width = 0; have_ws = have_jp = 0; prev_jp = prev_noend = prev_alnum = 0;
            continue;
        }
        w = cwidth(t, p, len);
        width += w;
        if (width >= maxw) {
            if (have_ws && (!have_jp || last_ws + 1 >= last_jp)) {
                wb(last_ws, 0x0d);
                line_start = last_ws + 1;
            } else if (have_jp) {
                if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(last_jp - s);
                line_start = last_jp;
            } else if (jp && p != line_start) {
                if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(p - s);
                line_start = p;
            } else if (p != line_start) {
                if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(p - s);
                line_start = p;
            } else {
                line_start = p + len;
                if (*nbrk < MAXBRK) brk[(*nbrk)++] = (uint16_t)(line_start - s);
            }
            if (++lines == il) maxw += iw;
            p = line_start; width = 0; have_ws = have_jp = 0; prev_jp = prev_noend = prev_alnum = 0;
            continue;
        }
        if (c == ' ' && p > line_start) { last_ws = p; have_ws = 1; }
        prev_jp = jp;
        prev_alnum = !jp && is_alnum(c);
        prev_noend = jp ? (gflags(code) & 2) : ascii_no_end(c);
        p += len;
    }
    return lines + 1;
}

/* callback: wrap(this, char far *s) -> lines */
static uint32_t __cdecl cb_wrap(void)
{
    uint32_t sp = PHYS_SS + R_SP;
    uint32_t t = farptr(sp + 4), s, i;
    uint16_t off = rw(sp + 8), seg = rw(sp + 10);
    brkent *e;
    int lines;
    uint8_t c;
    if (!off && !seg) { SET_AX(0); return 0; }
    s = ((uint32_t)seg << 4) + off;
    for (i = 0; i < 8192 && (c = rb(s + i)); i++)
        if (c == 0x0d || c == 0x0a) wb(s + i, ' ');
    e = &g_brk[g_brk_next];
    lines = wrap_string(t, s, e->off, &e->n);
    e->addr = s;
    e->hash = str_hash(s);
    g_brk_next = (g_brk_next + 1) & 63;
    SET_AX(lines);
    return 0;
}

static int is_brk(uint32_t s, uint32_t p, const uint16_t *brk, int nbrk)
{
    int k;
    if (p == s) return 0;
    for (k = 0; k < nbrk; k++) if (s + brk[k] == p) return 1;
    return 0;
}

static int line_width(uint32_t t, uint32_t s, uint32_t p, const uint16_t *brk, int nbrk)
{
    int sum = 0;
    uint32_t p0 = p;
    for (;;) {
        uint16_t code; int jp, len;
        uint8_t c = rb(p);
        if (!c || c == '@' || c == 0x0d || c == 0x0a) break;
        if (p != p0 && is_brk(s, p, brk, nbrk)) break;
        if (jp_space(s, p)) { p++; continue; }
        len = char_at(p, &code, &jp);
        sum += cwidth(t, p, len);
        p += len;
    }
    return sum;
}

/* callback inside print_string(this, s, x, y) after the prologue (BP frame valid);
 * stores the number of line breaks into [bp-2] and continues at the original epilogue. */
static uint32_t __cdecl cb_print_string(void)
{
    uint32_t bp = PHYS_SS + R_BP;
    uint32_t t = farptr(bp + 6), s = farptr(bp + 10), p;
    int x = rws(bp + 14), y = rws(bp + 16), k, lines = 0, linex, cap = 0;
    const uint16_t *brk = NULL;
    int nbrk = 0;
    uint32_t h = str_hash(s);
    for (k = 0; k < 64; k++) {
        brkent *e = &g_brk[(g_brk_next - 1 - k) & 63];
        if (e->addr == s && e->hash == h) { brk = e->off; nbrk = e->n; break; }
    }
    g_lead = 0;
    if (rws(t + T_CENTER) == 1) {
        int mw = rws(t + T_MAXW);
        x = x + ((int16_t)(mw - line_width(t, s, s, brk, nbrk)) >> 1);
    } else if (rws(t + T_INDW))
        x += rws(t + T_INDW);
    linex = x;
    for (p = s;;) {
        uint8_t c = rb(p);
        int br;
        if (!c) break;
        br = is_brk(s, p, brk, nbrk);
        if (br || c == 0x0d || c == 0x0a || c == '@') {
            lines++;
            if (rws(t + T_INDL) == lines) linex -= rws(t + T_INDW);
            y += rws(t + T_LINEH);
            if (rws(t + T_CENTER) == 1)
                x = rws(t + T_POSX) + ((int16_t)(rws(t + T_MAXW) - line_width(t, s, br ? p : p + 1, brk, nbrk)) >> 1);
            else
                x = linex;
            g_lead = 0;
            if (!br) { p++; continue; }
        }
        if (jp_space(s, p)) { p++; continue; }
        p++;
        if (c == '~') {
            int i;
            for (i = 0; i < 7; i++) {
                int tp = rws(t + T_TABS + i * 2);
                if (tp > x) { x = tp; break; }
            }
        } else if (c >= 0xf1 && c <= 0xf4) {
            wb(t + T_COLIDX, (uint8_t)(c + 0x10));
        } else if (c == 0xf5) {
            wb(t + T_HILITE, 1);
        } else if (c == 0xf0) {
            wb(t + T_HILITE, 0);
            wb(t + T_COLIDX, 0);
        } else {
            if (c == '^') c = ' ';
            if (cap) {
                if (c >= 0x21 && c != '@' && c != '.' && c != '!' && c != '?' && c <= 0xf0) {
                    cap = 0;
                    if (c >= 'a' && c <= 'z') c -= 0x20;
                }
            } else if (c == '.' || c == '!' || c == '?') cap = 1;
            if (g_cls[c] != C_ASCII) cap = 0;
            if (g_cls[c] == C_LEAD && g_cls[rb(p)] != C_WIDE) {
                /* not a Japanese pair: an umlaut etc. of untranslated text */
                int w;
                g_lead = 0;
                orig_width(t, c, &w);
                draw_orig(t, c, w, x, y);
                x += w + 1;
            } else
                x += print_char(t, c, x, y);
        }
    }
    ww(PHYS_SS + R_BP - 2, (uint16_t)lines);
    return 0;
}

/* callback: print_char(this, c, x, y) -> advance */
static uint32_t __cdecl cb_print_char(void)
{
    uint32_t sp = PHYS_SS + R_SP;
    uint32_t t = farptr(sp + 4);
    uint8_t c = rb(sp + 8);
    int x = rws(sp + 10), y = rws(sp + 12), a;
    if (g_cls[c] != C_ASCII) dbg("pc %02x lead=%02x x=%d y=%d ret=%04x:%04x tab=%d\n", c, g_lead, x, y,
                                  rw(sp + 2), rw(sp), rw(farptr(t + T_TAB) + c * 4) || rw(farptr(t + T_TAB) + c * 4 + 2));
    a = print_char(t, c, x, y);
    ww(t + T_CURX, (uint16_t)(x + a));
    SET_AX(a);
    return 0;
}

/* callback: get_width(this, c, int far *w) -> glyph index */
static uint32_t __cdecl cb_get_width(void)
{
    uint32_t sp = PHYS_SS + R_SP;
    uint32_t t = farptr(sp + 4);
    uint8_t c = rb(sp + 8);
    uint32_t wp = farptr(sp + 10);
    int w, r = byte_width(t, c, &w);
    ww(wp, (uint16_t)w);
    SET_AX(r);
    return 0;
}

/* callback: string_width(this, s) -> sum of glyph widths (game semantics: no gaps) */
static uint32_t __cdecl cb_string_width(void)
{
    uint32_t sp = PHYS_SS + R_SP;
    uint32_t t = farptr(sp + 4), s = farptr(sp + 8), p = s;
    int sum = 0;
    for (;;) {
        uint16_t code; int jp, len, w;
        uint8_t c = rb(p);
        if (!c) break;
        if (jp_space(s, p)) { p++; continue; }
        len = char_at(p, &code, &jp);
        if (len == 2) sum += jp_w(t);
        else { byte_width(t, c, &w); if (w > 0) sum += w; }
        p += len;
    }
    SET_AX(sum);
    return 0;
}

/* Copy protection question (resident seg 0ee0:0467, after "push bp; mov bp,sp").
 * this = [bp+6]; the PASSWORD.DAT record was read to this+0x26:
 *   +26 word, +27 line, +28 page, +29 key1, +2A key2, +2B..+3E encoded answer.
 * Formats the question (fmt = *(this+4) -> PROTECT.LTX[0]) into [ds:AF90] like
 * sprintf(fmt, page, line, word); an extra "%s" in the format receives the decoded answer. */
static uint32_t __cdecl cb_protect(void)
{
    uint32_t t = farptr(PHYS_SS + R_BP + 6);
    uint32_t fmt = farptr(farptr(t + 4)), dst = farptr(PHYS_DS + 0xaf90);
    int args[3], ai = 0, i;
    char ans[24], num[12];
    uint8_t cl = rb(t + 0x29), acc = rb(t + 0x2a), c;
    args[0] = (int8_t)rb(t + 0x28); args[1] = (int8_t)rb(t + 0x27); args[2] = (int8_t)rb(t + 0x26);
    for (i = 0; i < 20; i++) {
        uint8_t e = rb(t + 0x2b + i), d = (uint8_t)(e - acc);
        ans[i] = (char)(d ^ cl); cl = d; acc = e;
    }
    ans[20] = 0;
    for (i = 0; i < 20 && ans[i]; i++)
        if ((uint8_t)ans[i] < 0x20 || (uint8_t)ans[i] >= 0x7f) { ans[i] = 0; break; }
    for (i = 0; i < 1000 && (c = rb(fmt)); fmt++) {
        if (c == '%' && rb(fmt + 1) == 'd') {
            const char *q;
            wsprintfA(num, "%d", ai < 3 ? args[ai] : 0); ai++;
            for (q = num; *q; q++) wb(dst + i++, (uint8_t)*q);
            fmt++;
        } else if (c == '%' && rb(fmt + 1) == 's') {
            const char *q;
            for (q = ans; *q; q++) wb(dst + i++, (uint8_t)*q);
            fmt++;
        } else if (c == '%' && rb(fmt + 1) == '%') {
            wb(dst + i++, '%'); fmt++;
        } else
            wb(dst + i++, c);
    }
    wb(dst + i, 0);
    dbg("protect: page %d line %d word %d -> %s\n", args[0], args[1], args[2], ans);
    return 0;
}

static int g_ncalls;
#define WRAP(name) static uint32_t __cdecl w_##name(void) { uint32_t r; \
    if (g_ncalls < 30) dbg("cb %s sp=%x bp=%x\n", #name, R_SP, R_BP); \
    r = name(); if (g_ncalls++ < 30) dbg("  -> ax=%x\n", (uint16_t)REG32(0)); return r; }
WRAP(cb_wrap) WRAP(cb_print_string) WRAP(cb_print_char) WRAP(cb_get_width) WRAP(cb_string_width)

static int g_cb_installed;
static int install_callbacks(void)
{
    uint32_t *tab = (uint32_t*)A_CB_HANDLERS;
    int b = g_cb_base, i;
    if (g_cb_installed) return 1;
    for (i = 0; i < 6; i++)
        if (tab[b + i] != A_CB_ILLEGAL) { dbg("callback slot %x busy (%x)\n", b + i, tab[b + i]); return 0; }
    tab[b + 0] = (uint32_t)w_cb_get_width;
    tab[b + 1] = (uint32_t)w_cb_print_char;
    tab[b + 2] = (uint32_t)w_cb_print_string;
    tab[b + 3] = (uint32_t)w_cb_wrap;
    tab[b + 4] = (uint32_t)w_cb_string_width;
    tab[b + 5] = (uint32_t)cb_protect;
    g_cb_installed = 1;
    dbg("callbacks installed at %x\n", b);
    return 1;
}

/* ----------------------------------------------------- file building */
typedef FILE *(__cdecl *fopen_t)(const char *, const char *);
static fopen_t real_fopen;

static uint8_t *read_all(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");   /* our own CRT (FILE* of msvcrt must not be mixed) */
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

/* 'S' section (STAR.DAT): u32 orig_size, u32 orig_crc, u32 nrep, then per replaced entry:
 *   u32 index, u32 nsplice, nsplice * {u32 start, u32 end, u32 len, bytes}
 * Each splice replaces original bytes [start,end) of that entry, so the DLL carries no
 * original game data. Archive: u16 n, n * {u16 flag, char name[14], u32 off}, data. */
typedef struct { const uint8_t *rec; } datrep;
static uint8_t *build_dat(const uint8_t *orig, uint32_t olen, const uint8_t *sec, uint32_t slen, uint32_t *outlen)
{
    uint32_t osz = ((const uint32_t*)sec)[0], ocrc = ((const uint32_t*)sec)[1], nrep = ((const uint32_t*)sec)[2];
    uint32_t n = *(const uint16_t*)orig, base = 2 + n * 20, i, k, total, pos;
    const uint8_t **rep;
    const uint8_t *p = sec + 12;
    uint32_t *start, *end;
    uint8_t *out;
    (void)slen;
    if (osz != olen || crc32(orig, olen) != ocrc) { dbg("dat mismatch\n"); return NULL; }
    rep = (const uint8_t**)calloc(n, sizeof(*rep));
    start = (uint32_t*)calloc(n, 4);
    end = (uint32_t*)calloc(n, 4);
    for (i = 0; i < n; i++) start[i] = base + *(const uint32_t*)(orig + 2 + i * 20 + 16);
    for (i = 0; i < n; i++) {
        end[i] = olen;
        for (k = i + 1; k < n; k++) if (start[k] >= start[i]) { end[i] = start[k]; break; }
    }
    for (i = 0; i < nrep; i++) {
        uint32_t idx = ((const uint32_t*)p)[0], ns = ((const uint32_t*)p)[1];
        if (idx < n) rep[idx] = p;
        p += 8;
        for (k = 0; k < ns; k++) p += 12 + ((const uint32_t*)p)[2];
    }
    total = base;
    for (i = 0; i < n; i++) {
        uint32_t l = end[i] - start[i];
        if (rep[i]) {
            const uint8_t *q = rep[i] + 8;
            uint32_t ns = ((const uint32_t*)rep[i])[1];
            for (k = 0; k < ns; k++) {
                const uint32_t *r = (const uint32_t*)q;
                l = l - (r[1] - r[0]) + r[2];
                q += 12 + r[2];
            }
        }
        total += l;
    }
    out = (uint8_t*)malloc(total);
    memcpy(out, orig, base);
    pos = base;
    for (i = 0; i < n; i++) {
        uint32_t s0 = start[i], l = end[i] - start[i];
        *(uint32_t*)(out + 2 + i * 20 + 16) = pos - base;
        if (rep[i]) {
            const uint8_t *q = rep[i] + 8;
            uint32_t ns = ((const uint32_t*)rep[i])[1], cur = 0;
            for (k = 0; k < ns; k++) {
                const uint32_t *r = (const uint32_t*)q;
                if (r[0] < cur || r[1] > l || r[0] > r[1]) { free(out); free(rep); free(start); free(end); dbg("bad splice %u\n", i); return NULL; }
                memcpy(out + pos, orig + s0 + cur, r[0] - cur); pos += r[0] - cur;
                memcpy(out + pos, q + 12, r[2]); pos += r[2];
                cur = r[1];
                q += 12 + r[2];
            }
            memcpy(out + pos, orig + s0 + cur, l - cur); pos += l - cur;
        } else {
            memcpy(out + pos, orig + s0, l);
            pos += l;
        }
    }
    free(rep); free(start); free(end);
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

static const char *patched_path(const char *orig_path, char *buf)
{
    const char *bn = base_name(orig_path);
    uint32_t slen, olen, nlen;
    const uint8_t *sx, *sd;
    uint8_t *orig, *out = NULL;
    if (!g_enabled) return NULL;
    sx = find_sect('X', bn, &slen);
    sd = sx ? NULL : find_sect('S', bn, &slen);
    if (!sx && !sd) return NULL;
    wsprintfA(buf, "%s\\%08X_%s", g_dir, g_blob_crc, bn);
    if (GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES) return buf;
    {   /* remove copies left by older patch versions */
        char pat[MAX_PATH], old[MAX_PATH], cur[16];
        WIN32_FIND_DATAA fd;
        HANDLE fh;
        wsprintfA(pat, "%s\\????????_%s", g_dir, bn);
        wsprintfA(cur, "%08X_", g_blob_crc);
        fh = FindFirstFileA(pat, &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                if (_strnicmp(fd.cFileName, cur, 9)) {
                    wsprintfA(old, "%s\\%s", g_dir, fd.cFileName);
                    DeleteFileA(old);
                }
            } while (FindNextFileA(fh, &fd));
            FindClose(fh);
        }
    }
    orig = read_all(orig_path, &olen);
    if (!orig) return NULL;
    dbg("building %s (%u bytes)\n", bn, olen);
    out = sx ? build_exe(orig, olen, sx, slen, &nlen) : build_dat(orig, olen, sd, slen, &nlen);
    free(orig);
    if (!out) return NULL;
    if (!write_all(buf, out, nlen)) { free(out); return NULL; }
    free(out);
    dbg("built %s\n", buf);
    return buf;
}

static FILE *__cdecl hook_fopen(const char *name, const char *mode)
{
    char buf[MAX_PATH];
    const char *pp;
    FILE *f;
    dbg("fopen %s %s\n", name ? name : "(null)", mode ? mode : "");
    if (!name || !g_enabled || (mode && strchr(mode, 'w'))) return real_fopen(name, mode);
    EnterCriticalSection(&g_cs);
    pp = patched_path(name, buf);
    if (pp && !install_callbacks()) pp = NULL;
    LeaveCriticalSection(&g_cs);
    if (pp) {
        /* never let the game write into our copy */
        f = real_fopen(pp, (mode && strchr(mode, '+')) ? "rb" : mode);
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
    wsprintfA(g_dir, "%s\\RoA2_JP", la);
    CreateDirectoryA(g_dir, NULL);
    if (!verify_dosbox()) return;
    if (!load_blob(self)) return;
    crt = GetModuleHandleA("msvcrt.dll");
    if (!crt) return;
    real_fopen = (fopen_t)GetProcAddress(crt, "fopen");
    if (!real_fopen) return;
    if (!hook_iat()) return;
    g_enabled = 1;
    dbg("RoA2 JP patch active, blob %08X\n", g_blob_crc);
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

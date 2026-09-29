#include "ui.h"

#include <stdarg.h>
#include <string.h>

static C2D_TextBuf text_buf;

// Parsed strings are cached across frames (parsing through the system font is the costly
// part of text on Old 3DS; most strings repeat every frame).
#define TC_ENTRIES 160
#define TC_MAXLEN 96
typedef struct {
    u32 hash;
    char str[TC_MAXLEN];
    C2D_Text text;
    float width;  // at size 1
} TextEntry;
static TextEntry tc[TC_ENTRIES];
static int tc_count;
static C2D_TextBuf tc_buf;

static u32 str_hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s) h = (h ^ (u8)*s++) * 16777619u;
    return h;
}

// Parsed text for s (NULL if it cannot be parsed); *width is its width at size 1.
static const C2D_Text *get_text(const char *s, float *width)
{
    size_t len = strlen(s);
    if (len >= TC_MAXLEN) {  // long strings: this frame only
        static C2D_Text t;
        const char *end = C2D_TextParse(&t, text_buf, s);  // stops early when the buffer is full
        if (!end || *end) return NULL;
        C2D_TextOptimize(&t);
        C2D_TextGetDimensions(&t, 1, 1, width, NULL);
        return &t;
    }
    u32 h = str_hash(s);
    for (int i = 0; i < tc_count; i++)
        if (tc[i].hash == h && !strcmp(tc[i].str, s)) {
            *width = tc[i].width;
            return &tc[i].text;
        }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (tc_count < TC_ENTRIES) {
            TextEntry *e = &tc[tc_count];
            const char *end = C2D_TextParse(&e->text, tc_buf, s);
            if (end && !*end) {
                C2D_TextOptimize(&e->text);
                C2D_TextGetDimensions(&e->text, 1, 1, &e->width, NULL);
                e->hash = h;
                memcpy(e->str, s, len + 1);
                tc_count++;
                *width = e->width;
                return &e->text;
            }
        }
        // full: start over (text already drawn this frame has its vertices queued)
        C2D_TextBufClear(tc_buf);
        tc_count = 0;
    }
    return NULL;
}

void ui_init(void)
{
    text_buf = C2D_TextBufNew(8192);
    tc_buf = C2D_TextBufNew(6144);
}

void ui_exit(void)
{
    C2D_TextBufDelete(text_buf);
    C2D_TextBufDelete(tc_buf);
}

void ui_frame_begin(void)
{
    C2D_TextBufClear(text_buf);
}

static float draw_plain(float x, float y, float size, u32 color, const char *s)
{
    if (dbg_flag('t') || !*s) return 0;
    float w;
    const C2D_Text *t = get_text(s, &w);
    if (!t) return 0;
    C2D_DrawText(t, C2D_WithColor, x, y, 0, size, size, color);
    return w * size;
}

void ui_text(float x, float y, float size, u32 color, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    draw_plain(x, y, size, color, buf);
}

float ui_text_width(float size, const char *s)
{
    float w;
    if (!*s || !get_text(s, &w)) return 0;
    return w * size;
}

void ui_text_center(float cx, float y, float size, u32 color, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    draw_plain(cx - ui_text_width(size, buf) / 2, y, size, color, buf);
}

// Length of the colour code at s (0 if none) and its colour.
static int color_code(const char *s, u32 base, u32 *color)
{
    switch (*s) {
    case '\\': *color = UI_RED; return 1;
    case '@': *color = UI_GREEN; return 1;
    case '&': *color = UI_PURPLE; return 1;
    case '/': *color = UI_BLUE; return 1;
    case '|': *color = UI_GRAY; return 1;
    case '`': *color = UI_YELLOW; return 1;
    case '~': *color = base; return 1;
    }
    if ((u8)s[0] == 0xE2 && (u8)s[1] == 0x84 && (u8)s[2] == 0x96) {  // U+2116
        *color = UI_ORANGE;
        return 3;
    }
    return 0;
}

float ui_text_coded(float x, float y, float size, u32 base, const char *s)
{
    char seg[256];
    int n = 0;
    float x0 = x;
    u32 color = base;
    while (1) {
        u32 next = color;
        int code = *s ? color_code(s, base, &next) : 0;
        if (!*s || code) {
            seg[n] = 0;
            x += draw_plain(x, y, size, color, seg);
            n = 0;
            if (!*s) break;
            color = next;
            s += code;
            continue;
        }
        if (n < (int)sizeof seg - 1) seg[n++] = *s;
        s++;
    }
    return x - x0;
}

void ui_strip_codes(const char *s, char *out, size_t size)
{
    size_t n = 0;
    u32 dummy;
    while (*s && n + 1 < size) {
        int code = color_code(s, 0, &dummy);
        if (code) { s += code; continue; }
        out[n++] = *s++;
    }
    out[n] = 0;
}

bool ui_keyboard(const char *hint, const char *initial, char *out, size_t size, int max_chars)
{
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, max_chars);
    swkbdSetHintText(&kb, hint);
    if (initial) swkbdSetInitialText(&kb, initial);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    char buf[256];
    SwkbdButton button = swkbdInputText(&kb, buf, sizeof buf);
    if (button != SWKBD_BUTTON_CONFIRM) return false;
    snprintf(out, size, "%s", buf);
    return true;
}

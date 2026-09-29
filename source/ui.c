#include "ui.h"

#include <stdarg.h>

static C2D_TextBuf text_buf;

void ui_init(void)
{
    text_buf = C2D_TextBufNew(8192);
}

void ui_exit(void)
{
    C2D_TextBufDelete(text_buf);
}

void ui_frame_begin(void)
{
    C2D_TextBufClear(text_buf);
}

static float draw_plain(float x, float y, float size, u32 color, const char *s)
{
    if (dbg_flag('t') || !*s) return 0;
    C2D_Text t;
    if (!C2D_TextParse(&t, text_buf, s)) return 0;  // buffer full: skip the rest of the frame's text
    C2D_TextOptimize(&t);
    float w;
    C2D_TextGetDimensions(&t, size, size, &w, NULL);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0, size, size, color);
    return w;
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
    C2D_Text t;
    static C2D_TextBuf measure_buf;
    if (!measure_buf) measure_buf = C2D_TextBufNew(256);
    C2D_TextBufClear(measure_buf);
    if (!C2D_TextParse(&t, measure_buf, s)) return 0;
    float w;
    C2D_TextGetDimensions(&t, size, size, &w, NULL);
    return w;
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

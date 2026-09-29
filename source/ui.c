#include "ui.h"

#include <stdarg.h>
#include <math.h>
#include <string.h>

#include "gen/ui_font.h"
#include "sprite.h"

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

// ------------------------------------------------------------------ pixel font
// Sonic 3 & Knuckles HUD Font (chriswal1200, FontStruct, CC BY-SA 3.0), one texel per screen
// pixel: used for every plain ASCII string; others fall back to the system font.

static C2D_SpriteSheet font_sheet;
static C3D_Tex *font_tex;

static bool font_usable(const char *s)
{
    if (!font_tex) return false;
    for (; *s; s++)
        if ((u8)*s < UI_FONT_FIRST || (u8)*s > UI_FONT_LAST) return false;
    return true;
}

static int font_scale(float size) { return size >= 0.62f ? 2 : 1; }

static int font_glyph(char c)
{
    if (c == '{') c = '(';
    if (c == '}') c = ')';
    return (u8)c - UI_FONT_FIRST;
}

static float font_width(const char *s, int scale)
{
    int w = 0;
    for (; *s; s++) w += UI_FONT_ADVANCE[font_glyph(*s)];
    return (float)(w * scale);
}

static float font_draw(float x, float y, int scale, u32 color, const char *s)
{
    x = floorf(x + 0.5f);
    y = floorf(y + 0.5f);
    // white glyphs times the colour (the sprite batcher multiplies exactly)
    float x0 = x;
    for (; *s; s++) {
        int g = font_glyph(*s);
        if (*s != ' ')
            sprite_draw_tex(font_tex, (g % UI_FONT_COLS) * UI_FONT_CELL_W, (g / UI_FONT_COLS) * UI_FONT_HEIGHT,
                            UI_FONT_CELL_W, UI_FONT_HEIGHT, x, y, (float)scale, color);
        x += UI_FONT_ADVANCE[g] * scale;
    }
    return x - x0;
}

void ui_init(void)
{
    text_buf = C2D_TextBufNew(8192);
    tc_buf = C2D_TextBufNew(6144);
    font_sheet = C2D_SpriteSheetLoad("romfs:/gfx/ui_font.t3x");
    if (font_sheet) {
        font_tex = C2D_SpriteSheetGetImage(font_sheet, 0).tex;
        C3D_TexSetFilter(font_tex, GPU_NEAREST, GPU_NEAREST);
    }
}

void ui_exit(void)
{
    C2D_TextBufDelete(text_buf);
    C2D_TextBufDelete(tc_buf);
    if (font_sheet) C2D_SpriteSheetFree(font_sheet);
}

void ui_frame_begin(void)
{
    C2D_TextBufClear(text_buf);
}

static float draw_plain(float x, float y, float size, u32 color, const char *s)
{
    if (dbg_flag('t') || !*s) return 0;
    // pixel font, unless the line would run off the screen (the system font is narrower)
    if (font_usable(s) && x + font_width(s, font_scale(size)) <= sprites_scene_width())
        return font_draw(x, y, font_scale(size), color, s);
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
    if (font_usable(s)) return font_width(s, font_scale(size));
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

// Coded text wrapped at word boundaries between left and right; starts at x on line y and
// returns the number of lines used (1 or more). With draw false it only counts. *end_x: where
// the text ended.
int ui_text_coded_wrap(float x, float y, float left, float right, float line_h, float size, u32 base,
                       const char *s, bool draw, float *end_x)
{
    int lines = 1;
    u32 color = base;
    char ch[2] = { 0, 0 };
    float space = ui_text_width(size, "a");
    while (*s) {
        // one word: characters up to a space, colour codes applied as they come
        const char *w = s;
        float ww = 0;
        u32 c = color;
        while (*w && *w != ' ') {
            u32 next;
            int code = color_code(w, base, &next);
            if (code) { c = next; w += code; continue; }
            int len = ((u8)*w & 0xE0) == 0xC0 ? 2 : ((u8)*w & 0xF0) == 0xE0 ? 3 : 1;
            char g[4] = { 0 };
            memcpy(g, w, len);
            ww += ui_text_width(size, g);
            w += len;
        }
        if (x + ww > right && x > left) {
            x = left;
            y += line_h;
            lines++;
        }
        while (s < w) {
            u32 next;
            int code = color_code(s, base, &next);
            if (code) { color = next; s += code; continue; }
            int len = ((u8)*s & 0xE0) == 0xC0 ? 2 : ((u8)*s & 0xF0) == 0xE0 ? 3 : 1;
            char g[4] = { 0 };
            memcpy(g, s, len);
            if (draw) x += draw_plain(x, y, size, color, g);
            else x += ui_text_width(size, g);
            s += len;
        }
        (void)c;
        while (*s == ' ') {
            x += space;
            s++;
        }
    }
    (void)ch;
    if (end_x) *end_x = x;
    return lines;
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

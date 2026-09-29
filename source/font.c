#include "font.h"

#include "gen/sprites.h"
#include "sprite.h"

// Next UTF-8 code point
static u32 next_cp(const char **s)
{
    const u8 *p = (const u8 *)*s;
    u32 c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

float text_spr(float xx, float yy, const char *str, u32 color, float alpha)
{
    bool draw = xx > -1000;
    float x = xx, y = yy, max_w = 0;
    u32 col = color;
    sprite_set_exact_tint(true);
    while (*str) {
        u32 c = next_cp(&str);
        int ind = 44;
        switch (c) {
        case '\n': x = xx; y += 8; continue;
        case ' ': x += 5; continue;
        case '-': ind = 26; break;
        case ',': ind = 27; break;
        case '0': ind = 37; break;
        case '.': ind = 38; break;
        case '\'': ind = 39; break;
        case ':': ind = 40; break;
        case '(': ind = 41; break;
        case ')': ind = 42; break;
        case '%': ind = 43; break;
        case '+': ind = 77; break;
        case 0x5C: col = 0x3700C2; continue;  // backslash
        case '@': col = 0x39FF0F; continue;
        case '&': col = 0xFF24B8; continue;
        case '/': col = 0xFF675D; continue;
        case '|': col = 0x646464; continue;
        case '`': col = 0x00DBFF; continue;
        case 0x2116: col = 0x1460EA; continue;
        case '~': col = color; continue;
        default:
            if (c >= '1' && c <= '9') ind = 28 + (c - '1');
            break;
        }
        if (c >= 0x430 && c <= 0x44F) ind = 45 + (c - 0x430);   // а..я
        if (c >= 0x410 && c <= 0x42F) ind = 45 + (c - 0x410);   // capitals share the glyphs
        if (c >= 'a' && c <= 'z') ind = c - 'a';
        if (c >= 'A' && c <= 'Z') ind = c - 'A';
        if (draw)
            sprite_draw(SPR_LETTER1, ind, x, y, 1, 1, 0, 0xFF000000 | (color != FONT_WHITE ? color : col), alpha);
        x += 6;
        switch (c) {
        case 'w': case 'm': case 'x': case 'n':
        case 0x43C: case 0x434: case 0x438: case 0x439: case 0x44E: case 0x44C:
        case 0x43B: case 0x448: case 0x449: case 0x446: case 0x436:
            x += 2;
            break;
        }
        if (x - xx > max_w) max_w = x - xx;
    }
    sprite_set_exact_tint(false);
    return max_w;
}

float text_spr_width(const char *str) { return text_spr(-2000, 0, str, FONT_WHITE, 1); }

void number_spr(float x, float y, const char *str, u32 color, float alpha)
{
    int len = (int)strlen(str);
    sprite_set_exact_tint(true);
    for (int i = 0; i < len; i++)
        if (str[i] >= '0' && str[i] <= '9')
            sprite_draw(SPR_NUMBER, str[i] - '0', x + i * 5 - (len - 1) * 2, y, 1, 1, 0, 0xFF000000 | color, alpha);
    sprite_set_exact_tint(false);
}

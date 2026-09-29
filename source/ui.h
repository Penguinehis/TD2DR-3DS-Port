#pragma once
// Text drawing (system font) and the software keyboard.

#include "common.h"

#define UI_WHITE C2D_Color32(255, 255, 255, 255)
#define UI_GRAY C2D_Color32(150, 150, 160, 255)
#define UI_RED C2D_Color32(255, 70, 70, 255)
#define UI_GREEN C2D_Color32(80, 230, 80, 255)
#define UI_YELLOW C2D_Color32(255, 225, 70, 255)
#define UI_BLUE C2D_Color32(90, 150, 255, 255)
#define UI_PURPLE C2D_Color32(200, 110, 255, 255)
#define UI_ORANGE C2D_Color32(255, 160, 40, 255)

void ui_init(void);
void ui_exit(void);
void ui_frame_begin(void);  // once per frame, before any text is drawn

void ui_text(float x, float y, float size, u32 color, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
void ui_text_center(float cx, float y, float size, u32 color, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
float ui_text_width(float size, const char *s);

// Text with the server's colour codes: \ red, @ green, & purple, / blue, | gray,
// ` yellow, U+2116 orange, ~ reset to base. Returns the drawn width.
int ui_text_coded_wrap(float x, float y, float left, float right, float line_h, float size, u32 base,
                       const char *s, bool draw, float *end_x);
float ui_text_coded(float x, float y, float size, u32 base, const char *s);
// Strip the colour codes (for measuring or logging).
void ui_strip_codes(const char *s, char *out, size_t size);

// Blocking software keyboard. Returns false if cancelled.
bool ui_keyboard(const char *hint, const char *initial, char *out, size_t size, int max_chars);

#pragma once
// The game's sprite font (scr_text_spr / scr_number_spr): spr_letter1 glyphs, 6 px advance,
// colour codes \ @ & / | ` U+2116 ~ like the chat, Cyrillic letters. Colours are BGR.

#include "common.h"

#define FONT_WHITE 0xFFFFFF

// Draw at (x, y) in screen pixels; returns the width. With x < -1000 it only measures.
float text_spr(float x, float y, const char *str, u32 color, float alpha);
float text_spr_width(const char *str);
// scr_number_spr: spr_number digits centred on x.
void number_spr(float x, float y, const char *str, u32 color, float alpha);

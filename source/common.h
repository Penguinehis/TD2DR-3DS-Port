#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240
#define GAME_FPS 60

#define PACKED __attribute__((packed))

// Sprites and tiles are batched with a shader of our own (sprite.c); any citro2d drawing first
// closes that batch so the drawing order holds.
void sprites_batch_end(void);
#define C2D_DrawRectSolid(...) (sprites_batch_end(), C2D_DrawRectSolid(__VA_ARGS__))
#define C2D_DrawImage(...) (sprites_batch_end(), C2D_DrawImage(__VA_ARGS__))
#define C2D_DrawImageAt(...) (sprites_batch_end(), C2D_DrawImageAt(__VA_ARGS__))
#define C2D_DrawText(...) (sprites_batch_end(), C2D_DrawText(__VA_ARGS__))
#define C2D_TargetClear(...) (sprites_batch_end(), C2D_TargetClear(__VA_ARGS__))
#define C2D_SceneBegin(...) (sprites_batch_end(), C2D_SceneBegin(__VA_ARGS__))
#define C2D_Flush(...) (sprites_batch_end(), C2D_Flush(__VA_ARGS__))
#define C3D_FrameEnd(...) (sprites_batch_end(), C3D_FrameEnd(__VA_ARGS__))

// Whole-file read into a malloc'd buffer. Returns NULL on failure.
void *read_file(const char *path, size_t *size_out);

// Read a NUL-terminated string from a cursor, advancing it.
static inline const char *take_str(const u8 **p)
{
    const char *s = (const char *)*p;
    *p += strlen(s) + 1;
    return s;
}

#define TAKE(p, T) ({ T _v; memcpy(&_v, *(p), sizeof(T)); *(p) += sizeof(T); _v; })

void dbg_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

// Frame timings line (main.c)
void main_draw_perf(float x, float y);

// Debug switches from sdmc:/sonic3ds.cfg (see main.c).
bool dbg_flag(char c);
void dbg_flush(void);  // write the buffered log lines
int dbg_num(char c, int def);
// Text after the letter up to ';' (e.g. "c192.168.1.5:8606;"). Returns false if absent.
bool dbg_str(char c, char *out, size_t size);

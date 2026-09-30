#pragma once

#include "common.h"
#include "gen/sprites.h"

enum SpriteMode { SPRITE_UNUSED, SPRITE_MASK, SPRITE_TILEMAP, SPRITE_NORMAL };

typedef struct PACKED {
    u8 mode;
    u16 group;
    u16 width, height;
    s16 xorigin, yorigin;
    s16 bbox_left, bbox_top, bbox_right, bbox_bottom;
    u8 collision_kind;
    u8 speed_type;       // 0 = frames per second, 1 = frames per game frame
    float speed;
    u16 frame_count;
    u32 first_frame;
} SpriteInfo;

// Loads romfs:/data/sprites.bin. Call once after romfsInit().
void sprites_init(void);
void sprites_exit(void);

const SpriteInfo *sprite_info(int spr);

// Frame advance per game frame at image_speed 1 (GameMaker semantics).
float sprite_frame_step(int spr);

// Draw like GameMaker's draw_sprite_ext. (x, y) is the sprite origin in camera space.
// colour is ABGR (GameMaker and citro2d share the layout); alpha 0..1.
void sprite_draw(int spr, float frame, float x, float y, float xscale, float yscale,
                 float angle_deg, u32 colour, float alpha);

// Coloured image_blend is approximated (half-way lerp) because citro2d cannot multiply.
// For white artwork (fonts, flashes) an exact tint is a full lerp; enable it around such draws.
void sprite_set_exact_tint(bool exact);

// Draw a sprite repeated to cover a rectangle (background layers). Camera space.
// True when sprite_draw_tiled with these arguments covers the whole view with opaque pixels
bool sprite_tiled_covers(int spr, float frame, float x, float y, bool htile, bool vtile, float view_w, float view_h);
// A texture rect (not a sprite) through the batcher: exact colour multiply (UI font glyphs)
void sprite_draw_tex(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float scale, u32 rgba);
// Scaled soft images (full-screen vignettes 480x270 -> 400x240): linear filtering, so the
// uneven nearest-neighbour rows of a non-integer scale do not show
void sprite_draw_smooth(int spr, float frame, float x, float y, float xscale, float yscale, u32 colour, float alpha);
// The frame pixels [sx0, sx1) x [sy0, sy1) of a normal sprite, unscaled, the origin at (x, y)
void sprite_draw_sub(int spr, float frame, float x, float y, int sx0, int sy0, int sx1, int sy1, u32 colour,
                     float alpha);
// A tilemap sprite's pixel columns [src_l, src_r) only (multiples of 16), unscaled
void sprite_draw_part(int spr, float frame, float x, float y, int src_l, int src_r);
void sprite_draw_tiled(int spr, float frame, float x, float y, bool htile, bool vtile,
                       float view_w, float view_h, u32 colour);

// Start a new frame for the texture cache (sheets used this frame are never evicted).
void sprites_frame_begin(void);
// Load texture sheets synchronously for the next frames (level start); otherwise sheets needed
// in play are read in the background and appear a frame or two late.
void sprites_sync_load(int frames);
bool sprites_loading(void);  // inside that level-start window
void sprite_preload(int spr);
void sprites_flush(void);  // free every loaded sheet (a new level: the old one's art is not needed)  // load a sprite's sheets now (in the level-start window: at once)
// The scene being drawn (after C2D_SceneBegin): 400x240 top, 320x240 bottom
void sprites_scene(float w, float h);
float sprites_scene_width(void);
// Per-frame draw statistics (quads drawn, texture changes) of the previous frame
extern int sprite_stat_quads, sprite_stat_switches;

// Hint that a texture group is about to be needed (e.g. on room load).
void sprites_preload_group(int group);

// Precise collision masks (romfs:/data/masks.bin). Returns false when the sprite has none,
// in which case its rectangular bbox is the collision shape.
bool sprite_has_mask(int spr);
// Pixel (lx, ly) in sprite-local coordinates (origin at the sprite's top-left).
bool sprite_mask_test(int spr, int frame, int lx, int ly);

// Debug stats.
int sprites_loaded_sheets(void);
u32 sprites_linear_free(void);

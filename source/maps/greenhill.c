// room_greenhill: obj_ghz_controller (sky scroll + rain), obj_ghz_water (lightning water),
// obj_ghz_wave, obj_flash, SERVER_GHZTHUNDER_STATE.
#include <math.h>

#include "../audio.h"
#include "../entities.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"
#include "../settings.h"

#define MAX_RAIN 640

typedef struct {
    float x, y, spd;
} Drop;

static struct {
    RoomLayer *sky;
    float bg_x;
    bool optimize;             // fewer drops (the GML does this on Android; here on Old 3DS)
    int rain_sprite;
    Drop rain[MAX_RAIN];
    int rain_count;
    float flash;               // obj_flash image_alpha
    bool in_water;             // global.player.inWater
    int rain_sound;
} ghz;

static float current_time_ms(void) { return level.time * 1000.0f / 60; }

// ------------------------------------------------------------------ obj_ghz_controller

static void controller_create(MapObj *o)
{
    (void)o;
    ghz.sky = layer_find("Background");
    ghz.bg_x = 0;
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    ghz.optimize = !new3ds;
    const ObjectInfo *rain = object_info(OBJ_GHZ_RAIN);
    ghz.rain_sprite = rain ? rain->sprite : -1;
    ghz.rain_count = 0;
    ghz.rain_sound = audio_play_ex(SND_RAIN, 1, true);
}

static void spawn_drop(float x, float y)
{
    if (ghz.rain_count >= MAX_RAIN) return;
    Drop *d = &ghz.rain[ghz.rain_count++];
    d->x = x;
    d->y = y;
    d->spd = -(rand() % 1000) / 1000.0f;  // random_range(0, -1)
}

static float rnd(float a, float b) { return a + (b - a) * (rand() % 10000) / 10000.0f; }

static void controller_step(MapObj *o)
{
    (void)o;
    // The GML camera is 480x270; the rain area follows it around the 3DS view.
    float cx = level.cam_x - 40, cy = level.cam_y - 15;
    int top = ghz.optimize ? 1 : 3, side = ghz.optimize ? 1 : 2;
    for (int pass = 0; pass < 2; pass++) {
        if (!settings.gfx_weather) break;
        for (int i = 0; i < top; i++) spawn_drop(cx + rnd(-120, 560), cy - 1);
        for (int i = 0; i < side; i++) spawn_drop(cx + 481, cy + rnd(-120, 350));
    }
    // obj_ghz_rain Step
    for (int i = 0; i < ghz.rain_count;) {
        Drop *d = &ghz.rain[i];
        d->y += 5 + d->spd;
        d->x -= 3 + d->spd;
        if (d->y >= cy + 310) *d = ghz.rain[--ghz.rain_count];
        else i++;
    }
    if (ghz.flash > 0) ghz.flash -= 0.016f;
}

// Draw_72 (Pre-Draw): the sky layer scrolls left, fixed to the camera
static void controller_end_step(MapObj *o)
{
    (void)o;
    ghz.bg_x -= 1;
    layer_set_pos(ghz.sky, ghz.bg_x + level.cam_x, level.cam_y);
}

static void controller_destroy(MapObj *o)
{
    (void)o;
    audio_stop(ghz.rain_sound);
}

// ------------------------------------------------------------------ obj_ghz_water

typedef struct {
    bool electro;
} Water;

static void water_step(MapObj *o)
{
    if (!level.has_player) return;
    Player *p = &level.player;
    float l = o->x, r = o->x, t = o->y, b = o->y;
    const SpriteInfo *s = sprite_info(o->sprite);
    if (s) {
        l = o->x + (s->bbox_left - s->xorigin) * o->xscale;
        r = o->x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
        t = o->y + (s->bbox_top - s->yorigin) * o->yscale;
        b = o->y + (s->bbox_bottom + 1 - s->yorigin) * o->yscale;
    }
    bool touch = (p->sBL.x >= l && p->sBL.x < r && p->sBL.y >= t && p->sBL.y < b) ||
                 (p->sBR.x >= l && p->sBR.x < r && p->sBR.y >= t && p->sBR.y < b);
    if (touch) {
        if (MAPOBJ_VARS(o, Water)->electro) player_hurt(p, 20, -p->image_xscale * 4, -6);
        if (!ghz.in_water) {
            ghz.in_water = true;
            player_sound(p, SND_WATERSPLASH);
            net_quick_effect(p->x, t, SPR_WATERSPLASH, false, 1, 0, 0, 1);
        }
        player_slow(p, 0.5f);
    } else if (ghz.in_water) {
        ghz.in_water = false;
        player_sound(p, SND_WATERSPLASH);
        net_quick_effect(p->x, t, SPR_WATERSPLASH, false, 1, 0, 0, 1);
    }
}

static void water_draw(MapObj *o)
{
    if (!MAPOBJ_VARS(o, Water)->electro) return;
    const SpriteInfo *s = sprite_info(o->sprite);
    float l = o->x, r = o->x + 97, t = o->y;
    if (s) {
        l = o->x + (s->bbox_left - s->xorigin) * o->xscale;
        r = o->x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
        t = o->y + (s->bbox_top - s->yorigin) * o->yscale;
    }
    float frame = current_time_ms() / 45 * sprite_frame_step(SPR_GHZ_ELECTROSHOCK);
    for (int i = 0; i < (r - l) / 97 + 1; i++) {
        float x = i * 97 - level.cam_x;
        if (x < -97 || x > TOP_W) continue;
        sprite_draw(SPR_GHZ_ELECTROSHOCK, frame, x, t - level.cam_y, 1, 1, 0, 0xFFFFFFFF, 1);
    }
}

// ------------------------------------------------------------------ obj_ghz_wave

static void wave_step(MapObj *o) { o->y = o->ystart + sinf(current_time_ms() / 250) * 2; }

// ------------------------------------------------------------------ level

static void draw_front(void)
{
    for (int i = 0; i < ghz.rain_count; i++) {
        float x = ghz.rain[i].x - level.cam_x, y = ghz.rain[i].y - level.cam_y;
        if (x < -16 || y < -16 || x > TOP_W + 16 || y > TOP_H + 16) continue;
        sprite_draw(ghz.rain_sprite, 0, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    }
}

static void draw_gui(void)
{
    // obj_flash: a white screen fading out
    if (ghz.flash > 0) C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32f(1, 1, 1, fminf(ghz.flash, 1)));
}

static void set_electro(bool on)
{
    for (int i = 0;; i++) {
        MapObj *w = mapobj_find(OBJ_GHZ_WATER, i);
        if (!w) break;
        MAPOBJ_VARS(w, Water)->electro = on;
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    if (type != SERVER_GHZTHUNDER_STATE) return false;
    if (pass) return true;
    u8 t = rd_u8(r);
    if (t == 0) {
        ghz.flash = 1;
        audio_play(SND_THUNDER);
        set_electro(true);
    } else if (t == 1) {
        set_electro(false);
    }
    return true;
}

static void leave(void)
{
    audio_stop(ghz.rain_sound);
    memset(&ghz, 0, sizeof ghz);
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_GHZ_CONTROLLER, .create = controller_create, .step = controller_step,
      .end_step = controller_end_step, .destroy = controller_destroy },
    { .object = OBJ_GHZ_WATER, .step = water_step, .draw = water_draw },
    { .object = OBJ_GHZ_WAVE, .step = wave_step },
    { .object = -1 },
};

const MapModule MAP_GREENHILL = {
    .room = ROOM_GREENHILL,
    .objects = OBJECTS,
    .draw_front = draw_front,
    .draw_gui = draw_gui,
    .packet = packet,
    .leave = leave,
};

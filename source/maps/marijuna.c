// room_marijuna: obj_marijuna_crystalcontroller (swapped controls + the static after the exit
// ring spawns), obj_marijuna_crystal (SERVER_MJCRYSTAL_STATE), obj_marijuna_lavaplatform
// (SERVER_MJLAVA_STATE, moving jump-through platform over lava), obj_marijuna_judger
// (SERVER_MJJUDGER_STATE) with its obj_marijuna_judgerpor shots, obj_marijuna_guko and
// obj_marjiuna_static.
#include <math.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

static struct {
    int level_state;           // obj_level.state (1 once the exit ring spawned)
    int lava_loop;             // snd_lava emitter loop handle (-1 none)
} mj = { .lava_loop = -1 };

static float current_time_ms(void) { return level.time * 1000.0f / 60; }
static float rnd(float a, float b) { return a + (b - a) * (rand() % 10000) / 10000.0f; }

// The emitter gain used by every object here: 1 - min(dist to the view centre, 700) / 700
static float emitter_gain(float x, float y)
{
    float d = hypotf(x - (level.cam_x + TOP_W / 2), y - (level.cam_y + TOP_H / 2));
    float g = 1.0f - fminf(d, 700) / 700;
    if (d > 250) g *= fmaxf(0, 1 - (d - 250) / 250);  // audio_emitter_falloff(250, 500, 1)
    return g;
}

// ------------------------------------------------------------------ obj_marijuna_crystalcontroller

static MapObj *controller(void) { return mapobj_find(OBJ_MARIJUNA_CRYSTALCONTROLLER, 0); }

// swapControls(sec, xx, yy). The GML swaps global.KeyLeft/Right and KeyUp/Down until Alarm_0
// and draws an inverted-colour circle growing from (xx, yy) with shd_inverse. Neither the key
// swap (player input, see the report) nor the shader is available here; the timer, the sound
// and the "boohoo" bubble are.
static void swap_controls(float sec)
{
    MapObj *c = controller();
    if (!c || c->alarm[0] > 0) return;
    if (level.has_player) player_sound(&level.player, SND_BOOHOO);
    else audio_play(SND_BOOHOO);
    c->alarm[0] = (int)(sec * 60);
    player_swap_dirs = true;
}

static void controller_create(MapObj *o) { o->depth = -1; }

static void controller_alarm(MapObj *o, int n)
{
    (void)o;
    if (n == 0) player_swap_dirs = false;  // the keys are restored
}

static void controller_step(MapObj *o)
{
    (void)o;
    if (!level.has_player) return;
    if (mj.level_state == 1 && !mapobj_number(OBJ_MARJIUNA_STATIC)) {
        int n = mapobj_number(OBJ_MARIJUNA_GUKO);
        MapObj *g = n > 0 ? mapobj_find(OBJ_MARIJUNA_GUKO, rand() % n) : NULL;
        if (g) *MAPOBJ_VARS(g, float) = 2;  // index = 2
        MapObj *s = mapobj_create(OBJ_MARJIUNA_STATIC, 0, 0);
        if (s) s->depth = -501;
    }
}

static void controller_draw(MapObj *o) { (void)o; }

static void controller_draw_gui(MapObj *o)
{
    if (!level.has_player || o->alarm[0] <= 0) return;
    const Player *p = &level.player;
    float off = 20;
    switch (p->character) {
    case CHARACTER_SALLY:
    case CHARACTER_KNUX:
    case CHARACTER_AMY: off = 25; break;
    case CHARACTER_EGGMAN: off = 36; break;
    }
    sprite_draw(SPR_MARIJUNA_BOOHOO, 0, ceilf(p->x - level.cam_x), ceilf(p->y - level.cam_y) - off, 1, 1, 0,
                0xFFFFFFFF, 1);
}

// ------------------------------------------------------------------ obj_marjiuna_static

static void static_create(MapObj *o)
{
    *MAPOBJ_VARS(o, float) = 0;  // fade
    o->depth = -500;
}

static void static_draw(MapObj *o)
{
    float *fade = MAPOBJ_VARS(o, float);
    if (*fade < 1) *fade += 0.016f;
    else *fade = 1;
    const SpriteInfo *s = sprite_info(SPR_STATIC);
    float xs = s && s->width ? TOP_W / (float)s->width : 1, ys = s && s->height ? TOP_H / (float)s->height : 1;
    sprite_draw(SPR_STATIC, o->image_index, (s ? s->xorigin : 0) * xs, (s ? s->yorigin : 0) * ys, xs, ys, 0,
                0xFFFFFFFF, *fade * 0.1f);
}

// ------------------------------------------------------------------ obj_marijuna_crystal

typedef struct {
    bool activated;
    float fade;
} Crystal;

static void crystal_step(MapObj *o)
{
    Crystal *c = MAPOBJ_VARS(o, Crystal);
    if (c->activated) {
        c->fade = fminf(c->fade + 0.016f, 1);
        if (level.has_player && c->fade >= 1 && mapobj_meets_player(o)) swap_controls(5);
    } else {
        c->fade = fmaxf(c->fade - 0.016f, 0);
    }
}

static void crystal_draw(MapObj *o)
{
    Crystal *c = MAPOBJ_VARS(o, Crystal);
    float x = o->x - level.cam_x, y = o->y - level.cam_y;
    sprite_draw(SPR_MARIJUNA_CRYSTAL, 0, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    sprite_draw(SPR_MARIJUNA_CRYSTAL2, current_time_ms() / 100, x, y, 1, 1, 0, 0xFFFFFFFF, c->fade);
}

// ------------------------------------------------------------------ obj_marijuna_lavaplatform

typedef struct {
    int timer, state;
} Lava;

static void lava_stop_loop(void)
{
    if (mj.lava_loop >= 0) audio_stop(mj.lava_loop);
    mj.lava_loop = -1;
}

static void lava_step(MapObj *o)
{
    Lava *lv = MAPOBJ_VARS(o, Lava);
    // scr_audio_play_3d(emitter, snd_lava, true): the loop follows the distance while audible
    float gain = emitter_gain(o->x, o->y);
    if (gain > 0.05f && mj.lava_loop < 0) mj.lava_loop = audio_play_ex(SND_LAVA, gain, true);
    else if (gain <= 0.02f && mj.lava_loop >= 0) lava_stop_loop();
    else if (mj.lava_loop >= 0) audio_set_gain(mj.lava_loop, gain);

    if (lv->timer++ >= 60 * 0.3f) {
        if (lv->state == 2 || lv->state == 3) maps_sound_at(SND_LAVAAPPEAR, o->x, o->y);
        lv->timer = 0;
    }
    if (!level.has_player) return;
    Player *p = &level.player;
    if (p->y + 16 >= o->y + 48 && p->y <= o->y + 672 && p->x + 10 >= o->x + 48 && p->x <= o->x + 113) {
        float dx = p->x - (o->x + 80);
        player_hurt_snd(p, 20, ((dx > 0) - (dx < 0)) * 4.0f, -2, SND_LAVAHIT);
    }
}

static void lava_destroy(MapObj *o)
{
    (void)o;
    lava_stop_loop();
}

// place_meeting(x, y, obj_player_sensorBL / BR): a sensor point inside the platform's box
static bool sensor_on(const MapObj *o, float sx, float sy)
{
    const SpriteInfo *s = sprite_info(o->mask >= 0 ? o->mask : o->sprite);
    if (!s) return false;
    float l = o->x + (s->bbox_left - s->xorigin) * o->xscale, r = o->x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
    float t = o->y + (s->bbox_top - s->yorigin) * o->yscale, b = o->y + (s->bbox_bottom + 1 - s->yorigin) * o->yscale;
    return sx >= fminf(l, r) && sx < fmaxf(l, r) && sy >= fminf(t, b) && sy < fmaxf(t, b);
}

// ------------------------------------------------------------------ obj_marijuna_judger

typedef struct {
    int state, timer;
} Judger;

static void judger_step(MapObj *o)
{
    Judger *j = MAPOBJ_VARS(o, Judger);
    o->image_index = j->state > 0;
    if (j->state != 2) return;
    if (j->timer % 6 == 0) {
        float xo = o->xscale > 0 ? 30 : -40;
        MapObj *s = mapobj_create(OBJ_MARIJUNA_JUDGERPOR, o->x + xo, o->y - 4);
        if (s) {
            s->depth = o->depth + 1;
            *MAPOBJ_VARS(s, float) = o->xscale;  // dir
        }
    }
    if (j->timer % 15 == 0) maps_sound_at(SND_LAVAHIT, o->x, o->y);
    j->timer++;
}

// ------------------------------------------------------------------ obj_marijuna_judgerpor

typedef struct {
    float dir, vdir;
} Shot;

static void shot_create(MapObj *o)
{
    Shot *s = MAPOBJ_VARS(o, Shot);
    s->dir = 1;
    s->vdir = rnd(-0.3f, 0.3f);
    o->alarm[0] = (int)rnd(30, 50);
}

static void shot_alarm(MapObj *o, int n)
{
    if (n == 0) mapobj_destroy(o);
}

static void shot_step(MapObj *o)
{
    Shot *s = MAPOBJ_VARS(o, Shot);
    o->xscale = s->dir > 0 ? 1 : (s->dir < 0 ? -1 : 0);
    o->x += s->dir * 4;
    o->y += s->vdir;
    if (level.has_player && mapobj_meets_player(o)) player_hurt(&level.player, 20, -level.player.image_xscale * 4, -6);
}

// ------------------------------------------------------------------ obj_marijuna_guko

typedef struct {
    float index, fade;
} Guko;

static void guko_create(MapObj *o)
{
    Guko *g = MAPOBJ_VARS(o, Guko);
    g->index = o->image_index;
    g->fade = 0;
}

static void guko_step(MapObj *o)
{
    Guko *g = MAPOBJ_VARS(o, Guko);
    if (mj.level_state >= 1) g->fade = fminf(g->fade + 0.016f, 1);
    o->image_index = g->index;
    o->image_alpha = 1.0f - g->fade;
}

static void guko_draw(MapObj *o)
{
    Guko *g = MAPOBJ_VARS(o, Guko);
    mapobj_draw_self(o);
    sprite_draw(SPR_MARIJUNA_GUKO2, g->index, o->x - level.cam_x, o->y - level.cam_y, 1, 1, 0, 0xFFFFFFFF, g->fade);
}

// ------------------------------------------------------------------ level

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    MapObj *o;
    switch (type) {
    case SERVER_GAME_SPAWN_RING: {
        // obj_level.state = 1 when the exit ring spawns (game.c handles the packet itself)
        NetReader c = *r;
        if (!pass && rd_u8(&c) == 0 && !c.bad) mj.level_state = 1;
        return false;
    }
    case SERVER_MJCRYSTAL_STATE: {
        if (pass) return true;
        bool state = rd_u8(r) != 0;
        for (int i = 0; (o = mapobj_find(OBJ_MARIJUNA_CRYSTAL, i)); i++) {
            MAPOBJ_VARS(o, Crystal)->activated = state;
            if (state) maps_sound_at(SND_DESTINY, o->x, o->y);
        }
        return true;
    }
    case SERVER_MJJUDGER_STATE: {
        if (pass) return true;
        int state = rd_u8(r);
        for (int i = 0; (o = mapobj_find(OBJ_MARIJUNA_JUDGER, i)); i++) {
            MAPOBJ_VARS(o, Judger)->state = state;
            if (state == 1) maps_sound_at(SND_JUDGER, o->x, o->y);
        }
        return true;
    }
    case SERVER_MJLAVA_STATE: {
        if (pass) return true;
        int state = rd_u8(r);
        float y = rd_f32(r);
        if (r->bad || !(o = mapobj_find(OBJ_MARIJUNA_LAVAPLATFORM, 0))) return true;
        MAPOBJ_VARS(o, Lava)->state = state;
        if ((state == 2 || state == 4) && level.has_player) {
            Player *p = &level.player;
            if (sensor_on(o, p->sBL.x, p->sBL.y) || sensor_on(o, p->sBR.x, p->sBR.y)) p->y = y - 17;
        }
        mapobj_move(o, o->x, y);
        return true;
    }
    default:
        return false;
    }
}

static void leave(void)
{
    lava_stop_loop();
    memset(&mj, 0, sizeof mj);
    mj.lava_loop = -1;
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_MARIJUNA_CRYSTALCONTROLLER, .create = controller_create, .step = controller_step,
      .draw = controller_draw, .draw_gui = controller_draw_gui, .alarm = controller_alarm },
    { .object = OBJ_MARJIUNA_STATIC, .create = static_create, .draw = static_draw },
    { .object = OBJ_MARIJUNA_CRYSTAL, .step = crystal_step, .draw = crystal_draw },
    { .object = OBJ_MARIJUNA_LAVAPLATFORM, .step = lava_step, .destroy = lava_destroy, .dynamic = true },
    { .object = OBJ_MARIJUNA_JUDGER, .step = judger_step },
    { .object = OBJ_MARIJUNA_JUDGERPOR, .create = shot_create, .step = shot_step, .alarm = shot_alarm },
    { .object = OBJ_MARIJUNA_GUKO, .create = guko_create, .step = guko_step, .draw = guko_draw },
    { .object = -1 },
};

const MapModule MAP_MARIJUNA = {
    .room = ROOM_MARIJUNA,
    .objects = OBJECTS,
    .packet = packet,
    .leave = leave,
};

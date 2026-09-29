// room_dotdotdot: obj_dotdotdot_i (background swaps and the darkness overlays driven by
// obj_dotdotdot_trigger / trigger2 / trigger3, music crossfade, speed reset), the ladders
// obj_dotdotdot_shitladder, obj_corpse_eggman and obj_eggstatue.
#include <math.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../game.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

static struct {
    RoomLayer *bg;            // layer "Background"
    float fade, fade2, fade3;
    bool fadeB, state;
    int bgnew;
    bool deep;                // mus_dotdotdot2 should be playing (trigger3 area)
    bool ladder, ladder_prev; // a ladder touched the player this frame / last frame
} dot;

static bool box(const MapObj *o, float *l, float *t, float *r, float *b)
{
    const SpriteInfo *s = sprite_info(o->mask >= 0 ? o->mask : o->sprite);
    if (!s) return false;
    float x0 = o->x + (s->bbox_left - s->xorigin) * o->xscale, x1 = o->x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
    float y0 = o->y + (s->bbox_top - s->yorigin) * o->yscale, y1 = o->y + (s->bbox_bottom + 1 - s->yorigin) * o->yscale;
    *l = fminf(x0, x1);
    *r = fmaxf(x0, x1);
    *t = fminf(y0, y1);
    *b = fmaxf(y0, y1);
    return true;
}

static bool obj_contains(const MapObj *o, float x, float y)
{
    float l, t, r, b;
    return box(o, &l, &t, &r, &b) && x >= l && x < r && y >= t && y < b;
}

// position_meeting(x, y, object)
static bool position_meeting(float x, float y, int object)
{
    for (int i = 0;; i++) {
        MapObj *o = mapobj_find(object, i);
        if (!o) return false;
        if (obj_contains(o, x, y)) return true;
    }
}

// image_speed = 0 for placed instances drawn by level.c
static void freeze_instances(int object)
{
    for (int i = 0; i < level.room.layer_count; i++) {
        RoomLayer *l = &level.room.layers[i];
        if (l->type != LAYER_INSTANCES) continue;
        for (int k = 0; k < l->count; k++)
            if (l->instances[k].object == object) l->instances[k].image_speed = 0;
    }
}

// ------------------------------------------------------------------ obj_dotdotdot_i

static void i_create(MapObj *o)
{
    dot.fade = 0;
    dot.fadeB = false;
    dot.bgnew = BACKGROUND_DOTDOTDOT;
    dot.state = true;
    dot.bg = layer_find("Background");
    dot.fade2 = dot.fade3 = 0;
    dot.deep = false;
    o->depth = -999;
    // mus_dotdotdot is the level track; maps_music swaps in mus_dotdotdot2 (audio crossfade)
}

static void set_bg_alpha(float a)
{
    if (!dot.bg) return;
    u32 al = (u32)(fminf(fmaxf(a, 0), 1) * 255);
    dot.bg->bg_colour = (dot.bg->bg_colour & 0xFFFFFF) | (al << 24);
}

static void i_step(MapObj *o)
{
    // The GML samples the triggers at the view centre
    o->x = level.cam_x + TOP_W / 2;
    o->y = level.cam_y + TOP_H / 2;

    if (dot.state) {
        if (dot.fade > 0) dot.fade -= 0.05f;
        else {
            if (dot.bg) dot.bg->bg_sprite = dot.bgnew;
            dot.state = false;
        }
    } else {
        if (dot.fade < 1) dot.fade += 0.05f;
        else dot.fadeB = false;
    }
    set_bg_alpha(dot.fade);

    int cur = dot.bg ? dot.bg->bg_sprite : -1;
    if (position_meeting(o->x, o->y, OBJ_DOTDOTDOT_TRIGGER3)) {
        if (!dot.fadeB && cur != BACKGROUND_DOTDOTDOT3) {
            dot.deep = true;  // audio_sound_gain(mus_dotdotdot2, 1, 2000) / (mus_dotdotdot, 0, 2000)
            dot.fadeB = dot.state = true;
            dot.bgnew = BACKGROUND_DOTDOTDOT3;
        }
        return;
    }
    if (position_meeting(o->x, o->y, OBJ_DOTDOTDOT_TRIGGER2)) {
        if (!dot.fadeB && cur != BACKGROUND_DOTDOTDOT2) {
            dot.fadeB = dot.state = true;
            dot.bgnew = BACKGROUND_DOTDOTDOT2;
        }
        return;
    }
    if (position_meeting(o->x, o->y, OBJ_DOTDOTDOT_TRIGGER)) {
        if (!dot.fadeB && cur != BACKGROUND_DOTDOTDOT) {
            dot.deep = false;
            dot.fadeB = dot.state = true;
            dot.bgnew = BACKGROUND_DOTDOTDOT;
        }
    }
}

// Draw GUI: the darkness overlays (spr_screenoverlay2 frames 0 / 1, 480x270 -> 400x240)
static void i_draw_gui(MapObj *o)
{
    (void)o;
    if (dot.bgnew == BACKGROUND_DOTDOTDOT) {
        if (dot.fade3 > 0) dot.fade3 -= 0.025f;
        if (dot.fade2 > 0) dot.fade2 -= 0.025f;
    } else if (dot.bgnew == BACKGROUND_DOTDOTDOT2) {
        if (dot.fade3 > 0) dot.fade3 -= 0.025f;
        if (dot.fade2 < 1) dot.fade2 += 0.025f;
    } else if (dot.bgnew == BACKGROUND_DOTDOTDOT3) {
        if (dot.fade3 < 1) dot.fade3 += 0.025f;
        if (dot.fade2 > 0) dot.fade2 -= 0.025f;
    }
    const SpriteInfo *s = sprite_info(SPR_SCREENOVERLAY2);
    if (!s) return;
    float xs = TOP_W / (float)s->width, ys = TOP_H / (float)s->height;
    if (dot.fade2 > 0) sprite_draw_smooth(SPR_SCREENOVERLAY2, 0, s->xorigin * xs, s->yorigin * ys, xs, ys, 0xFFFFFFFF, fminf(dot.fade2, 1));
    if (dot.fade3 > 0) sprite_draw_smooth(SPR_SCREENOVERLAY2, 1, s->xorigin * xs, s->yorigin * ys, xs, ys, 0xFFFFFFFF, fminf(dot.fade3, 1));
}

int maps_music(int room, int music)
{
    if (room == ROOM_DOTDOTDOT && music == SND_MUS_DOTDOTDOT && dot.deep) return SND_MUS_DOTDOTDOT2;
    return music;
}

static void i_destroy(MapObj *o)
{
    (void)o;
    audio_music_gain(1);
}

// ------------------------------------------------------------------ obj_dotdotdot_shitladder

typedef struct {
    bool coll;
} Ladder;

static void ladder_step(MapObj *o)
{
    Ladder *v = MAPOBJ_VARS(o, Ladder);
    v->coll = false;
    if (!level.has_player) return;
    Player *p = &level.player;
    if (obj_contains(o, p->sL.x, p->sL.y) || obj_contains(o, p->sR.x, p->sR.y)) {
        if (!p->isSlow) {
            if (p->xspd > 7) p->xspd = p->gspd = 7;
            if (p->xspd < -7) p->xspd = p->gspd = -7;
            p->maxHSpeed = 7;
        }
        v->coll = true;
        dot.ladder = true;
    }
}

// Draw_72 (Pre-Draw): slower walk animation on the ladder. The player's Pre-Draw already ran
// in player_step; its image_speed drives the next frame's advance.
static void ladder_end_step(MapObj *o)
{
    if (!MAPOBJ_VARS(o, Ladder)->coll || !level.has_player) return;
    if (level.player.state == ST_WALK) level.player.image_speed /= 2.5f;
}

static void ladder_draw(MapObj *o) { (void)o; }  // Draw_0 is empty

// ------------------------------------------------------------------ obj_corpse (obj_corpse_eggman)

typedef struct {
    int chr;
    bool exists;
} Corpse;

static void corpse_create(MapObj *o)
{
    Corpse *v = MAPOBJ_VARS(o, Corpse);
    switch (o->object) {
    case OBJ_CORPSE_TAILS: v->chr = CHARACTER_TAILS; break;
    case OBJ_CORPSE_KNUX: v->chr = CHARACTER_KNUX; break;
    case OBJ_CORPSE_EGGMAN: v->chr = CHARACTER_EGGMAN; break;
    case OBJ_CORPSE_AMY: v->chr = CHARACTER_AMY; break;
    case OBJ_CORPSE_CREAM: v->chr = CHARACTER_CREAM; break;
    default: v->chr = 0; break;
    }
    v->exists = false;
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (net.players[i].used && net.players[i].character == v->chr) v->exists = true;
}

static bool any_puppet(void)
{
    u16 id;
    bool demon;
    for (int c = CHARACTER_EXE; c <= CHARACTER_SALLY; c++)
        if (game_puppet_of(c, &id, &demon)) return true;
    return false;
}

static void corpse_step(MapObj *o)
{
    Corpse *v = MAPOBJ_VARS(o, Corpse);
    if (o->visible) return;
    if (net.my_character == v->chr) return;
    if (!any_puppet() || v->exists) return;
    o->visible = true;
}

// ------------------------------------------------------------------ level

static void init(void) { freeze_instances(OBJ_EGGSTATUE); }  // obj_eggstatue Step: image_speed = 0

static void step(void)
{
    // obj_dotdotdot_i Begin Step resets acc / maxHSpeed every frame when not slowed; the only
    // thing in this room that changes them is the ladder, so restore them when leaving one
    // (a per-frame reset would run after the character's Step here and undo its boosts).
    if (level.has_player && dot.ladder_prev && !dot.ladder && !level.player.isSlow && level.player.def) {
        level.player.maxHSpeed = level.player.def->base_maxspeed;
        level.player.acc = level.player.def->base_acc;
    }
    dot.ladder_prev = dot.ladder;
    dot.ladder = false;
}

static void leave(void)
{
    audio_music_gain(1);
    memset(&dot, 0, sizeof dot);
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_DOTDOTDOT_I, .create = i_create, .step = i_step, .draw_gui = i_draw_gui, .destroy = i_destroy },
    { .object = OBJ_DOTDOTDOT_SHITLADDER, .step = ladder_step, .end_step = ladder_end_step, .draw = ladder_draw },
    // triggers: instances only, sampled with position_meeting
    { .object = OBJ_DOTDOTDOT_TRIGGER },
    { .object = OBJ_DOTDOTDOT_TRIGGER2 },
    { .object = OBJ_DOTDOTDOT_TRIGGER3 },
    { .object = OBJ_CORPSE, .create = corpse_create, .step = corpse_step },
    { .object = -1 },
};

const MapModule MAP_DOTDOTDOT = {
    .room = ROOM_DOTDOTDOT,
    .objects = OBJECTS,
    .init = init,
    .step = step,
    .packet = NULL,
    .leave = leave,
};

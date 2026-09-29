// room_weedzone (Wood Dream): obj_weed (fog layer, the arms that grab survivors who stay away
// from a lit lantern, the ghost spawner), obj_weed_lantern / 2 / 3 with obj_weed_light and
// obj_surv_lampindicator (SERVER_WDLATERN_ACTIVATE), the conveyor belts obj_weed_convejor / 2 / 3,
// the background ghosts obj_weed_ghost / 2, obj_weed_zone (no fog inside) and
// obj_weed_selfinsert. obj_am_hideobject is a plain hiding place (WC_HIDING, empty events).
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

#define MAX_GHOSTS 16
// The GML GUI is 480x270; GUI sprites are scaled to the 400x240 top screen.
#define GUI_SX (TOP_W / 480.0f)
#define GUI_SY (TOP_H / 270.0f)

typedef struct {
    bool used;
    int object;                // OBJ_WEED_GHOST (layer Ghost1) or OBJ_WEED_GHOST2 (layer Ghost2)
    float dir, rand, xx, yy, pY;
} Ghost;

static struct {
    int level_state;           // obj_level.state (1 once the exit ring spawned)
    RoomLayer *fog;            // "Foguis"
    float fade, scroll;
    bool weed;
    float weed_anim, weed_progress;
    Ghost ghosts[MAX_GHOSTS];
} wz;

static float current_time_ms(void) { return level.time * 1000.0f / 60; }
static float rnd(float a, float b) { return a + (b - a) * (rand() % 10000) / 10000.0f; }
static int irnd(int a, int b) { return a + rand() % (b - a + 1); }

// Box of an object's mask in the room (bbox_left .. bbox_right + 1).
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

// distance_to_object(global.player), approximated from the player's origin
static float dist_to_player(const MapObj *o)
{
    float l, t, r, b;
    const Player *p = &level.player;
    if (!box(o, &l, &t, &r, &b)) return hypotf(o->x - p->x, o->y - p->y);
    float dx = fmaxf(fmaxf(l - p->x, 0), p->x - r), dy = fmaxf(fmaxf(t - p->y, 0), p->y - b);
    return sqrtf(dx * dx + dy * dy);
}

static float spr_h(const MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    return s ? s->height * o->yscale : 0;
}

static float spr_w(const MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    return s ? s->width * o->xscale : 0;
}

// Near a lit lantern: the arms let go and the timer restarts.
static void weed_reset(void)
{
    MapObj *w = mapobj_find(OBJ_WEED, 0);
    wz.weed = false;
    if (w) w->alarm[0] = (int)((20 + rnd(1, 2)) * 60);
}

// ------------------------------------------------------------------ obj_weed

static void weed_create(MapObj *o)
{
    wz.fade = 0;
    wz.weed = false;
    wz.weed_anim = -1;
    wz.weed_progress = 0;
    wz.scroll = 0;
    wz.fog = layer_find("Foguis");
    o->depth = -400;
    o->alarm[1] = irnd(5, 7) * 60;
    // The GML only sets it for survivors; Alarm_0 checks the character again.
    o->alarm[0] = (int)((20 + rnd(1, 2)) * 60);
}

static void spawn_ghost(int object, float dir, float xx, float y)
{
    for (int i = 0; i < MAX_GHOSTS; i++) {
        Ghost *g = &wz.ghosts[i];
        if (g->used) continue;
        *g = (Ghost){ .used = true, .object = object, .dir = dir, .rand = rnd(0, 2000), .xx = xx, .yy = y, .pY = y };
        return;
    }
}

static void weed_alarm(MapObj *o, int n)
{
    if (n == 0) {
        if (!level.has_player) return;
        const Player *p = &level.player;
        if (p->character == CHARACTER_EXE || p->hp <= 0 || p->revivalTimes >= 2) return;
        audio_play(SND_WDARMS);
        wz.weed = true;
    } else if (n == 1) {
        float y = (float)irnd(0, 200);
        switch (irnd(0, 4)) {
        case 0: spawn_ghost(OBJ_WEED_GHOST2, rnd(0.5f, 1), -64, y); break;
        case 1: spawn_ghost(OBJ_WEED_GHOST, rnd(0.5f, 1), -64, y); break;
        case 2: spawn_ghost(OBJ_WEED_GHOST2, -rnd(0.5f, 1), TOP_W + 64, y); break;
        case 3: spawn_ghost(OBJ_WEED_GHOST, -rnd(0.5f, 1), TOP_W + 64, y); break;
        }
        o->alarm[1] = irnd(5, 7) * 60;
    }
}

static void weed_step(MapObj *o)
{
    (void)o;
    // No fog while the view centre is inside an obj_weed_zone
    float cx = level.cam_x + TOP_W / 2, cy = level.cam_y + TOP_H / 2;
    bool inside = false;
    MapObj *z;
    for (int i = 0; !inside && (z = mapobj_find(OBJ_WEED_ZONE, i)); i++) {
        float l, t, r, b;
        inside = box(z, &l, &t, &r, &b) && cx >= l && cx < r && cy >= t && cy < b;
    }
    if (inside) {
        if (wz.fade > 0) wz.fade -= 0.016f;
    } else if (wz.fade < 1) {
        wz.fade += 0.016f;
    }
    if (!level.has_player || level.player.isDead || level.player.revivalTimes >= 2) wz.weed = false;
    if (wz.fog) {
        float a = fminf(fmaxf(wz.fade, 0), 1);
        wz.fog->bg_colour = (wz.fog->bg_colour & 0xFFFFFF) | ((u32)(a * 255) << 24);
    }
}

// Draw_72: the fog scrolls with the camera (the layer's own hspeed is cancelled out, the GML
// sets the position every frame)
static void weed_end_step(MapObj *o)
{
    (void)o;
    if (!wz.fog) return;
    layer_set_pos(wz.fog, level.cam_x + wz.scroll - level.time * wz.fog->hspeed, level.cam_y);
    wz.scroll += 2;
    // obj_weed_ghost / 2 Draw_72
    for (int i = 0; i < MAX_GHOSTS; i++) {
        Ghost *g = &wz.ghosts[i];
        if (!g->used) continue;
        g->xx += g->dir;
        g->yy = g->pY + sinf(current_time_ms() / 300 + g->rand) * 10;
        const SpriteInfo *s = sprite_info(g->object == OBJ_WEED_GHOST ? SPR_WEED_PRIZRAKS : SPR_WEED_PRIZRAKS2);
        float w = s ? s->width : 0;
        if ((g->dir > 0 && g->xx >= TOP_W + w + 64) || (g->dir < 0 && g->xx <= -128)) g->used = false;
    }
}

static void weed_draw_gui(MapObj *o)
{
    (void)o;
    if (!level.has_player || level.player.character == CHARACTER_EXE) return;
    const SpriteInfo *arms = sprite_info(SPR_WEED_ARMS);
    float no_frames = (arms ? arms->frame_count : 1) - 1;
    if (wz.weed) {
        if (wz.weed_anim < no_frames) {
            wz.weed_anim += 0.25f;
        } else {
            wz.weed_anim = no_frames;
            if (wz.weed_progress < 1) wz.weed_progress += 0.016f / 30;
            else wz.weed_progress = 1;
        }
    } else {
        if (wz.weed_anim > -1) wz.weed_anim -= 0.25f;
        if (wz.weed_progress > 0) wz.weed_progress -= 0.016f;
        else wz.weed_progress = 0;
    }
    if (wz.weed_anim >= 0) {
        float t = current_time_ms();
        float x = -10 + sinf(t / 300) * 8 + rnd(-2, 2), y = -10 + cosf(t / 300) * 8 + rnd(-2, 2);
        sprite_draw_smooth(SPR_WEED_ARMS, wz.weed_anim, x * GUI_SX, y * GUI_SY, GUI_SX, GUI_SY, 0xFFFFFFFF, 1);
    }
    if (wz.weed_progress > 0)
        sprite_draw_smooth(SPR_WEED_VINJETK, 0, 0, 0, GUI_SX, GUI_SY, 0xFFFFFFFF, wz.weed_progress);
}

// ------------------------------------------------------------------ obj_weed_ghost / 2

// The placed ghosts start flying like spawned ones; every ghost of a type is drawn by the
// placed instance so it keeps its background layer (Ghost1 / Ghost2).
static void ghost_create(MapObj *o) { spawn_ghost(o->object, 1, 0, o->y); }

static void ghost_draw(MapObj *o)
{
    int spr = o->object == OBJ_WEED_GHOST ? SPR_WEED_PRIZRAKS : SPR_WEED_PRIZRAKS2;
    float frame = level.time * sprite_frame_step(spr);
    for (int i = 0; i < MAX_GHOSTS; i++) {
        const Ghost *g = &wz.ghosts[i];
        if (!g->used || g->object != o->object) continue;
        float xs = g->dir > 0 ? 1 : -1;
        if (o->object == OBJ_WEED_GHOST) xs = -xs;
        sprite_draw(spr, frame, g->xx, g->yy, xs, 1, 0, 0xFFFFFFFF, 1);
    }
}

// ------------------------------------------------------------------ obj_weed_light

typedef struct {
    bool show;
} Light;

static void light_draw(MapObj *o) { (void)o; }

static void light_draw_gui(MapObj *o)
{
    if (MAPOBJ_VARS(o, Light)->show) o->image_alpha = fminf(o->image_alpha + 2.0f / 60, 1);
    else o->image_alpha = fmaxf(o->image_alpha - 2.0f / 60, 0);
    sprite_draw(SPR_WEED_LIGHT, 0, o->x - level.cam_x, o->y - level.cam_y, 1, 1, 0, 0xFFFFFFFF, o->image_alpha);
}

// ------------------------------------------------------------------ obj_weed_lantern / 2 / 3

typedef struct {
    bool active;
    float frame, targetX, targetY;
    MapObj *light;
} Lantern;

static MapObj *make_light(float x, float y)
{
    MapObj *l = mapobj_create(OBJ_WEED_LIGHT, x, y);
    if (!l) return NULL;
    l->depth = -401;
    l->image_alpha = 0;
    MAPOBJ_VARS(l, Light)->show = false;
    return l;
}

static void lantern_create(MapObj *o)
{
    Lantern *L = MAPOBJ_VARS(o, Lantern);
    o->nid = (int)mapobj_prop(o, "nid", 0);
    L->active = false;
    L->frame = 0;
    L->targetX = o->x;
    L->targetY = o->y + spr_w(o) / 2;
    float ly = o->object == OBJ_WEED_LANTERN ? o->y + spr_h(o) / 2 : o->y + spr_h(o) + 33 / 2.0f;
    L->light = make_light(o->x, ly);
}

static void set_light(Lantern *L)
{
    if (L->light && L->light->used && !L->light->destroyed) MAPOBJ_VARS(L->light, Light)->show = L->active;
}

static void lantern_step(MapObj *o)
{
    Lantern *L = MAPOBJ_VARS(o, Lantern);
    set_light(L);
    if (L->active) {
        o->image_index += 0.16f;
        if (o->image_index >= mapobj_frames(o) - 1) o->image_index = 1;
    } else {
        o->image_index = 0;
    }
    if (level.has_player && L->active && dist_to_player(o) < 80) weed_reset();
}

static void lantern2_step(MapObj *o)
{
    Lantern *L = MAPOBJ_VARS(o, Lantern);
    float h = spr_h(o), a = o->angle * (float)M_PI / 180;
    L->targetX = o->x + sinf(a) * h;
    L->targetY = o->y + cosf(a) * h;
    set_light(L);
    if (L->active) {
        L->frame += 0.16f;
        if (L->frame >= 4 - 1) L->frame = 1;
    } else {
        L->frame = 0;
    }
    if (level.has_player && L->active) {
        const Player *p = &level.player;
        if (hypotf(L->targetX - p->x, L->targetY + 33 / 2.0f - p->y) < 80) weed_reset();
    }
    o->angle = ((sinf(current_time_ms() / 400) + 1) / 2.0f) * 4;
}

static void lantern2_draw(MapObj *o)
{
    Lantern *L = MAPOBJ_VARS(o, Lantern);
    mapobj_draw_self(o);
    float x = L->targetX - level.cam_x, y = L->targetY - level.cam_y;
    // draw_sprite_ext(..., rot = image_alpha, ...) in the GML: a 1 degree tilt
    sprite_draw(SPR_WEED_LATERN, L->frame, x, y, 1, 1, o->image_alpha, 0xFFFFFFFF, 1);
    if (L->active) sprite_draw(SPR_WEED_LIGHT, current_time_ms() / 500, x, y + 33 / 2.0f, 1, 1, 0, 0xFFFFFFFF, 1);
}

// ------------------------------------------------------------------ obj_surv_lampindicator

typedef struct {
    float targetX, targetY;
} Indicator;

static void indicator_create(MapObj *o)
{
    o->depth = -200;
    o->image_alpha = 0.7f;
    o->alarm[0] = 20 * 60;
}

static void indicator_alarm(MapObj *o, int n)
{
    if (n == 0) mapobj_destroy(o);
}

static void indicator_step(MapObj *o)
{
    if (!level.has_player || !mapobj_number(OBJ_WEED_LANTERN)) return;
    const Player *p = &level.player;
    if (p->revivalTimes >= 2 || p->isDead) {
        mapobj_destroy(o);
        return;
    }
    Indicator *in = MAPOBJ_VARS(o, Indicator);
    // point_direction: degrees, counter-clockwise, y down
    o->angle = atan2f(-(in->targetY - p->y), in->targetX - p->x) * 180 / (float)M_PI;
}

static void indicator_draw(MapObj *o) { (void)o; }

static void indicator_draw_gui(MapObj *o)
{
    if (!level.has_player) return;
    const Player *p = &level.player;
    sprite_draw(SPR_SINDICATOR3, 0, ceilf(p->x - level.cam_x), ceilf(p->y - level.cam_y), 1, 1, o->angle, 0xFFFFFFFF, 1);
}

// ------------------------------------------------------------------ obj_weed_convejor

static void convejor_step(MapObj *o)
{
    if (!level.has_player) return;
    Player *p = &level.player;
    float l, t, r, b;
    if (!box(o, &l, &t, &r, &b)) return;
    r -= 1;  // bbox_right / bbox_bottom are inclusive
    b -= 1;
    bool touched = (p->sBL.x >= l - 4 && p->sBL.x <= r && p->sBL.y >= t && p->sBL.y <= b) ||
                   (p->sBR.x >= l - 4 && p->sBR.x <= r && p->sBR.y >= t && p->sBR.y <= b);
    if (touched) p->x -= 4;
}

// ------------------------------------------------------------------ obj_weed_selfinsert

static void selfinsert_draw(MapObj *o)
{
    if (wz.level_state >= 1) mapobj_draw_self(o);
    else sprite_draw(SPR_SELFINSERT2, 0, 3968 - level.cam_x, 974 - level.cam_y, 1, 1, 0, 0xFFFFFFFF, 1);
}

// ------------------------------------------------------------------ level

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    if (type == SERVER_GAME_SPAWN_RING) {
        // obj_level.state = 1 when the exit ring spawns (game.c handles the packet itself)
        NetReader c = *r;
        if (!pass && rd_u8(&c) == 0 && !c.bad) wz.level_state = 1;
        return false;
    }
    if (type != SERVER_WDLATERN_ACTIVATE) return false;
    bool state = rd_u8(r) != 0;
    int nid = rd_u8(r);
    if (!mapobj_number(OBJ_WEED) || !mapobj_number(OBJ_WEED_LANTERN)) return true;
    MapObj *o;
    for (int i = 0; (o = mapobj_find(OBJ_WEED_LANTERN, i)); i++) {
        Lantern *L = MAPOBJ_VARS(o, Lantern);
        if (o->nid != nid) {
            L->active = false;
            continue;
        }
        L->active = state;
        if (!state) continue;
        audio_play(SND_WDLAMP);
        if (!level.has_player) break;
        const Player *p = &level.player;
        if (p->character == CHARACTER_EXE || p->revivalTimes >= 2 || p->isDead) break;
        MapObj *ind = mapobj_create(OBJ_SURV_LAMPINDICATOR, p->x, p->y);
        if (ind) {
            MAPOBJ_VARS(ind, Indicator)->targetX = L->targetX;
            MAPOBJ_VARS(ind, Indicator)->targetY = L->targetY;
        }
    }
    return true;
}

static void leave(void) { memset(&wz, 0, sizeof wz); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_WEED, .create = weed_create, .step = weed_step, .end_step = weed_end_step,
      .draw_gui = weed_draw_gui, .alarm = weed_alarm },
    { .object = OBJ_WEED_ZONE },
    { .object = OBJ_WEED_GHOST, .create = ghost_create, .draw = ghost_draw },
    { .object = OBJ_WEED_GHOST2, .create = ghost_create, .draw = ghost_draw },
    { .object = OBJ_WEED_LIGHT, .draw = light_draw, .draw_gui = light_draw_gui },
    { .object = OBJ_WEED_LANTERN, .create = lantern_create, .step = lantern_step },
    { .object = OBJ_WEED_LANTERN2, .create = lantern_create, .step = lantern2_step, .draw = lantern2_draw },
    { .object = OBJ_WEED_LANTERN3, .create = lantern_create, .step = lantern2_step, .draw = lantern2_draw },
    { .object = OBJ_SURV_LAMPINDICATOR, .create = indicator_create, .step = indicator_step, .draw = indicator_draw,
      .draw_gui = indicator_draw_gui, .alarm = indicator_alarm },
    { .object = OBJ_WEED_CONVEJOR, .step = convejor_step },
    { .object = OBJ_WEED_SELFINSERT, .draw = selfinsert_draw },
    { .object = -1 },
};

const MapModule MAP_WEEDZONE = {
    .room = ROOM_WEEDZONE,
    .objects = OBJECTS,
    .packet = packet,
    .leave = leave,
};

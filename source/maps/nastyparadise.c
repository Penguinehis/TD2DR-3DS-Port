// room_nastyparadise: obj_nap_controller (snow), obj_nap_trigger, obj_nap_snowball(+_part,
// _waypoint) with SERVER_NAPBALL_STATE, obj_nap_iceblock(+_part) with SERVER_NAPICE_STATE /
// CLIENT_NAPICE_ACTIVATE, and the ice block bounce from scr_collision_objects_before.
#include <math.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../entities.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"
#include "../settings.h"

#define KNUX_GLIDE (ST_BALANCING + 3)  // chars/knux.c
#define MAX_SNOW 640

// Optional entities.c hook: the local player's own Tails projectile overlaps the rectangle
// (instance_place(x, y, obj_tails_projectile) && isOwner). Weak until entities.c provides it.
extern bool ents_own_projectile_meets(float l, float t, float r, float b) __attribute__((weak));

typedef struct {
    float x, y, spd;
    int life;
} Flake;

static struct {
    bool optimize;
    int snow_sprite;
    Flake snow[MAX_SNOW];
    int snow_count;
} nap;

static float rnd(float a, float b) { return a + (b - a) * (rand() % 10000) / 10000.0f; }
static int irnd(int a, int b) { return a + rand() % (b - a + 1); }

static bool bbox(const MapObj *o, float *l, float *t, float *r, float *b)
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

// ------------------------------------------------------------------ obj_nap_controller

static void controller_create(MapObj *o)
{
    (void)o;
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    nap.optimize = !new3ds;  // the GML thins the snow on Android; here on Old 3DS
    const ObjectInfo *snow = object_info(OBJ_NAP_SNOW);
    nap.snow_sprite = snow ? snow->sprite : SPR_NAP_SNOW;
    nap.snow_count = 0;
}

static void spawn_flake(float x, float y)
{
    if (nap.snow_count >= MAX_SNOW) return;
    Flake *f = &nap.snow[nap.snow_count++];
    f->x = x;
    f->y = y;
    f->spd = rnd(-1, 1);
    f->life = 60 * 2;  // alarm[0]
}

static bool player_in_trigger(void)
{
    for (int i = 0;; i++) {
        MapObj *t = mapobj_find(OBJ_NAP_TRIGGER, i);
        if (!t) return false;
        if (mapobj_meets_player(t)) return true;
    }
}

static void controller_step(MapObj *o)
{
    (void)o;
    // obj_nap_snow Step / Alarm_0 (runs even when no new flakes spawn)
    for (int i = 0; i < nap.snow_count;) {
        Flake *f = &nap.snow[i];
        f->y += 2 + f->spd;
        f->x -= 3 + f->spd;
        if (--f->life <= 0) *f = nap.snow[--nap.snow_count];
        else i++;
    }
    if (!level.has_player || player_in_trigger()) return;
    // obj_camera is the top-left of a 480x270 view; centre it on the 3DS view
    float cx = level.cam_x - 40, cy = level.cam_y - 15;
    if (settings.gfx_weather) {
        for (int i = 0; i < (nap.optimize ? 1 : 3); i++) spawn_flake(cx + rnd(-120, 560), cy - 1);
        for (int i = 0; i < (nap.optimize ? 1 : 2); i++) spawn_flake(cx + 481, cy + rnd(-120, 350));
    }
}

// ------------------------------------------------------------------ obj_nap_iceblock

typedef struct {
    bool sent;   // CLIENT_NAPICE_ACTIVATE sent since the block last spawned
} Ice;

static void ice_create(MapObj *o) { o->depth = -2; }

static void ice_activate(MapObj *o)
{
    Ice *v = MAPOBJ_VARS(o, Ice);
    if (v->sent) return;  // the GML resends every frame; the server ignores repeats
    v->sent = true;
    NetPacket pk;
    pkt_begin(&pk, CLIENT_NAPICE_ACTIVATE);
    pkt_u8(&pk, (u8)o->nid);
    net_send(&pk, true);
}

static bool ice_point(const MapObj *o, float px, float py)
{
    if (o->world) return !o->world->disabled && inst_contains_point(o->world, px, py);
    float l, t, r, b;
    return bbox(o, &l, &t, &r, &b) && px >= l && px < r && py >= t && py < b;
}

static void ice_step(MapObj *o)
{
    bool broken = o->sprite == SPR_NAP_ICEBLOCK3;  // empty mask
    if (!level.has_player) return;
    Player *p = &level.player;

    // scr_collision_objects_before (player code in the GML)
    if (!p->isDead && !broken) {
        float l, t, r, b;
        if (bbox(o, &l, &t, &r, &b) && p->y + 19 > t &&
            (ice_point(o, p->sBL.x, p->sBL.y + 2 + p->yspd) || ice_point(o, p->sBR.x, p->sBR.y + 2 + p->yspd))) {
            p->yspd = -5;
            p->isJumping = true;
            p->isGrounded = false;
            ice_activate(o);
        }
        if (p->isAttacking && mapobj_meets_player(o)) ice_activate(o);
    }

    if (!o->visible) return;
    if (o->sprite == SPR_NAP_ICEBLOCK2 && o->image_index >= mapobj_frames(o) - 1) o->sprite = SPR_NAP_ICEBLOCK;
    float l, t, r, b;
    if (ents_own_projectile_meets && bbox(o, &l, &t, &r, &b) && ents_own_projectile_meets(l, t, r, b)) ice_activate(o);
    if (mapobj_meets_player(o) && p->character == CHARACTER_KNUX && p->state == KNUX_GLIDE) ice_activate(o);
}

// ------------------------------------------------------------------ obj_nap_iceblock_part

typedef struct {
    float xspd, yspd, dir;
} IcePart;

static void icepart_step(MapObj *o)
{
    IcePart *v = MAPOBJ_VARS(o, IcePart);
    v->yspd += 0.2f;
    v->xspd += v->dir * 0.05f;
    o->x += v->xspd;
    o->y += v->yspd;
    float l, t, r, b;
    if (bbox(o, &l, &t, &r, &b)) {
        WorldInst *w = world_rect(l, t, r, b, WC_FLOOR);
        if (w) {
            const ObjectInfo *info = object_info(w->object);
            if (info && (info->flags & OBJF_VISIBLE)) mapobj_destroy(o);
            return;
        }
    }
    // The GML parts only stop on visible floors and otherwise fall forever
    if (o->y > (float)level.room.height + 64) mapobj_destroy(o);
}

// ------------------------------------------------------------------ obj_nap_snowball

typedef struct {
    int timer;
    bool doDestroy;
} Snowball;

static void snowball_create(MapObj *o)
{
    o->depth = 102;
    o->image_alpha = 0;
}

static void snowball_step(MapObj *o)
{
    Snowball *v = MAPOBJ_VARS(o, Snowball);
    if (o->image_alpha < 1) o->image_alpha += 0.016f;
    if (!level.has_player) return;
    if (v->doDestroy && !audio_is_playing(SND_SNOWBALL_BREAK)) mapobj_destroy(o);
    if (v->doDestroy) return;
    // camera shake near the rolling ball: omitted
    Player *p = &level.player;
    if (o->image_index >= 8 && mapobj_meets_player(o)) {
        p->isSlow = true;
        p->alarm4 = 60 * 3;
        player_hurt(p, 20, -p->image_xscale * 4, -6);
    }
    if (++v->timer >= 30) {
        maps_sound_at(SND_SNOWBALL_ROLL, o->x, o->y);
        v->timer = 0;
    }
}

// ------------------------------------------------------------------ obj_nap_snowball_part

typedef struct {
    float xspd, yspd;
    bool doDestroy;
} SnowPart;

static void snowpart_create(MapObj *o)
{
    o->depth = -4;
    o->image_speed = 0;
    o->alarm[0] = (int)(60 * (1 + rnd(0, 0.5f)));
}

static void snowpart_alarm(MapObj *o, int n)
{
    if (n == 0) MAPOBJ_VARS(o, SnowPart)->doDestroy = true;
}

static void snowpart_step(MapObj *o)
{
    SnowPart *v = MAPOBJ_VARS(o, SnowPart);
    v->yspd += 0.12f;
    v->xspd -= (v->xspd > 0 ? 1 : v->xspd < 0 ? -1 : 0) * 0.04f;
    o->x += v->xspd;
    o->y += v->yspd;
    o->angle += 1;
    if (v->doDestroy) {
        o->image_alpha -= 0.05f;
        if (o->image_alpha <= 0) mapobj_destroy(o);
    }
}

// ------------------------------------------------------------------ level

static void draw_front(void)
{
    for (int i = 0; i < nap.snow_count; i++) {
        float x = nap.snow[i].x - level.cam_x, y = nap.snow[i].y - level.cam_y;
        if (x < -4 || y < -4 || x > TOP_W + 4 || y > TOP_H + 4) continue;
        sprite_draw(nap.snow_sprite, 0, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    }
}

static MapObj *waypoint(int nid, int wid)
{
    for (int i = 0;; i++) {
        MapObj *w = mapobj_find(OBJ_NAP_SNOWBALL_WAYPOINT, i);
        if (!w) return NULL;
        if (w->nid == nid && (int)mapobj_prop(w, "wid", 0) == wid) return w;
    }
}

static MapObj *snowball(int nid)
{
    for (int i = 0;; i++) {
        MapObj *s = mapobj_find(OBJ_NAP_SNOWBALL, i);
        if (!s) return NULL;
        if (s->nid == nid) return s;
    }
}

static void snowball_break(MapObj *s)
{
    float dir = (s->nid == 4 || s->nid == 1) ? 1 : -1;
    int ran = irnd(0, 2);
    for (int i = ran; i < ran + 2; i++) {
        MapObj *part = mapobj_create(OBJ_NAP_SNOWBALL_PART, s->x, s->y - 48);
        if (!part) break;
        part->y += rnd(-1.5f, 1.5f) * 24;
        part->image_index = i;
        MAPOBJ_VARS(part, SnowPart)->xspd = (7 + rnd(-2, 2)) * dir;
        MAPOBJ_VARS(part, SnowPart)->yspd = -4 + rnd(-2, 2);
    }
    int n = irnd(3, 6);
    for (int i = 0; i < n; i++) {
        MapObj *part = mapobj_create(OBJ_NAP_SNOWBALL_PART, s->x, s->y - 48);
        if (!part) break;
        part->y += rnd(-1, 1) * 24;
        part->image_index = irnd(4, 9);
        MAPOBJ_VARS(part, SnowPart)->xspd = (4 + rnd(-2, 2)) * dir;
        MAPOBJ_VARS(part, SnowPart)->yspd = -4 + rnd(-2, 2);
    }
    maps_sound_at(SND_SNOWBALL_BREAK, s->x, s->y);
    MAPOBJ_VARS(s, Snowball)->doDestroy = true;
    s->visible = false;
}

static void napball_packet(NetReader *r, bool reliable)
{
    int state = rd_u8(r), nid = rd_u8(r);
    if (reliable) {
        if (state == 0) {
            s8 dir = (s8)rd_u8(r);
            MapObj *w = waypoint(nid, 0);
            if (!w) return;
            MapObj *s = mapobj_create(OBJ_NAP_SNOWBALL, w->x + 32, w->y + 64);
            if (!s) return;
            s->nid = nid;
            s->xscale = dir;
        } else if (state == 2) {
            for (int i = 0;; i++) {  // every snowball with that id
                MapObj *s = mapobj_find(OBJ_NAP_SNOWBALL, i);
                if (!s) break;
                if (s->nid == nid && !MAPOBJ_VARS(s, Snowball)->doDestroy) snowball_break(s);
            }
        }
    } else if (state == 1) {
        int wp = rd_u8(r), frame = rd_u8(r);
        double prog = rd_f64(r);
        MapObj *first = waypoint(nid, wp), *next = waypoint(nid, wp + 1), *s = snowball(nid);
        if (!first || !next || !s) return;
        s->x = first->x + (float)((next->x - first->x) * prog) + 32;
        s->y = first->y + (float)((next->y - first->y) * prog) + 64;
        s->image_index = frame;
    }
}

static void napice_packet(NetReader *r)
{
    int state = rd_u8(r), nid = rd_u8(r);
    MapObj *ice = mapobj_by_nid(OBJ_NAP_ICEBLOCK, nid);
    if (!ice) return;
    if (state == 0) {
        ice->visible = false;
        ice->sprite = SPR_NAP_ICEBLOCK3;  // empty mask: no collision
        if (ice->world) ice->world->disabled = true;
        maps_sound_at(SND_ICE_BREAK, ice->x, ice->y);
        for (int side = 0; side < 2; side++)
            for (int i = 0; i < 2; i++) {
                MapObj *part = mapobj_create(OBJ_NAP_ICEBLOCK_PART, ice->x + 20, ice->y + i * 20);
                if (!part) continue;
                part->depth = -20;
                IcePart *v = MAPOBJ_VARS(part, IcePart);
                v->xspd = side == 0 ? 1 - i : 1 + i;
                v->yspd = -3;
                v->dir = side == 0 ? -1 : 1;
            }
    } else if (state == 1) {
        maps_sound_at(SND_ICE_SPAWN, ice->x, ice->y);
        ice->sprite = SPR_NAP_ICEBLOCK2;
        ice->image_index = 0;
        ice->visible = true;
        if (ice->world) ice->world->disabled = false;
        MAPOBJ_VARS(ice, Ice)->sent = false;
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    switch (type) {
    case SERVER_NAPBALL_STATE:
        if (!pass) napball_packet(r, reliable);
        return true;
    case SERVER_NAPICE_STATE:
        if (!pass && reliable) napice_packet(r);
        return true;
    default:
        return false;
    }
}

static void leave(void) { memset(&nap, 0, sizeof nap); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_NAP_CONTROLLER, .create = controller_create, .step = controller_step },
    { .object = OBJ_NAP_TRIGGER },
    { .object = OBJ_NAP_SNOWBALL_WAYPOINT },
    { .object = OBJ_NAP_ICEBLOCK, .create = ice_create, .step = ice_step },
    { .object = OBJ_NAP_ICEBLOCK_PART, .step = icepart_step },
    { .object = OBJ_NAP_SNOWBALL, .create = snowball_create, .step = snowball_step },
    { .object = OBJ_NAP_SNOWBALL_PART, .create = snowpart_create, .step = snowpart_step, .alarm = snowpart_alarm },
    { .object = -1 },
};

const MapModule MAP_NASTYPARADISE = {
    .room = ROOM_NASTYPARADISE,
    .objects = OBJECTS,
    .draw_front = draw_front,
    .packet = packet,
    .leave = leave,
};

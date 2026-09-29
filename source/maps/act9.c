// room_act9: obj_act9_wall (the closing walls, SERVER_ACT9WALL_STATE), obj_act9_bodies.
// Music (mus_act9 / mus_act9_chase crossfade) and the black/dark-grey "silhouette" blends of
// the players, puppets, rings and effects belong to game.c / player.c / entities.c.
#include <math.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../entities.h"
#include "../game.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

// Optional game.c hook: hp and revivalTimes of the puppet playing a character (false when no
// puppet plays it). Weak so this file links before game.c provides it.
extern bool game_puppet_status(int character, int *hp, int *revival) __attribute__((weak));

static float current_time_ms(void) { return level.time * 1000.0f / 60; }
static float rnd(float a, float b) { return a + (b - a) * (rand() % 10000) / 10000.0f; }

// GameMaker bbox_left/top/right/bottom (right/bottom inclusive) of an object's mask.
static bool bbox(const MapObj *o, float *l, float *t, float *r, float *b)
{
    const SpriteInfo *s = sprite_info(o->mask >= 0 ? o->mask : o->sprite);
    if (!s) return false;
    float x0 = o->x + (s->bbox_left - s->xorigin) * o->xscale, x1 = o->x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
    float y0 = o->y + (s->bbox_top - s->yorigin) * o->yscale, y1 = o->y + (s->bbox_bottom + 1 - s->yorigin) * o->yscale;
    *l = fminf(x0, x1);
    *r = fmaxf(x0, x1) - 1;
    *t = fminf(y0, y1);
    *b = fmaxf(y0, y1) - 1;
    return true;
}

static bool point_in(const MapObj *o, float px, float py)
{
    if (o->world) return inst_contains_point(o->world, px, py);
    float l, t, r, b;
    return bbox(o, &l, &t, &r, &b) && px >= l && px < r + 1 && py >= t && py < b + 1;
}

static int sgn(float v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

// ------------------------------------------------------------------ obj_act9_wall

static void wall_create(MapObj *o)
{
    // depth = -90: drawn above the player by draw_front
    o->depth = -90;
    o->visible = false;
}

// scr_player_instakill (the wall kills even through Sally's shield, so not via player_hurt)
static void instakill(Player *p)
{
    if (p->isDead) return;
    p->hp = 0;
    p->deadTimer = 31;
    p->rings = 0;
    p->xspd = p->image_xscale * 2;
    p->isGrounded = p->isSpinning = p->isLookingDown = p->isLookingUp = false;
    p->isDead = true;
    player_sound(p, SND_DEAD);
    net_quick_effect(p->x, p->y, SPR_BLOOD2, false, 1, 0, 0, 1);
    if (player_on_death) player_on_death(p);
}

static void crush(Player *p, int dir)
{
    if (!p->isDead && p->character != CHARACTER_EXE && p->revivalTimes < 2) {
        instakill(p);
        player_sound(p, SND_DEAD);
        net_quick_effect(p->x, p->y, SPR_BLOOD2, false, dir, 0, 0, 1);
    }
    p->x = 1535;
    p->y = 943;
    p->gspd = p->xspd = p->yspd = 0;
    p->isHurt = false;
    p->hurttime = 0;
}

// Step_1 (Begin Step)
static void wall_begin_step(MapObj *o)
{
    if (!level.has_player) return;
    Player *p = &level.player;
    float l, t, r, b;
    if (!bbox(o, &l, &t, &r, &b)) return;
    if (o->nid == 1) {
        if (p->x - 8 <= r) p->x = ceilf(r) + 8;
        if (point_in(o, p->sL.x, p->sL.y) && p->sL.coll && p->sR.coll) crush(p, sgn(o->x - p->x));
    } else if (o->nid == 2) {
        if (p->x + 8 >= l) p->x = ceilf(l) - 8;
        if (point_in(o, p->sR.x, p->sR.y) && p->sL.coll && p->sR.coll) crush(p, sgn(r - p->x));
    } else if (o->nid == 0) {
        if (p->y <= b + 16) {
            if (!p->isDead && p->y <= b + 8) crush(p, sgn(r - p->x));
            else p->y = b + 16;
        }
    }
}

// ------------------------------------------------------------------ obj_act9_bodies

typedef struct {
    float sY, sS;
    bool exists;
} Body;

static bool character_in_match(int character)
{
    if (level.has_player && level.player.character == character) return true;
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (net.players[i].used && net.players[i].character == character) return true;
    return false;
}

static void bodies_create(MapObj *o)
{
    Body *v = MAPOBJ_VARS(o, Body);
    v->sY = o->y;
    v->sS = rnd(0, 25);
    o->image_alpha = 0;
    v->exists = character_in_match((int)o->image_index + 1);
}

static void bodies_step(MapObj *o)
{
    Body *v = MAPOBJ_VARS(o, Body);
    o->y = v->sY + sinf(current_time_ms() / 550 + v->sS) * 4;
    int character = (int)o->image_index + 1;
    if (level.has_player && level.player.character == character) {
        if (level.player.revivalTimes != 1 || level.player.hp > 0) return;
        v->exists = false;
    }
    int hp, rev;
    if (game_puppet_status && game_puppet_status(character, &hp, &rev)) {
        if (rev != 1 || hp > 0) return;
        v->exists = false;
    } else if (!game_puppet_status) {
        u16 id;
        bool demon;
        if (game_puppet_of(character, &id, &demon)) return;  // no hp info: treat as alive
    }
    // net_tcpprocess: the body of a player who left the match shows up
    if (v->exists && !character_in_match(character)) v->exists = false;
    if (v->exists) return;
    if (o->image_alpha < 1) o->image_alpha += 0.016f * 0.25f;
}

// ------------------------------------------------------------------ level

static void draw_front(void)
{
    for (int i = 0;; i++) {
        MapObj *w = mapobj_find(OBJ_ACT9_WALL, i);
        if (!w) break;
        mapobj_draw_self(w);
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    if (type != SERVER_ACT9WALL_STATE) return false;
    if (pass) return true;
    int id = rd_u8(r);
    float x = rd_u16(r), y = rd_u16(r);
    for (int i = 0;; i++) {
        MapObj *w = mapobj_find(OBJ_ACT9_WALL, i);
        if (!w) break;
        if (w->nid != id) continue;
        if (id == 0) mapobj_move(w, -2240, y - 768);
        else if (id == 1) mapobj_move(w, x - 2240, y);
        else if (id == 2) mapobj_move(w, (float)level.room.width - x, y);
        break;
    }
    return true;
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_ACT9_WALL, .create = wall_create, .begin_step = wall_begin_step, .dynamic = true },
    { .object = OBJ_ACT9_BODIES, .create = bodies_create, .step = bodies_step },
    { .object = -1 },
};

const MapModule MAP_ACT9 = {
    .room = ROOM_ACT9,
    .objects = OBJECTS,
    .draw_front = draw_front,
    .packet = packet,
};

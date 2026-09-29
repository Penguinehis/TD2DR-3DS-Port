// room_kindandfair: obj_kaf_speedboobster (speed-boost monitors), SERVER_KAFMONITOR_STATE,
// CLIENT_KAFMONITOR_ACTIVATE. The player-side break check ("Boob box" in
// scr_collision_objects) is done here, in the monitor's step.
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

// Character state macros (chars/amy.c, chars/sally.c)
#define AMY_HJUMP (ST_BALANCING + 2)
#define SALLY_SLIDE (ST_BALANCING + 2)

typedef struct {
    bool isBroken;
} Monitor;

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

static void send_activate(const MapObj *o, bool proj)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_KAFMONITOR_ACTIVATE);
    pkt_u8(&pk, (u8)o->nid);
    pkt_u8(&pk, proj);
    net_send(&pk, true);
}

static void monitor_create(MapObj *o)
{
    MAPOBJ_VARS(o, Monitor)->isBroken = false;
    o->nid = 0;
    o->image_speed = 0;
}

// scr_collision_objects "Boob box" (runs in the player's step in the GML)
static void monitor_step(MapObj *o)
{
    Monitor *v = MAPOBJ_VARS(o, Monitor);
    if (!level.has_player || v->isBroken || (int)o->image_index != 0) return;
    Player *p = &level.player;
    float l, t, r, b;
    if (!box(o, &l, &t, &r, &b)) return;
    // collision_rectangle(x - 6, y - 20, x + 6, y + 20, obj_kaf_speedboobster)
    if (p->x + 6 < l || p->x - 6 >= r || p->y + 20 < t || p->y - 20 >= b) return;
    int ch = p->character;
    bool attacking = false;
    if (p->isAttacking && (ch == CHARACTER_EXE || ch == CHARACTER_AMY || ch == CHARACTER_KNUX ||
                           ch == CHARACTER_EGGMAN || ch == CHARACTER_SALLY))
        attacking = true;
    if (ch == CHARACTER_AMY && p->state == AMY_HJUMP) attacking = true;
    if (p->isJumping || (p->state == ST_FALL && ch == CHARACTER_AMY)) attacking = true;
    if (p->isSpinning) attacking = true;
    if (p->state == SALLY_SLIDE && ch == CHARACTER_SALLY) attacking = true;
    if (ch == CHARACTER_EXE && p->invisTimer > 0) attacking = false;
    if (attacking || p->isBoosting) {
        v->isBroken = true;
        send_activate(o, false);
    }
    // End Step: a Tails shot breaks it too (proj = true)
    float pl, pt, pr, pb;
    if (!v->isBroken && mapobj_bbox(o, &pl, &pt, &pr, &pb) && ents_projectile_meets(pl, pt, pr, pb)) {
        v->isBroken = true;
        send_activate(o, true);
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    if (type != SERVER_KAFMONITOR_STATE) return false;
    if (pass) return true;
    int state = rd_u8(r);
    int nid = rd_u8(r);
    int n = mapobj_number(OBJ_KAF_SPEEDBOOBSTER);
    if (!n) return true;
    MapObj *o = mapobj_find(OBJ_KAF_SPEEDBOOBSTER, nid % n);
    if (!o) return true;
    Monitor *v = MAPOBJ_VARS(o, Monitor);
    o->nid = nid;
    if (state == 1) {
        o->image_index = 0;
        v->isBroken = false;
    } else if (state == 2) {
        o->image_index = 1;
        v->isBroken = true;
        maps_sound_at(SND_BREAK, o->x, o->y - 16);
        if (!level.has_player) return true;
        u16 pid = rd_u16(r);
        if (net.id != pid) return true;
        Player *p = &level.player;
        if (p->isBoosting) return true;
        p->isBoosting = true;
        p->isSpinning = false;
        p->isJumping = false;
    }
    net_quick_effect(o->x + 15, o->y + 15, SPR_EXPLOSION, false, 1, 0, 0, 0.5f);
    return true;
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_KAF_SPEEDBOOBSTER, .create = monitor_create, .step = monitor_step },
    { .object = -1 },
};

const MapModule MAP_KINDANDFAIR = {
    .room = ROOM_KINDANDFAIR,
    .objects = OBJECTS,
    .packet = packet,
};

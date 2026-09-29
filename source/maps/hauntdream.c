// room_haundream: obj_hd_crystal (the switches that toggle the doors), obj_hd_door (vertical)
// and obj_hd_door2 (horizontal) with moving collision, SERVER_HDDOOR_STATE and
// CLIENT_HDDOOR_TOGGLE. obj_hd_spring (hold jump or up) is in the shared spring code,
// obj_hd_hide is a hiding place (WC_HIDING) and obj_hd_collision a plain solid.
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

static float current_time_ms(void) { return level.time * 1000.0f / 60; }

// sprite_width / sprite_height
static float spr_w(const MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    return s ? s->width * o->xscale : 0;
}

static float spr_h(const MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    return s ? s->height * o->yscale : 0;
}

// scr_player_instakill (the same steps as the hp <= 0 branch of player_hurt_ex, which cannot
// be reached through player_hurt while the player is invulnerable or shielded).
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

// A door closed on the player: the EXE and demons go to the nearest obj_deathtp_point,
// survivors die.
static void crushed(Player *p)
{
    if (p->character == CHARACTER_EXE || p->revivalTimes >= 2) {
        WorldInst *pt = world_nearest(p->x, p->y, WC_DEATH_TP);
        if (!pt) return;
        p->x = pt->x;
        p->y = pt->y;
    } else {
        instakill(p);
    }
}

// ------------------------------------------------------------------ obj_hd_crystal

typedef struct {
    bool state, canUse;
    float sY;
} Crystal;

static void crystal_create(MapObj *o)
{
    Crystal *c = MAPOBJ_VARS(o, Crystal);
    c->state = false;
    c->canUse = true;
    c->sY = o->y;
    o->depth = 10;
}

static void crystal_step(MapObj *o)
{
    Crystal *c = MAPOBJ_VARS(o, Crystal);
    o->y = c->sY + sinf(current_time_ms() / 400) * 2;
    // Draw_0: looking up / down and pressing up / down while touching the crystal
    if (!level.has_player || !player_controls || !mapobj_meets_player(o)) return;
    const Player *p = &level.player;
    if ((p->isLookingDown || p->isLookingUp) && (hidKeysDown() & (KEY_DOWN | KEY_UP))) {
        NetPacket pk;
        pkt_begin(&pk, CLIENT_HDDOOR_TOGGLE);
        net_send(&pk, true);
    }
}

static void crystal_draw(MapObj *o)
{
    Crystal *c = MAPOBJ_VARS(o, Crystal);
    if (level.has_player && c->canUse && mapobj_meets_player(o))
        sprite_draw(SPR_LIMPCITY_EYE_HINT, current_time_ms() / 400, o->x - level.cam_x, o->y - level.cam_y, 1, 1, 0,
                    0xFFFFFFFF, 1);
    o->image_index = c->state;
    mapobj_draw_self(o);
}

// ------------------------------------------------------------------ obj_hd_door / obj_hd_door2

typedef struct {
    bool state;
    float start;   // sY (door) / sX (door2)
} Door;

static void door_create(MapObj *o)
{
    Door *d = MAPOBJ_VARS(o, Door);
    d->state = false;
    d->start = o->object == OBJ_HD_DOOR2 ? o->x : o->y;
}

static void door_step(MapObj *o)
{
    // A dead player lying in the door's way is pushed out to the right
    if (!level.has_player || !level.player.isDead) return;
    for (int guard = 0; guard < 512 && mapobj_meets_player(o); guard++) level.player.x++;
}

static void door_end_step(MapObj *o)
{
    Door *d = MAPOBJ_VARS(o, Door);
    float w = spr_w(o), h = spr_h(o);
    if (!d->state) {
        if (o->y >= d->start) return;
        mapobj_move(o, o->x, o->y + 1);
        if (!level.has_player) return;
        Player *p = &level.player;
        // Any point of the door's bottom edge inside the sensor rectangle TL..BR
        float by = o->y + h + 1;
        if (by >= p->sTL.y && by <= p->sBR.y && o->x <= p->sBR.x && o->x + w > p->sTL.x) crushed(p);
    } else if (o->y > d->start - h) {
        mapobj_move(o, o->x, o->y - 1);
    }
}

static void door2_end_step(MapObj *o)
{
    Door *d = MAPOBJ_VARS(o, Door);
    float w = spr_w(o), h = spr_h(o);
    if (!d->state) {
        if (o->x >= d->start) return;
        mapobj_move(o, o->x + 1, o->y);
        if (!level.has_player) return;
        Player *p = &level.player;
        // Any point of the door's right edge (y+2 .. y+h-2) inside the player's 16x36 box
        float rx = o->x + w + 1;
        if (rx >= p->x - 8 && rx <= p->x + 8 && o->y + 2 <= p->y + 18 && o->y + h - 2 > p->y - 18) crushed(p);
    } else if (o->x > d->start - w) {
        mapobj_move(o, o->x - 1, o->y);
    }
}

// ------------------------------------------------------------------ level

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    if (type != SERVER_HDDOOR_STATE) return false;
    if (pass) return true;
    u8 t = rd_u8(r);
    bool state = rd_u8(r) != 0;
    if (t == 0) {
        if (!mapobj_number(OBJ_HD_DOOR) || !mapobj_number(OBJ_HD_DOOR2) || !mapobj_number(OBJ_HD_CRYSTAL))
            return true;
        MapObj *o;
        for (int i = 0; (o = mapobj_find(OBJ_HD_CRYSTAL, i)); i++) MAPOBJ_VARS(o, Crystal)->state = state;
        for (int i = 0; (o = mapobj_find(OBJ_HD_DOOR, i)); i++) {
            MAPOBJ_VARS(o, Door)->state = state;
            maps_sound_at(SND_DOOR, o->x, o->y);
        }
        for (int i = 0; (o = mapobj_find(OBJ_HD_DOOR2, i)); i++) {
            MAPOBJ_VARS(o, Door)->state = state;
            maps_sound_at(SND_DOOR, o->x, o->y);
        }
    } else {
        MapObj *o;
        for (int i = 0; (o = mapobj_find(OBJ_HD_CRYSTAL, i)); i++) {
            Crystal *c = MAPOBJ_VARS(o, Crystal);
            if (c->canUse) maps_sound_at(SND_MESSAGE, o->x, o->y);
            c->canUse = state;
        }
    }
    return true;
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_HD_CRYSTAL, .create = crystal_create, .step = crystal_step, .draw = crystal_draw },
    { .object = OBJ_HD_DOOR, .create = door_create, .step = door_step, .end_step = door_end_step, .dynamic = true },
    { .object = OBJ_HD_DOOR2, .create = door_create, .end_step = door2_end_step, .dynamic = true },
    { .object = -1 },
};

const MapModule MAP_HAUNTDREAM = {
    .room = ROOM_HAUNDREAM,
    .objects = OBJECTS,
    .packet = packet,
};

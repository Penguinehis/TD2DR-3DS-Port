// room_pricelessfreedom: obj_pf_lift (lifts that carry the player who activated them up),
// SERVER_PFLIFT_STATE (reliable: activate / arrive / reset; unreliable: position) and
// CLIENT_PFLIT_ACTIVATE. The lifts have no collision shape (no parent object).
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

typedef struct {
    bool activated, shouldFade;
    u16 pid;
} Lift;

static bool riding(const MapObj *o)
{
    u16 pid = MAPOBJ_VARS(o, Lift)->pid;
    return level.has_player && pid != 0 && pid == net.id;  // pid 0: nobody
}

// The rider is held on the lift (Step_1 / Step_0 / Step_2 all do this)
static void hold_player(const MapObj *o)
{
    Player *p = &level.player;
    p->x = o->x;
    p->y = o->y - 16;
    p->xspd = p->yspd = 0;
    p->state = ST_HURT;
    p->isHurt = p->isFlying = p->isSpinning = p->isJumping = p->isGrounded = p->isAttacking = false;
    // isStomping (Exetior) has no shared field here
}

static void lift_create(MapObj *o)
{
    Lift *v = MAPOBJ_VARS(o, Lift);
    v->activated = v->shouldFade = false;
    v->pid = 0;
    o->image_alpha = 0;
    o->depth = -2;
}

static void lift_begin_step(MapObj *o)
{
    if (!riding(o)) return;
    hold_player(o);
    level.player.canMove = true;
}

static void lift_step(MapObj *o)
{
    Lift *v = MAPOBJ_VARS(o, Lift);
    if (!v->shouldFade && o->image_alpha < 1) o->image_alpha += 0.016f;
    if (v->shouldFade && o->image_alpha > 0) o->image_alpha -= 0.016f;
    if (o->image_alpha < 1 || !level.has_player) return;
    Player *p = &level.player;
    if (p->hp <= 0) return;
    if (!v->activated && mapobj_meets_player(o)) {
        NetPacket pk;
        pkt_begin(&pk, CLIENT_PFLIT_ACTIVATE);
        pkt_u8(&pk, (u8)o->nid);
        net_send(&pk, true);
        v->activated = true;
    }
    if (riding(o)) {
        hold_player(o);
        p->canMove = true;
    }
}

// The rider's hurt sprite (Draw_76 / Draw_72), after the player's own Pre-Draw
static int hurt_sprite(const Player *p)
{
    bool demon = p->revivalTimes >= 2;
    switch (p->character) {
    case CHARACTER_EXE:
        switch (p->exe_character) {
        case EXE_ORIGINAL: return p->invisTimer > 0 ? SPR_EXE_INVIS_HURT : SPR_EXE_HURT;
        case EXE_CHAOS: return SPR_CHAOS_HURT;  // spr_chaos_sidle while slimed (slimeTimer is Chaos-private)
        case EXE_EXETIOR: return SPR_EXETIOR_HURT;
        case EXE_EXELLER: return SPR_EXELLER_HURT;
        }
        return -1;
    case CHARACTER_TAILS: return demon ? SPR_ETAILS_HURT : SPR_TAILS_HURT;
    case CHARACTER_KNUX: return demon ? SPR_EKNUX_HURT : SPR_KNUX_HURT;
    case CHARACTER_EGGMAN: return demon ? SPR_EEGG_HURT : SPR_EGG_HURT;
    case CHARACTER_AMY: return demon ? SPR_EAMY_HURT : SPR_AMY_HURT;
    case CHARACTER_CREAM: return demon ? SPR_ECREAM_HURT : SPR_CREAM_HURT;
    case CHARACTER_SALLY: return demon ? SPR_ESALLY_HURT : SPR_SALLY_HURT;
    }
    return -1;
}

static void lift_end_step(MapObj *o)
{
    if (!riding(o)) return;
    Player *p = &level.player;
    hold_player(o);
    int spr = hurt_sprite(p);
    if (spr >= 0 && p->sprite != spr) {
        p->sprite = spr;
        p->image_index = 0;
    }
    p->image_speed = 0.5f;
}

// ------------------------------------------------------------------ packets

static MapObj *lift_by_id(int id)
{
    for (int i = 0;; i++) {
        MapObj *l = mapobj_find(OBJ_PF_LIFT, i);
        if (!l || l->nid == id) return l;
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    if (type != SERVER_PFLIFT_STATE) return false;
    if (pass) return true;
    int state = rd_u8(r), id = rd_u8(r);
    MapObj *inst = lift_by_id(id);
    if (!inst) return true;
    Lift *v = MAPOBJ_VARS(inst, Lift);
    Player *p = &level.player;
    if (!reliable) {
        u16 pid = rd_u16(r);
        float y = rd_u16(r);
        if (!v->activated) return true;
        mapobj_move(inst, inst->x, y);
        v->pid = pid;
        return true;
    }
    if (state == 0) {
        u16 pid = rd_u16(r);
        v->activated = true;
        v->pid = pid;
        if (pid == net.id && level.has_player) {
            player_controls_lock = true;  // global.playerControls = false
            p->x = inst->x;
            p->y = inst->y;
        }
    } else if (state == 2) {
        u16 pid = rd_u16(r);
        float y = rd_u16(r);
        v->activated = false;
        v->shouldFade = true;
        v->pid = 0;
        mapobj_move(inst, inst->x, y);
        for (int i = 0;; i++) {  // with(obj_pf_lift) scr_audio_play_3d(emitter, snd_lift)
            MapObj *l = mapobj_find(OBJ_PF_LIFT, i);
            if (!l) break;
            maps_sound_at(SND_LIFT, l->x, l->y);
        }
        if (pid == net.id && level.has_player) {
            player_controls_lock = false;  // global.playerControls = true
            p->x = inst->x;
            p->y = inst->y;
            p->yspd = -3;
        }
    } else if (state == 3) {
        float sy = rd_u16(r);
        v->activated = false;
        v->shouldFade = false;
        v->pid = 0;
        mapobj_move(inst, inst->x, sy);
    }
    return true;
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_PF_LIFT, .create = lift_create, .begin_step = lift_begin_step, .step = lift_step,
      .end_step = lift_end_step, .dynamic = true },
    { .object = -1 },
};

const MapModule MAP_PRICELESSFREEDOM = {
    .room = ROOM_PRICELESSFREEDOM,
    .objects = OBJECTS,
    .packet = packet,
};

// obj_egg + scr_egg_special.
#include <math.h>

#include "../gen/objects.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../net.h"
#include "../sprite.h"
#include "chars.h"
#include "../entities.h"
#include "../gen/sounds.h"

#define EGGMAN_DJUMP_RECHARGE (60 * 10)
#define EGGMAN_TRACKER_RECHARGE (60 * 30)
#define EGGMAN_SHIELD_RECHARGE (60 * 20)
#define EEGGMAN_SHIELD_RECHARGE (60 * 7)
#define EGGMAN_MAXSPEED 9
#define EGGMAN_ACC 0.045835f

// Step_2
#define EGG_DJUMP (ST_BALANCING + 1)
#define EGG_ZIPLINE (ST_BALANCING + 2)

typedef struct {
    int djumpRecharge, trackerRecharge, shieldRechrage;
    bool isColliding;
} EggVars;

// Create_0
static void init(Player *p)
{
    p->canSpin = false;
    p->jumpForce = 6.2f;
    // TODO(M5b): reviveObj = obj_revival_puppet (Step_0 keeps it at x, y - 40, master_id = nid)
}

// place_meeting(x, y, obj_angleanuller) with the current sprite bbox. obj_angleanuller_eggsafe
// is not a child of obj_angleanuller, so it does not count here.
static bool place_angleanuller(const Player *p)
{
    const SpriteInfo *s = sprite_info(p->sprite);
    float l = p->x, t = p->y, r = p->x, b = p->y;
    if (s) {
        float l0 = p->x + (s->bbox_left - s->xorigin) * p->image_xscale;
        float r0 = p->x + (s->bbox_right + 1 - s->xorigin) * p->image_xscale;
        l = fminf(l0, r0);
        r = fmaxf(l0, r0) - 1;
        t = p->y + s->bbox_top - s->yorigin;
        b = p->y + s->bbox_bottom - s->yorigin;
    }
    WorldInst *list[16];
    int n = world_query_rect(l, t, r, b, WC_ANGLE_NULL, list, 16);
    for (int i = 0; i < n; i++)
        if (list[i]->object == OBJ_ANGLEANULLER) return true;
    return false;
}

// scr_egg_special
static void special(Player *p, const Keys *k)
{
    EggVars *v = CHAR_VARS(p, EggVars);

    if (p->redRingTimer > 0) {
        if (v->djumpRecharge <= 2) v->djumpRecharge = 2;
        if (v->shieldRechrage >= 0 && v->shieldRechrage <= 2) v->shieldRechrage = 2;
        if (v->trackerRecharge <= 2) v->trackerRecharge = 2;
        p->isAttacking = false;
    }

    if (p->isHiding) {
        if (v->djumpRecharge <= 60 * 2) v->djumpRecharge = 60 * 2;
        if (v->shieldRechrage >= 0 && v->shieldRechrage <= 60 * 2) v->shieldRechrage = 60 * 2;
        if (v->trackerRecharge <= 60 * 2) v->trackerRecharge = 60 * 2;
        p->isAttacking = false;
    }

    if (player_controls && k->a_p && p->isJumping && v->djumpRecharge == 0) {
        p->isJumping = false;
        p->yspd = -8;
        player_sound(p, SND_EGG_DJUMP);
        v->djumpRecharge = -1;
    }

    if (player_controls && k->b_p && v->shieldRechrage == 0) {
        p->isAttacking = true;
        player_sound(p, SND_EGG_SHIELD);
        v->shieldRechrage = -1;
    }

    float xx = p->image_xscale > 0 ? 12 : -12;
    v->isColliding = p->isGrounded && !place_angleanuller(p) && world_point(p->x + xx, p->y + 18, WC_FLOOR);
    if (player_controls && k->c_p && v->trackerRecharge <= 0 && p->isLookingDown && v->isColliding) {
        player_sound(p, SND_EGG_TRACKER);
        NetPacket pk;
        pkt_begin(&pk, CLIENT_ETRACKER);
        pkt_u16(&pk, (u16)(int)p->x);
        pkt_u16(&pk, (u16)(int)(p->y + 18));
        net_send(&pk, true);
        v->trackerRecharge = EGGMAN_TRACKER_RECHARGE;
    }

    if (v->shieldRechrage < 0) {
        v->shieldRechrage--;
        if (v->shieldRechrage <= -(60 * 1.5)) {
            p->isAttacking = false;
            v->shieldRechrage = p->revivalTimes >= 2 ? EEGGMAN_SHIELD_RECHARGE : EGGMAN_SHIELD_RECHARGE;
        }
    }

    if (v->djumpRecharge < 0) {
        v->djumpRecharge--;
        if (v->djumpRecharge <= -30) v->djumpRecharge = EGGMAN_DJUMP_RECHARGE;
    }

    if (v->djumpRecharge > 0) v->djumpRecharge--;
    if (v->shieldRechrage > 0) v->shieldRechrage--;
    if (v->trackerRecharge > 0) v->trackerRecharge--;
}

// Step_0 is the common form (step_move NULL). After scr_collision_basic it only has the contact
// damage from demonized obj_player_puppet instances (shared game code, TODO(M5b)).

// Step_2
static void end_step(Player *p)
{
    const EggVars *v = CHAR_VARS_C(p, EggVars);
    if (!p->emotion) p->state = ST_IDLE;
    if (p->isDead) { p->state = ST_DEAD; return; }
    if (p->shockedTimer > 0) { p->state = ST_DEAD; return; }
    if (p->isHurt) { p->state = ST_HURT; return; }
    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        else { p->state = EGG_ZIPLINE; return; }
    }
    if (p->isLookingUp) { p->state = ST_LOOKUP; return; }
    if (p->isLookingDown) { p->state = ST_LOOKDOWN; return; }
    if (p->isSpinning) { p->state = ST_SPIN; return; }
    if (fabsf(p->xspd) > 0 && p->isGrounded) p->state = fabsf(p->xspd) < 6 ? ST_WALK : ST_RUN;
    if (p->isOnEdge) { p->state = ST_BALANCING; return; }
    if (!p->isGrounded) {
        if (p->isJumping) {
            p->state = ST_JUMP;
        } else if (v->djumpRecharge < 0) {
            if ((int)floorf(p->y) % 10 == 0) net_quick_effect(p->x, p->y, SPR_EGGPACK, false, 1, 0, -1, 0.5f);
            p->state = EGG_DJUMP;
        } else {
            p->state = ST_FALL;
        }
    }
}

// Other_7
static void animation_end(Player *p)
{
    switch (p->state) {
    case ST_IDLE: p->image_index = player_frames(p) - 11; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3:
        if (p->emHeld[p->state - ST_EMOTION1]) {
            p->image_index = 0;
        } else {
            p->image_index = 0;
            p->state = ST_IDLE;
            p->emotion = false;
        }
        break;
    }
}

// Draw_76
static void pre_draw(Player *p)
{
    int spr = player_anim_sprite(p);
    if (spr >= 0) p->sprite = spr;
    int frames = player_frames(p);
    switch (p->state) {
    case ST_IDLE: p->image_speed = 1; break;
    case ST_BALANCING: p->image_xscale = p->edgeDir; p->image_speed = 1; break;
    case ST_HURT: p->image_speed = 0.4f; break;
    case ST_WALK: p->image_speed = 0.2f + fabsf(p->xspd) / p->maxHSpeed; break;
    case ST_FALL: p->image_index = 0; break;
    case ST_RUN: p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f); break;
    case ST_JUMP: p->image_index = p->yspd > 0 ? 1 : 0; break;
    case ST_DEAD:
        if (p->revivalTimes >= 2) { p->image_speed = 0.5f; break; }
        p->image_speed = p->isGrounded ? 1 : 0;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;
    case ST_LOOKUP:
    case ST_LOOKDOWN:
        p->image_speed = 1;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;
    case ST_SPIN: p->image_speed = fabsf(p->xspd) / 5; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: p->image_speed = 1; break;
    }
}

// Draw_0 (effectTime afterimages / palette swap: TODO(M8) in player_draw)
static void draw(const Player *p, float cam_x, float cam_y)
{
    player_draw_body(p, cam_x, cam_y, p->isSpinning ? 0 : p->angle * 180.0f / (float)M_PI);
    if (p->isAttacking) {
        // (current_time / 30) % 41
        float frame = fmodf(level.time * (1000.0f / 60.0f) / 30.0f, 41);
        sprite_draw(SPR_ELECTROSHIELD, floorf(frame), floorf(p->x) - cam_x, floorf(p->y) - cam_y, 1, 1, 0,
                    0xFFFFFFFF, 1);
    }
}

// scr_move_basic "if(isBoosting)" branch
static void on_boost(Player *p)
{
    EggVars *v = CHAR_VARS(p, EggVars);
    if (v->shieldRechrage < 0) v->shieldRechrage = -(60 * 1.5);
    p->isAttacking = false;
}

// scr_collision_objects spring switch (the shared code resets isJumping right after, as the GML does)
static void on_spring(Player *p) { p->isJumping = true; }

static u8 net_flags(const Player *p) { return p->isAttacking ? NETF_ATTACKING : 0; }

// net_state_game SERVER_GAME_DEATHTIMER_END: ability timers restart when demonized
static void on_demonize(Player *p)
{
    EggVars *v = CHAR_VARS(p, EggVars);
    v->djumpRecharge = 0;
    v->trackerRecharge = 0;
    v->shieldRechrage = 0;
}

const CharDef CHAR_EGGMAN_DEF = {
    .on_demonize = on_demonize,
    .name = "eggman",
    .init = init,
    .special = special,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .on_boost = on_boost,
    .on_spring = on_spring,
    .base_acc = EGGMAN_ACC,
    .base_maxspeed = EGGMAN_MAXSPEED,
    .net_flags = net_flags,
};

// obj_sally + scr_sally_special.
#include <math.h>

#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "chars.h"
#include "../entities.h"
#include "../gen/sounds.h"

#define SALLY_EATTACK_RECHARGE (60 * 4)
#define SALLY_ATTACK_RECHARGE (60 * 15)
#define SALLY_SHIELD_RECHARGE (60 * 30)
#define SALLY_ESHIELD_RECHARGE (60 * 15)
#define SALLY_MAXSPEED 11
#define SALLY_ACC 0.046875f

// Step_2
#define SALLY_ATTACK (ST_BALANCING + 1)
#define SALLY_SLIDE (ST_BALANCING + 2)
#define SALLY_SEX (ST_BALANCING + 3)
#define SALLY_ZIPLINE (ST_BALANCING + 4)

typedef struct {
    int shieldTimer, shieldRechrage;
    float slideSpeed;
    bool isSliding;
} SallyVars;

// Create_0
static void init(Player *p)
{
    SallyVars *v = CHAR_VARS(p, SallyVars);
    v->slideSpeed = 5;
    p->canSpin = false;
    p->jumpForce = 6.5f;
    // TODO(M5b): reviveObj = obj_revival_puppet (Step_0 keeps it at x, y - 40, master_id = nid)
}

// scr_sally_special
static void special(Player *p, const Keys *k)
{
    SallyVars *v = CHAR_VARS(p, SallyVars);

    if (p->redRingTimer > 0) {
        if (v->shieldRechrage <= 2) v->shieldRechrage = 2;
        if (p->attackTimer <= 2) p->attackTimer = 2;
        p->isAttacking = false;
    }

    if (p->isHiding) {
        if (p->attackTimer <= 60 * 2) p->attackTimer = 60 * 2;
        if (v->shieldRechrage <= 60 * 2) v->shieldRechrage = 60 * 2;
        p->isAttacking = false;
    }

    if (player_controls && k->b_p && !p->isGrounded && p->attackTimer <= 0) {
        player_sound(p, SND_DASH);
        if (k->left) p->image_xscale = -1;
        else if (k->right) p->image_xscale = 1;
        p->image_index = 0;
        p->image_speed = 0;
        p->isJumping = false;
        p->isAttacking = true;
        p->attackTimer = p->revivalTimes >= 2 ? SALLY_EATTACK_RECHARGE : SALLY_ATTACK_RECHARGE;
    }

    if (player_controls && k->c_p && v->shieldRechrage <= 0) {
        player_sound(p, SND_SALLY_SHIELD);
        v->shieldTimer = 5 * 60;
        v->shieldRechrage = p->revivalTimes >= 2 ? SALLY_ESHIELD_RECHARGE : SALLY_SHIELD_RECHARGE;
    }

    if (player_controls && fabsf(p->xspd) >= 2.5f && k->down_p && p->isGrounded && !v->isSliding) {
        player_sound(p, SND_SALLY_SLIDE);
        p->isAttacking = false;
        v->isSliding = true;
        v->slideSpeed = p->xspd + 2 * sgnf(p->xspd);
    }

    if (v->shieldRechrage > 0) v->shieldRechrage--;
    if (p->attackTimer > 0) p->attackTimer--;

    if (v->shieldTimer > 0) {
        v->shieldTimer--;
        if (v->shieldTimer <= 0 && p->revivalTimes >= 2) {
            player_sound(p, SND_SALLY_SHIELDBREAK);
            net_quick_effect(p->x, p->y, SPR_SHIELDBREAK2, false, 1, 0, 0, 1);
        }
        v->shieldRechrage = p->revivalTimes >= 2 ? SALLY_ESHIELD_RECHARGE : SALLY_SHIELD_RECHARGE;
    }

    if (v->isSliding) {
        if (p->shockedTimer > 0) { v->isSliding = false; return; }
        if (!p->isGrounded) { v->isSliding = false; return; }
        if (p->x <= 1 || p->x >= level.room.width - 1) { v->isSliding = false; return; }

        if (fabsf(v->slideSpeed) > 0) {
            v->slideSpeed -= fminf(fabsf(v->slideSpeed), p->acc * 2.5f) * sgnf(v->slideSpeed) * cosf(p->angle);
        } else {
            v->isSliding = false;
            return;
        }

        if ((int)floorf(p->x) % 5 == 0) {
            net_quick_effect(p->x - 8 * sgnf(v->slideSpeed), p->y + 20, SPR_DUST, false, 1, 0, 0, 0.5f);
            player_sound_local(SND_SALLY_SLIDE);
        }

        p->gspd = 0;
        p->xspd = v->slideSpeed * cosf(p->angle);
        p->yspd = v->slideSpeed * -sinf(p->angle);
    }
}

// Step_0 is the common form (step_move NULL). After scr_collision_basic it only has the contact
// damage from demonized obj_player_puppet instances (shared game code, TODO(M5b)).

// Step_2
static void end_step(Player *p)
{
    const SallyVars *v = CHAR_VARS_C(p, SallyVars);
    if (!p->emotion) p->state = ST_IDLE;
    if (p->isDead) { p->state = ST_DEAD; return; }
    if (p->shockedTimer > 0) { p->state = ST_DEAD; return; }
    if (p->isHurt) { p->state = ST_HURT; return; }
    if (v->isSliding) { p->state = SALLY_SLIDE; return; }
    if (p->isAttacking) { p->state = SALLY_ATTACK; return; }
    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        else { p->state = SALLY_ZIPLINE; return; }
    }
    if (p->isLookingUp) { p->state = ST_LOOKUP; return; }
    if (p->isLookingDown) { p->state = ST_LOOKDOWN; return; }
    if (p->isSpinning) { p->state = ST_SPIN; return; }
    if (fabsf(p->xspd) > 0 && p->isGrounded) p->state = fabsf(p->xspd) < 6 ? ST_WALK : ST_RUN;
    if (p->isOnEdge) { p->state = ST_BALANCING; return; }
    if (!p->isGrounded) p->state = p->isJumping ? ST_JUMP : ST_FALL;
}

// Other_7
static void animation_end(Player *p)
{
    switch (p->state) {
    case ST_IDLE: p->image_index = player_frames(p) - 14; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: {
        int i = p->state - ST_EMOTION1;
        if (p->emHeld[i]) {
            p->image_index = i == 0 ? 2 : 0;
        } else {
            p->image_index = 0;
            p->state = ST_IDLE;
            p->emotion = false;
        }
        break;
    }
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
    case ST_JUMP:
        if (p->yspd > 0) {
            p->image_speed = 1;
            if (p->image_index >= frames - 1) p->image_index = frames - 1;
        } else {
            p->image_speed = 0;
        }
        break;
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
    case SALLY_SLIDE: p->image_speed = 0; break;
    case SALLY_ATTACK:
        p->image_speed = 1;
        if (p->image_index >= frames - 1) {
            p->isAttacking = false;
            p->image_index = frames - 1;
        }
        break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3:
    case SALLY_SEX: p->image_speed = 1; break;
    }
}

// Draw_0 (effectTime afterimages / palette swap: TODO(M8) in player_draw)
static void draw(const Player *p, float cam_x, float cam_y)
{
    const SallyVars *v = CHAR_VARS_C(p, SallyVars);
    player_draw_body(p, cam_x, cam_y, p->isSpinning ? 0 : p->angle * 180.0f / (float)M_PI);
    if (v->shieldTimer > 0) {
        // (current_time / 30) % 41
        float frame = fmodf(level.time * (1000.0f / 60.0f) / 30.0f, 41);
        sprite_draw(p->revivalTimes >= 2 ? SPR_SALLYSHIELD2 : SPR_SALLYSHIELD, floorf(frame), floorf(p->x) - cam_x,
                    floorf(p->y) - cam_y, 1, 1, 0, 0xFFFFFFFF, 1);
    }
}

// scr_move_basic "if(isDead)" block
static void on_dead(Player *p)
{
    SallyVars *v = CHAR_VARS(p, SallyVars);
    p->isAttacking = false;
    v->isSliding = false;
    if (v->shieldTimer > 0) v->shieldTimer = 0;
}

// scr_move_basic "if(isHurt)" switch
static void on_hurt(Player *p)
{
    p->isAttacking = false;
    CHAR_VARS(p, SallyVars)->isSliding = false;
}

// scr_move_basic "if(isBoosting)" block
static void on_boost(Player *p)
{
    SallyVars *v = CHAR_VARS(p, SallyVars);
    if (v->shieldTimer > 0) v->shieldTimer = 0;
}

// scr_collision_basic: wall hit
static void on_wall(Player *p, WorldInst *wall, int dir)
{
    (void)wall;
    (void)dir;
    CHAR_VARS(p, SallyVars)->isSliding = false;
}

// scr_collision_basic: out of the room bottom
static void on_abyss(Player *p) { CHAR_VARS(p, SallyVars)->isSliding = false; }

// scr_collision_objects spring switch (isJumping is reset right after by the shared code)
static void on_spring(Player *p)
{
    SallyVars *v = CHAR_VARS(p, SallyVars);
    if (p->isAttacking) {
        p->attackTimer = p->revivalTimes >= 2 ? SALLY_EATTACK_RECHARGE : SALLY_ATTACK_RECHARGE;
        p->isAttacking = false;
    }
    if (v->isSliding) {
        v->slideSpeed = 0;
        v->isSliding = false;
    }
    p->isJumping = true;
}

// scr_collision_objects: no hiding while sliding
static bool hide_blocked(const Player *p) { return CHAR_VARS_C(p, SallyVars)->isSliding; }

// scr_player_hurt: the shield swallows one hit (damage > 0 is checked by the caller)
static bool hurt_blocked(Player *p, int damage)
{
    SallyVars *v = CHAR_VARS(p, SallyVars);
    if (damage <= 0 || v->shieldTimer <= 0) return false;
    // rSallyShield achievement: omitted
    p->hurttime = 60;
    v->shieldTimer = 0;
    player_sound(p, SND_SALLY_SHIELDBREAK);
    net_quick_effect(p->x, p->y, p->revivalTimes >= 2 ? SPR_SHIELDBREAK2 : SPR_SHIELDBREAK, false, 1, 0, 0, 1);
    return true;
}

static u8 net_flags(const Player *p)
{
    u8 f = p->isAttacking ? NETF_ATTACKING : 0;
    if (CHAR_VARS_C(p, SallyVars)->shieldTimer > 0) f |= NETF_SALLYSHIELD;
    return f;
}

// net_state_game SERVER_GAME_DEATHTIMER_END: ability timers restart when demonized
static void on_demonize(Player *p)
{
    SallyVars *v = CHAR_VARS(p, SallyVars);
    p->attackTimer = 0;
    v->shieldTimer = 0;
    v->shieldRechrage = 0;
}

// obj_playerui
static void draw_gui(const Player *p)
{
    const SallyVars *v = CHAR_VARS_C(p, SallyVars);
    int val = p->revivalTimes >= 2 ? SALLY_EATTACK_RECHARGE : SALLY_ATTACK_RECHARGE;
    float prog = 1.0f - fmaxf(p->attackTimer, 0) / (float)val;
    gui_ability(SPR_GUI_SALLYATTACK, p->revivalTimes >= 2, prog, 10, 240, prog >= 1 && !p->isGrounded);
    val = p->revivalTimes >= 2 ? SALLY_ESHIELD_RECHARGE : SALLY_SHIELD_RECHARGE;
    prog = 1.0f - fmaxf(v->shieldRechrage, 0) / (float)val;
    gui_ability(SPR_GUI_SALLYSHIELD, p->revivalTimes >= 2, prog, 10, 258, prog >= 1);
}

const CharDef CHAR_SALLY_DEF = {
    .draw_gui = draw_gui,
    .on_demonize = on_demonize,
    .name = "sally",
    .init = init,
    .special = special,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .on_dead = on_dead,
    .on_hurt = on_hurt,
    .on_boost = on_boost,
    .on_wall = on_wall,
    .on_abyss = on_abyss,
    .on_spring = on_spring,
    .hide_blocked = hide_blocked,
    .hurt_blocked = hurt_blocked,
    .base_acc = SALLY_ACC,
    .base_maxspeed = SALLY_MAXSPEED,
    .net_flags = net_flags,
};

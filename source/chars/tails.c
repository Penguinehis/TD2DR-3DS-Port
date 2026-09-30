// obj_tails + scr_tails_special + obj_tails_tail.
#include <math.h>

#include "../gen/sprites.h"
#include "../net.h"
#include "../sprite.h"
#include "chars.h"
#include "../gen/sounds.h"

#define TAILS_ATTACK_RECHARGE (23 * 60)
#define ETAILS_ATTACK_RECHARGE (12 * 60)
#define TAILS_MAXSPEED 11
#define TAILS_ACC 0.046875f

// obj_tails_tail
typedef struct {
    bool tail_visible;
    int tail_sprite;
    float tail_x, tail_y, tail_xscale, tail_angle, tail_index;
} TailsVars;

static void init(Player *p)
{
    TailsVars *v = CHAR_VARS(p, TailsVars);
    v->tail_sprite = SPR_TAILS_TAIL2;
    v->tail_xscale = 1;
}

static int recharge(const Player *p) { return p->revivalTimes >= 2 ? ETAILS_ATTACK_RECHARGE : TAILS_ATTACK_RECHARGE; }

static void special(Player *p, const Keys *k)
{
    if (p->isHiding) {
        if (p->attackTimer <= 2 * 60) p->attackTimer = 2 * 60;
        if (p->flyTimer <= -420 + 2 * 60) p->flyTimer = -420 + 2 * 60;
    }

    if (!p->isGrounded && k->a_p && p->attackCharge <= 0 && p->attackAfter <= 0) {
        if (!p->isFlying && p->flyTimer <= -420) {
            p->flyTimer = 160;
            p->isFlying = true;
        }
        if (p->isFlying) {
            if (p->flyTimer > 0) p->flyGrv = -1;
            else p->isFlying = false;
            p->isJumping = false;
            if (fabsf(p->xspd) > 0) p->image_xscale = sgnf(p->xspd);
        }
    }

    if (p->isFlying) {
        p->isSpinning = false;
        if (p->flyGrv < 2) p->flyGrv += 0.035f;
        if (p->flyTimer <= 0) p->isFlying = false;
        p->yspd = p->flyGrv;
    }

    if (k->b_p && p->attackTimer == 0 && p->attackCharge == 0) {
        NetPacket pk;
        pkt_begin(&pk, CLIENT_TPROJECTILE_STARTCHARGE);
        net_send(&pk, true);
        p->attackTimer = recharge(p);
        p->attackCharge = 1;
    }

    if (p->attackCharge > 0) {
        p->emotion = false;
        p->attackTimer = recharge(p);
        p->attackCharge++;
        p->isJumping = p->isFlying = false;
        p->xspd = p->gspd = 0;
        p->canMove = false;
        if (k->left) p->image_xscale = -1;
        else if (k->right) p->image_xscale = 1;
        p->attackKDir = p->image_xscale;

        if (p->attackCharge >= 60 * 3.5 || !player_controls || p->isZipline || p->isHurt || p->isDead ||
            p->shockedTimer > 0 || !k->b) {
            // net_tails_spawn_projectile: the server spawns and moves the shot
            int val = (int)floorf(((p->attackCharge / 60.0f) / 3.5f) * 100);
            int dmg = 1 + p->attackCharge / 42;
            if (p->revivalTimes >= 2) dmg = p->attackCharge >= 126 ? 60 : p->attackCharge >= 84 ? 40 : 20;
            NetPacket pk;
            pkt_begin(&pk, CLIENT_TPROJECTILE);
            pkt_u16(&pk, (u16)(int)p->x);
            pkt_u16(&pk, (u16)(int)p->y);
            pkt_u8(&pk, (u8)(s8)p->image_xscale);
            pkt_u8(&pk, (u8)dmg);
            pkt_u8(&pk, p->revivalTimes >= 2);
            pkt_u8(&pk, (u8)ceilf(val / 20.0f));
            net_send(&pk, true);
            player_sound(p, SND_TAILS_SHOOT);  // TODO(M8): camera shake
            if (p->attackCharge < 84) { p->attackAfter = 60 * 0.4f; p->recoil = -p->attackKDir * 2; }
            else if (p->attackCharge < 126) { p->attackAfter = 60 * 0.5f; p->recoil = -p->attackKDir * 4; }
            else { p->attackAfter = 60 * 0.6f; p->recoil = -p->attackKDir * 6; }
        }
        if (p->isHurt || p->hp <= 0 || p->isDead || p->shockedTimer > 0 || p->isZipline) {
            p->isJumping = p->isFlying = p->isSpinning = false;
            p->canMove = true;
            p->attackCharge = 0;
            p->attackAfter = 0;
        }
    }

    if (p->attackAfter > 0) {
        if (p->isHurt || p->hp <= 0 || p->isDead || p->shockedTimer > 0 || p->isZipline) {
            p->isJumping = p->isFlying = p->isSpinning = false;
            p->canMove = true;
            p->attackAfter = 0;
            p->attackCharge = 0;
        }
        p->canSpin = false;
        p->attackAfter--;
        p->attackCharge = 0;
        p->isJumping = p->isFlying = p->isSpinning = false;
        p->canMove = true;
        p->xspd = p->recoil;
        p->gspd = p->recoil;
        p->recoil -= fminf(fabsf(p->recoil), p->acc * 3) * sgnf(p->recoil);
    } else {
        p->canSpin = true;
    }

    if (p->attackTimer > 0) p->attackTimer--;

    if (p->isGrounded) {
        if (p->isFlying) { p->isFlying = false; p->flyTimer = 0; }
        if (p->flyTimer > -420) p->flyTimer--;
    } else if (p->flyTimer > 0) {
        p->flyTimer--;
    }
}

// Step_0 movement: Tails only moves while canMove (charging stops him); otherwise only the
// special runs.
static void step_move(Player *p, const Keys *k)
{
    if (p->canMove) {
        if (p->shockedTimer <= 0) {
            player_move_basic(p, k);
        } else {
            p->recoil = 0;
            p->attackAfter = 0;
            p->canMove = true;
            player_stun_tick(p);
        }
    } else {
        special(p, k);
    }
    if (p->isHurt || p->hp <= 0 || p->isDead || p->shockedTimer > 0 || p->isZipline) {
        p->recoil = 0;
        p->attackCharge = 0;
        p->attackAfter = 0;
    }
}

static void tail_step(Player *p);

// Step_2
static void end_step(Player *p)
{
    if (!p->emotion) p->state = ST_IDLE;
    if (p->isDead || p->shockedTimer > 0) { p->state = ST_DEAD; goto tail; }
    if (p->attackCharge > 0 || p->attackAfter > 0) { p->state = ST_TAILS_ATTACK1; goto tail; }
    if (p->isHurt) { p->state = ST_HURT; goto tail; }
    if (p->isFlying) { p->state = ST_TAILS_FLY; goto tail; }
    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        else { p->state = ST_TAILS_ZIPLINE; goto tail; }
    }
    if (p->isLookingUp) { p->state = ST_LOOKUP; goto tail; }
    if (p->isLookingDown) { p->state = ST_LOOKDOWN; goto tail; }
    if (p->isSpinning) { p->state = ST_SPIN; goto tail; }
    if (fabsf(p->xspd) > 0 && p->isGrounded) p->state = fabsf(p->xspd) < 8 ? ST_WALK : ST_RUN;
    if (p->isOnEdge) { p->state = ST_BALANCING; goto tail; }
    if (!p->isGrounded) p->state = p->isJumping ? ST_JUMP : ST_FALL;
tail:
    tail_step(p);
}

// Other_7. The emotion keys held keep the taunt looping.
static void animation_end(Player *p)
{
    switch (p->state) {
    case ST_IDLE: p->image_index = 70; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: {
        int i = p->state - ST_EMOTION1;
        if (p->emHeld[i]) {
            p->image_index = i == 2 ? player_frames(p) - 8 : 0;
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
    case ST_HURT: p->image_speed = 0; p->image_index = 0; break;
    case ST_BALANCING: p->image_xscale = p->edgeDir; p->image_speed = 1; break;
    case ST_FALL: p->image_index = p->yspd > 0 ? 1 : 0; break;
    case ST_WALK: p->image_speed = fabsf(p->xspd) / p->maxHSpeed; break;
    case ST_RUN:
    case ST_JUMP: p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f); break;
    case ST_DEAD:
        if (p->revivalTimes >= 2) { p->image_speed = 0.5f; break; }
        p->image_speed = p->isGrounded ? 1 : 0;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;
    case ST_LOOKUP:
    case ST_LOOKDOWN:
        p->image_speed = 0.2f;
        if (p->image_index >= 1) p->image_index = 1;
        break;
    case ST_SPIN: p->image_speed = fabsf(p->xspd) / 5; break;
    case ST_TAILS_FLY: p->image_speed = 2; break;
    case ST_TAILS_ZIPLINE: p->image_speed = 0; p->image_index = 0; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: p->image_speed = 1; break;
    }
}

// obj_tails_tail End Step
static void tail_step(Player *p)
{
    TailsVars *v = CHAR_VARS(p, TailsVars);
    bool demon = p->revivalTimes >= 2;
    v->tail_visible = false;
    v->tail_angle = 0;
    v->tail_x = p->x;
    v->tail_y = p->y;
    v->tail_index += 0.2f * sprite_frame_step(v->tail_sprite);
    if (p->state == ST_HURT || p->hp <= 0) return;
    if (p->state == ST_BALANCING) {
        v->tail_xscale = p->edgeDir;
        v->tail_x += v->tail_xscale * 4;
        v->tail_y += 8;
        v->tail_visible = true;
        v->tail_sprite = demon ? SPR_ETAILS_TAIL1 : SPR_TAILS_TAIL1;
        return;
    }
    if (p->state == ST_IDLE || p->emotion || p->state == ST_LOOKDOWN || p->state == ST_LOOKUP) {
        v->tail_xscale = p->image_xscale;
        v->tail_x += v->tail_xscale * 2;
        v->tail_y += 4;
        v->tail_visible = true;
        v->tail_sprite = demon ? SPR_ETAILS_TAIL2 : SPR_TAILS_TAIL2;
        return;
    }
    if (p->state == ST_JUMP || p->state == ST_SPIN) {
        v->tail_y += 6;
        v->tail_xscale = 1;
        v->tail_visible = true;
        v->tail_angle = -atan2f(p->yspd, p->isGrounded ? p->gspd : p->xspd) * 180.0f / (float)M_PI;
        v->tail_sprite = demon ? SPR_ETAILS_TAIL1 : SPR_TAILS_TAIL1;
    }
}

// Draw_0 (the tail object draws first, one depth behind)
static void draw(const Player *p, float cam_x, float cam_y)
{
    const TailsVars *v = CHAR_VARS_C(p, TailsVars);
    float alpha = (p->hurttime > 0 || p->isHiding) ? 0.5f : 1.0f;
    if (v->tail_visible)
        sprite_draw(v->tail_sprite, v->tail_index, v->tail_x - cam_x, v->tail_y - cam_y, v->tail_xscale, 1,
                    v->tail_angle, 0xFFFFFFFF, alpha);
    float angle = (p->attackCharge > 0 || p->isSpinning) ? 0 : p->angle * 180.0f / (float)M_PI;
    player_draw_body(p, cam_x, cam_y, angle);
    if (p->attackCharge > 0)
        sprite_draw(SPR_TAILSCHARGE, floorf(((p->attackCharge / 60.0f) / 3.5f) * 100),
                    p->x + p->image_xscale * 24 - cam_x, p->y - cam_y, p->image_xscale, 1, 0, 0xFFFFFFFF, 1);
}

static void on_hurt(Player *p) { p->attackAfter = 0; }

static void on_spring(Player *p)
{
    if (p->isFlying) {
        p->flyTimer = 0;
        p->isFlying = false;
    }
}

// net_state_game SERVER_GAME_DEATHTIMER_END: ability timers restart when demonized
static void on_demonize(Player *p)
{
    p->attackTimer = 0;
    p->flyTimer = -420;
}

// obj_playerui
static void draw_gui(const Player *p)
{
    gui_ability(SPR_GUI_TAILSFLY, p->revivalTimes >= 2, fminf(p->flyTimer, 0) / -420.0f, 10, 240, p->flyTimer <= -420);
    float prog = 1.0f - p->attackTimer / (float)recharge(p);
    gui_ability(SPR_GUI_TAILSATTACK, 0, prog, 12, 250, prog >= 1);
}

const CharDef CHAR_TAILS_DEF = {
    .draw_gui = draw_gui,
    .on_demonize = on_demonize,
    .name = "tails",
    .init = init,
    .special = special,
    .step_move = step_move,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .on_hurt = on_hurt,
    .on_spring = on_spring,
    .base_acc = TAILS_ACC,
    .base_maxspeed = TAILS_MAXSPEED,
};

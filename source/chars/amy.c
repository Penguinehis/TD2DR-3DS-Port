// obj_amy + scr_amy_special.
#include <math.h>
#include <stdlib.h>

#include "../net.h"
#include "chars.h"
#include "../gen/sprites.h"
#include "../entities.h"
#include "../gen/sounds.h"

#define AMY_ATTACK_RECHARGE (60 * 20)
#define AMY_EXEATTACK_RECHARGE (60 * 5)
#define AMY_BIGJUMP_RECHARGE (60 * 9)
#define AMY_MAXSPEED 11
#define AMY_ACC 0.046875f

// Step_2 state macros
#define AMY_ATTACK (ST_BALANCING + 1)
#define AMY_HJUMP (ST_BALANCING + 2)
#define AMY_SEX (ST_BALANCING + 3)
#define AMY_ZIPLINE (ST_BALANCING + 4)

// Indices in global.net_effs (net_projectile.gml)
#define NET_EFF_ROSEHEART 9
#define NET_EFF_EROSEHEART 10

typedef struct {
    int hjumpTimer;
    bool isHJ;
} AmyVars;

// Create_0 (only what differs from player_init)
static void init(Player *p) { p->canSpin = false; }

// net_quick_effect(xx, yy, spr, fade, dir, xspd, yspd) with the default spd 0.5; eff is the
// global.net_effs index (9 spr_roseheart, 10 spr_eroseheart).
static void quick_effect(float xx, float yy, int eff, bool fade, int dir, int xspd, int yspd)
{
    net_quick_effect(xx, yy, eff == 10 ? SPR_EROSEHEART : SPR_ROSEHEART, fade, dir, xspd, yspd, 0.5f);
}

// scr_amy_special (global.playerControls: the keys are already neutral when false)
static void special(Player *p, const Keys *k)
{
    AmyVars *v = CHAR_VARS(p, AmyVars);

    if (p->redRingTimer > 0) {
        if (p->attackTimer <= 2) p->attackTimer = 2;
        if (v->hjumpTimer <= 2) v->hjumpTimer = 2;
        p->isAttacking = false;
    }

    if (p->isHiding) {
        if (p->attackTimer <= 60 * 2) p->attackTimer = 60 * 2;
        if (v->hjumpTimer <= 60 * 2) v->hjumpTimer = 60 * 2;
        p->isAttacking = false;
    }

    if (p->isLookingDown && k->a_p && v->hjumpTimer <= 0) {
        p->isGrounded = false;
        p->isSpinning = false;
        p->isJumping = true;
        p->justJumped = true;
        p->xspd -= 10 * sinf(p->angle);
        p->yspd = -10 * cosf(p->angle);
        v->isHJ = true;
        v->hjumpTimer = AMY_BIGJUMP_RECHARGE;
        player_sound(p, SND_JUMP);
    }

    if (k->b_p && p->attackTimer <= 0) {
        if (k->left) p->image_xscale = -1;
        else if (k->right) p->image_xscale = 1;

        for (int i = 0; i < 4; i++)
            quick_effect(p->x + 2 + (i * 10) * p->image_xscale, p->y - 4 + (rand() % 7 - 3),
                         p->revivalTimes >= 2 ? NET_EFF_EROSEHEART : NET_EFF_ROSEHEART, false, 1, 0, -1);

        p->image_index = 0;
        p->attackTimer = p->revivalTimes >= 2 ? AMY_EXEATTACK_RECHARGE : AMY_ATTACK_RECHARGE;
        p->isAttacking = true;
        player_sound(p, SND_DASH);
    }

    if (p->isAttacking) p->gspd = 0;

    if (p->state == AMY_HJUMP && p->isGrounded) {
        p->state = ST_IDLE;
        p->isAttacking = false;
    }

    if (v->hjumpTimer > 0 && p->isGrounded) v->hjumpTimer--;
    if (p->attackTimer > 0) p->attackTimer--;
}

// Step_2
static void end_step(Player *p)
{
    const AmyVars *v = CHAR_VARS_C(p, AmyVars);
    if (!p->emotion) p->state = ST_IDLE;
    if (p->isDead) { p->state = ST_DEAD; return; }
    if (p->shockedTimer > 0) { p->state = ST_DEAD; return; }
    if (v->isHJ) { p->state = AMY_HJUMP; return; }
    if (p->isAttacking) { p->state = AMY_ATTACK; return; }
    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        else { p->state = AMY_ZIPLINE; return; }
    }
    if (p->isHurt) { p->state = ST_HURT; return; }
    if (p->isLookingUp) { p->state = ST_LOOKUP; return; }
    if (p->isLookingDown) { p->state = ST_LOOKDOWN; return; }
    if (p->isSpinning) { p->state = ST_SPIN; return; }
    if (fabsf(p->xspd) > 0 && p->isGrounded) p->state = fabsf(p->xspd) < 8 ? ST_WALK : ST_RUN;
    if (p->isOnEdge) { p->state = ST_BALANCING; return; }
    if (!p->isGrounded) p->state = p->isJumping ? ST_JUMP : ST_FALL;
}

// Other_7. The emotion keys held keep the taunt looping.
static void animation_end(Player *p)
{
    switch (p->state) {
    case ST_IDLE: p->image_index = player_frames(p) - 4; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: {
        int i = p->state - ST_EMOTION1;
        if (p->emHeld[i]) {
            p->image_index = i == 2 ? player_frames(p) - 10 : 0;
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
    AmyVars *v = CHAR_VARS(p, AmyVars);
    int spr = player_anim_sprite(p);
    if (spr >= 0) p->sprite = spr;
    int frames = player_frames(p);
    switch (p->state) {
    case ST_IDLE: p->image_speed = 1; break;
    case ST_HURT: p->image_speed = 0; p->image_index = 0; break;
    case ST_BALANCING: p->image_xscale = p->edgeDir; p->image_speed = 1; break;
    case ST_WALK: p->image_speed = 0.2f + fabsf(p->xspd) / p->maxHSpeed; break;
    case ST_FALL: p->image_index = 0; break;
    case ST_RUN: p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f); break;
    case ST_JUMP:
        p->image_speed = 1.1f;
        if (p->image_index >= frames - 1) p->isJumping = false;
        break;
    case ST_DEAD:
        if (p->revivalTimes >= 2) { p->image_speed = 0.5f; break; }
        p->image_speed = p->isGrounded ? 1 : 0;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;
    case ST_LOOKUP:
    case ST_LOOKDOWN:
        p->image_speed = 1;
        if (p->image_index >= 1) p->image_index = 1;
        break;
    case ST_SPIN: p->image_speed = fabsf(p->xspd) / 5; break;
    case AMY_ATTACK:
        p->image_speed = 0.3f;
        if (p->image_index >= frames - 1) p->isAttacking = false;
        break;
    case AMY_HJUMP:
        p->image_speed = 0.3f;
        if (p->image_index >= frames - 1) v->isHJ = false;
        break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: p->image_speed = 1; break;
    }
}

// Draw_0 (effectTime afterimages and the palette swap: TODO(M8) in player_draw)
static void draw(const Player *p, float cam_x, float cam_y)
{
    player_draw_body(p, cam_x, cam_y, p->isSpinning ? 0 : p->angle * 180.0f / (float)M_PI);
}

// scr_move_basic: the ground jump returns while attacking
static bool jump_blocked(const Player *p) { return p->isAttacking; }

// scr_collision_check_bottom: isHJ = false once the floor angle is set
static void on_land_angle(Player *p) { CHAR_VARS(p, AmyVars)->isHJ = false; }

// scr_collision_objects_after: spring / hd spring switch (isJumping is cleared right after
// by the shared launch code, as in the GML)
static void on_spring(Player *p)
{
    if (p->isAttacking) {
        p->attackTimer = p->revivalTimes >= 2 ? AMY_EXEATTACK_RECHARGE : AMY_ATTACK_RECHARGE;
        p->isAttacking = false;
    }
    p->isJumping = true;
}

// Alarm_4 has no extras (acc / maxHSpeed / isSlow are restored by player_step).

// obj_netclient Step_2: PLAYER_ATTACKING
static u8 net_flags(const Player *p) { return p->isAttacking ? NETF_ATTACKING : 0; }

// net_state_game SERVER_GAME_DEATHTIMER_END: ability timers restart when demonized
static void on_demonize(Player *p)
{
    AmyVars *v = CHAR_VARS(p, AmyVars);
    p->attackTimer = 0;
    v->hjumpTimer = 0;
}

// obj_playerui
static void draw_gui(const Player *p)
{
    const AmyVars *v = CHAR_VARS_C(p, AmyVars);
    float prog = 1.0f - fmaxf(v->hjumpTimer, 0) / (float)AMY_BIGJUMP_RECHARGE;
    gui_ability(SPR_GUI_AMYHJUMP, 0, prog, 10, 240, prog >= 1);
    int val = p->revivalTimes >= 2 ? AMY_EXEATTACK_RECHARGE : AMY_ATTACK_RECHARGE;
    prog = 1.0f - fmaxf(p->attackTimer, 0) / (float)val;
    gui_ability(SPR_GUI_AMYATTACK, 0, prog, 10, 258, prog >= 1);
}

const CharDef CHAR_AMY_DEF = {
    .draw_gui = draw_gui,
    .on_demonize = on_demonize,
    .name = "amy",
    .init = init,
    .special = special,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .jump_blocked = jump_blocked,
    .on_land_angle = on_land_angle,
    .on_spring = on_spring,
    .base_acc = AMY_ACC,
    .base_maxspeed = AMY_MAXSPEED,
    .net_flags = net_flags,
};

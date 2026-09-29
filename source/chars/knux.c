// obj_knux + scr_knux_special.
#include <math.h>

#include "../gen/rooms.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../room.h"
#include "chars.h"
#include "../gen/sounds.h"

#define KNUX_ATTACK_RECHARGE (60 * 20)
#define KNUX_EXEATTACK_RECHARGE (60 * 5)
#define KNUX_GLIDE_RECHARGE (60 * 15)
#define KNUX_MAXSPEED 11
#define KNUX_ACC 0.046835f

// Step_2 state macros
#define KNUX_ATTACK (ST_BALANCING + 1)
#define KNUX_AIR_ATTACK (ST_BALANCING + 2)
#define KNUX_GLIDE (ST_BALANCING + 3)
#define KNUX_ZIPLINE (ST_BALANCING + 4)
#define KNUX_STUCK (ST_BALANCING + 5)

typedef struct {
    int glideTimer, glideTimeout;
    float glide_xspd;
    bool isGliding, isStuck;
} KnuxVars;

// Create_0 (only what differs from player_init)
static void init(Player *p) { p->jumpForce = 6.5f; }

// The repeated "if(isStuck) { canMove = true; isStuck = false; }"
static void unstick(Player *p)
{
    KnuxVars *v = CHAR_VARS(p, KnuxVars);
    if (v->isStuck) {
        p->canMove = true;
        v->isStuck = false;
    }
}

// scr_knux_special (global.playerControls: the keys are already neutral when false)
static void special(Player *p, const Keys *k)
{
    KnuxVars *v = CHAR_VARS(p, KnuxVars);

    if (p->redRingTimer > 0) {
        if (p->attackTimer <= 2) p->attackTimer = 2;
        if (v->glideTimer <= 2) v->glideTimer = 2;
        p->isAttacking = false;
    }

    if (p->isHiding) {
        if (p->attackTimer <= 60 * 2) p->attackTimer = 60 * 2;
        if (v->glideTimer <= 60 * 2) v->glideTimer = 60 * 2;
        p->isAttacking = false;
    }

    if (k->a_p && !p->isGrounded && !v->isGliding && v->glideTimer <= 0) {
        p->isAttacking = false;
        p->isJumping = false;
        p->isSpinning = false;
        v->isGliding = true;
        v->glideTimer = KNUX_GLIDE_RECHARGE;
        v->glide_xspd = p->image_xscale * 5;
    }

    if (k->b_p && p->attackTimer <= 0) {
        if (k->left) p->image_xscale = -1;
        else if (k->right) p->image_xscale = 1;
        p->image_index = 0;
        p->attackTimer = p->revivalTimes >= 2 ? KNUX_EXEATTACK_RECHARGE : KNUX_ATTACK_RECHARGE;
        p->isAttacking = true;
        player_sound(p, SND_DASH);
    }

    if (p->isAttacking) {
        if (v->isGliding) {
            v->isGliding = false;
            unstick(p);
        }
        p->gspd = 0;
        p->xspd = p->image_xscale * (p->isGrounded ? 0.5f : 2);
    }

    if (p->isGrounded || p->isHurt) {
        v->isGliding = false;
        unstick(p);
    }

    if (v->isGliding) {
        p->isAttacking = false;
        v->glideTimeout++;
        if (v->glideTimeout >= 5 * 60) {
            v->glideTimeout = 0;
            v->isGliding = false;
            unstick(p);
        }

        if (k->a) {
            if (!v->isStuck) {
                if (k->left) {
                    if (v->glide_xspd > -5) v->glide_xspd -= p->acc * 2;
                } else if (k->right) {
                    if (v->glide_xspd < 5) v->glide_xspd += p->acc * 2;
                }
                switch ((int)floorf(v->glide_xspd)) {
                case 5: case 4: case 3: case 2: p->image_index = 0; break;
                case 1: p->image_index = 1; break;
                case 0: p->image_index = 2; break;
                case -1: p->image_index = 3; break;
                case -5: case -4: case -3: case -2: p->image_index = 4; break;
                }
            }
        } else {
            v->isGliding = false;
            unstick(p);
        }

        p->image_xscale = 1;
        p->yspd = 1;
        p->xspd = v->glide_xspd;
    } else {
        unstick(p);
    }

    if (p->attackTimer > 0) p->attackTimer--;
    if (v->glideTimer > 0 && !v->isGliding) v->glideTimer--;
}

// Step_2
static void end_step(Player *p)
{
    const KnuxVars *v = CHAR_VARS_C(p, KnuxVars);
    if (!p->emotion) p->state = ST_IDLE;
    if (p->isDead) { p->state = ST_DEAD; return; }
    if (p->shockedTimer > 0) { p->state = ST_DEAD; return; }
    if (p->isAttacking) {
        // Inverted in the GML (grounded uses KNUX_AIR_ATTACK); kept as is.
        p->state = p->isGrounded ? KNUX_AIR_ATTACK : KNUX_ATTACK;
        return;
    }
    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        else { p->state = KNUX_ZIPLINE; return; }
    }
    if (p->isHurt) { p->state = ST_HURT; return; }
    if (v->isStuck) { p->state = KNUX_STUCK; return; }
    if (v->isGliding) { p->state = KNUX_GLIDE; return; }
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
    case ST_IDLE:
    case ST_BALANCING: p->image_index = player_frames(p) - 4; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: {
        int i = p->state - ST_EMOTION1;
        if (p->emHeld[i]) {
            p->image_index = i == 1 ? 0 : 2;
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
    if (p->revivalTimes < 2 && p->state == ST_IDLE && level.room_id == ROOM_MARIJUNA) p->sprite = SPR_KNUX_MARIJUNA;
    int frames = player_frames(p);
    switch (p->state) {
    case ST_IDLE: p->image_speed = 1; break;
    case ST_HURT: p->image_speed = 0; p->image_index = 0; break;
    case ST_BALANCING: p->image_xscale = p->edgeDir; p->image_speed = 1; break;
    case ST_WALK: p->image_speed = 0.2f + fabsf(p->xspd) / p->maxHSpeed; break;
    case ST_FALL: p->image_index = p->yspd > 0 ? 1 : 0; break;
    case ST_RUN:
    case ST_JUMP: p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f); break;
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
    case KNUX_ATTACK:
        p->image_speed = 0.1f;
        if (p->image_index >= frames - 1) p->isAttacking = false;
        break;
    case KNUX_AIR_ATTACK:
        p->image_speed = 1.5f;
        if (p->image_index >= frames - 1) {
            p->isAttacking = false;
            p->isGrounded = false;
        }
        break;
    case KNUX_GLIDE: p->image_speed = 0; p->image_xscale = 1; break;
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

// scr_move_basic: isBoosting
static void on_boost(Player *p)
{
    CHAR_VARS(p, KnuxVars)->isGliding = false;
    unstick(p);
}

// scr_move_basic: the ground jump returns while attacking
static bool jump_blocked(const Player *p) { return p->isAttacking; }

// scr_collision_check: gliding into a wall grabs it (canStuck walls only)
static void on_wall(Player *p, WorldInst *wall, int dir)
{
    KnuxVars *v = CHAR_VARS(p, KnuxVars);
    if (v->isGliding && v->glideTimer > 0) {
        bool canStuck = !(object_info(wall->object)->flags & OBJF_NOSTICK);
        if (canStuck && !v->isStuck && !p->isHurt) {
            player_sound(p, SND_SNAP);
            v->isStuck = true;
        }
        if (v->isStuck && !p->isHurt) {
            p->image_xscale = dir;  // left wall -1, right wall 1
            p->yspd = 0;
        }
    }
}

// scr_collision_objects_after: spring / hd spring switch
static void on_spring(Player *p)
{
    KnuxVars *v = CHAR_VARS(p, KnuxVars);
    if (v->isGliding) {
        v->glideTimer = KNUX_GLIDE_RECHARGE;
        v->isGliding = false;
        unstick(p);
    }
    if (p->isAttacking) {
        p->attackTimer = p->revivalTimes >= 2 ? KNUX_EXEATTACK_RECHARGE : KNUX_ATTACK_RECHARGE;
        p->isAttacking = false;
    }
}

// Alarm_4 has no extras (acc / maxHSpeed / isSlow are restored by player_step).

// obj_netclient Step_2: PLAYER_ATTACKING
static u8 net_flags(const Player *p) { return p->isAttacking ? NETF_ATTACKING : 0; }

// net_state_game SERVER_GAME_DEATHTIMER_END: ability timers restart when demonized
static void on_demonize(Player *p)
{
    KnuxVars *v = CHAR_VARS(p, KnuxVars);
    p->attackTimer = 0;
    v->glideTimer = 0;
}

const CharDef CHAR_KNUX_DEF = {
    .on_demonize = on_demonize,
    .name = "knux",
    .init = init,
    .special = special,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .on_boost = on_boost,
    .jump_blocked = jump_blocked,
    .on_wall = on_wall,
    .on_spring = on_spring,
    .base_acc = KNUX_ACC,
    .base_maxspeed = KNUX_MAXSPEED,
    .net_flags = net_flags,
};

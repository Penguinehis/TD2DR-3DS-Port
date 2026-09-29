// obj_chaos + scr_chaos_special, and the EXE_CHAOS branches of scr_move_basic,
// scr_collision_basic (wall / floor "stuck") and scr_collision_objects (springs).
#include <math.h>

#include "chars.h"
#include "../entities.h"
#include "../gen/sounds.h"  // first: gen/anims.h needs the ctru types

#include "../gen/anims.h"
#include "../gen/sprites.h"
#include "../room.h"
#include "../sprite.h"

#define CHAOS_MAXSPEED 12
#define CHAOS_ACC 0.056875f
#define CHAOS_INVIS_RECHARGE (60 * 20)
#define CHAOS_INVIS_DURATION (60 * 15)

// Step_2 states (BALANCING + n)
#define CHAOS_ATTACK (ST_BALANCING + 1)
#define CHAOS_AIR_ATTACK (ST_BALANCING + 2)
#define CHAOS_SHOCKED (ST_BALANCING + 3)
#define CHAOS_WON (ST_BALANCING + 4)
#define CHAOS_LOST (ST_BALANCING + 5)
#define CHAOS_LOST2 (ST_BALANCING + 6)
#define CHAOS_ZIPLINE (ST_BALANCING + 7)
#define CHAOS_STUCKBALLS (ST_BALANCING + 8)
#define CHAOS_STUCKDICKS (ST_BALANCING + 9)
#define CHAOS_TRANSFORM (ST_BALANCING + 10)
#define CHAOS_AIRTRANSFORM (ST_BALANCING + 11)

// obj_exe's EXE_WON / EXE_LOST (scr_collision_check_angle compares the state with these)
#define EXE_WON_STATE (ST_BALANCING + 4)
#define EXE_LOST_STATE (ST_BALANCING + 5)

typedef struct {
    // Set by SERVER_GAME_EXE_WINS / SERVER_GAME_SURVIVOR_WIN in the GML (global.player.won/lost).
    // TODO(shared): nothing sets these yet; they should become Player fields.
    bool prevGrounded;
    int lostState;
    float dashDirX, dashDirY, attackSpeed;
    int dashedIntoPussy;
    int stuckTimer, stuckDir, stuckDashTimer;
    bool leftPressed, rightPressed, upPressed;
    int slimeTimer;
    bool slimeAnim;
    // keyboard_check(KeyLeft/Right/Up) this frame, read by the collision hooks, and the
    // previous frame's state for keyboard_check_released (captured in step_move every frame).
    bool keyLeft, keyRight, keyUp;
    bool prevLeft, prevRight, prevUp;
    bool relLeft, relRight, relUp;
} ChaosVars;

static void init(Player *p)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    p->hp = 10000;
    p->canSpinDash = false;
    v->prevGrounded = true;
    v->slimeTimer = -CHAOS_INVIS_RECHARGE;
    v->lostState = (rand() & 1) ? CHAOS_LOST2 : CHAOS_LOST;  // choose(CHAOS_LOST, CHAOS_LOST2)
    // obj_exe_indicator / obj_exe_sprindicator: HUD, not ported here.
}

// scr_chaos_special
static void special(Player *p, const Keys *k)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);

    if (player_controls && p->canMove && p->isJumping && k->a_p && !p->isZipline && v->slimeTimer <= 0)
        p->isJumping = false;

    if (player_controls && p->canMove && k->b_p && !p->isZipline && v->stuckDashTimer <= 0 && p->attackTimer <= 0 &&
        v->slimeTimer <= 0) {
        if (k->left) p->image_xscale = -1;
        else if (k->right) p->image_xscale = 1;

        if (!p->isGrounded) {
            v->dashDirX = p->image_xscale;
            v->dashDirY = 0;
            p->isJumping = false;

            if (k->left) v->dashDirX = -1;
            else if (k->right) v->dashDirX = 1;

            if (k->up) v->dashDirY = -1;
            else if (k->down) v->dashDirY = 1;

            v->dashedIntoPussy = 24;
            p->effectTime = 20;
            // obj_achivements.alarm[5]: achievements omitted
            player_sound(p, SND_CHAOS_DASH);
        } else {
            player_sound(p, SND_CHAOS_ATTACK);
        }

        p->image_index = 0;
        v->attackSpeed = p->xspd;
        p->attackTimer = p->isGrounded ? 90 : 150;
        p->isAttacking = true;
        p->isJumping = false;
        p->isSpinning = false;
    }

    if (player_controls && p->canMove && k->c_p && !p->isZipline && v->slimeTimer <= -CHAOS_INVIS_RECHARGE) {
        player_sound(p, SND_CHAOS_STURN);
        v->slimeAnim = false;
        v->slimeTimer = CHAOS_INVIS_DURATION;
        p->acc = CHAOS_ACC * 1.5f;
        p->maxHSpeed = CHAOS_MAXSPEED * 1.2f;

        p->canLookDown = false;
        p->canLookUp = false;
        p->canSpinDash = false;
        p->canSpin = false;
    }

    if (v->relRight) v->rightPressed = false;
    if (v->relLeft) v->leftPressed = false;
    if (v->relUp) v->upPressed = false;

    if (v->slimeTimer > 0) {
        if (v->slimeTimer <= CHAOS_INVIS_DURATION - 10) {
            if (player_controls && k->c_p) v->slimeTimer = 0;
        }

        if (!p->isGrounded && p->effectTime <= 0) p->effectTime = 6;

        if (p->shockedTimer > 0 || p->won || p->lost || p->isZipline) v->slimeTimer = 0;
    }

    if (v->slimeTimer == 0) {
        player_sound(p, SND_CHAOS_STURN);
        p->acc = CHAOS_ACC;
        p->maxHSpeed = CHAOS_MAXSPEED;
        p->canLookDown = true;
        p->canLookUp = true;
        p->canSpinDash = true;
        p->canSpin = true;
    }

    if (p->attackTimer > 0 && v->stuckTimer <= 0) p->attackTimer--;

    if (v->slimeTimer > -CHAOS_INVIS_RECHARGE) v->slimeTimer--;

    if (p->isAttacking) {
        if (!p->isGrounded || v->stuckDashTimer > 0) {
            p->xspd = v->dashDirX * 12;
            p->yspd = v->dashDirY * 8;

            if (p->attackTimer <= 150 - 15) {
                p->xspd = v->dashDirX * 2;
                p->yspd = v->dashDirY * 2;
                p->isAttacking = false;
                p->effectTime = 0;
            }
            return;
        }

        if (p->yspd > 0) p->yspd = p->isGrounded ? 0 : 2.5f;

        if (p->isGrounded && fabsf(p->xspd) > 0 && (int)floorf(p->x) % 4 == 0) {
            net_quick_effect(p->x - 8 * sgnf(p->image_xscale), p->y + 20, SPR_DUST, false, 1, 0, 0, 0.5f);
        }
    }

    if (v->dashedIntoPussy > 0) v->dashedIntoPussy--;

    if (v->stuckTimer > 0) {
        if ((v->prevGrounded && !p->isGrounded) || (!p->isGrounded && fabsf(p->xspd) > 0) || p->isBoosting ||
            p->isZipline || p->isHurt || p->shockedTimer > 0 || p->won || p->lost) {
            v->stuckTimer = 0;
            p->canMove = true;
            p->canSpin = true;
            return;
        }

        if (p->isGrounded && fabsf(p->xspd) > 0 && (int)floorf(p->x) % 4 == 0) {
            net_quick_effect(p->x - 8 * sgnf(p->image_xscale), p->y + 20, SPR_DUST, false, 1, 0, 0, 0.5f);
        }

        p->canSpin = false;
        p->canMove = false;
        p->isJumping = false;
        p->isAttacking = false;
        p->isSpinning = false;
        p->image_xscale = v->stuckDir;

        if (p->isGrounded) {
            p->gspd -= fminf(fabsf(p->gspd), p->acc * 8) * sgnf(p->gspd);
            p->xspd -= fminf(fabsf(p->xspd), p->acc * 8) * sgnf(p->xspd);
            p->xspd -= 0.225f * sinf(p->angle);
        } else {
            p->yspd = 0;
        }

        bool check = k->b_p;
        bool check2 = false;
        bool check3 = true;

        if (p->isGrounded && v->stuckDir > 0 && k->right) check2 = true;
        if (p->isGrounded && v->stuckDir < 0 && k->left) check2 = true;
        if (!p->isGrounded && k->up) check2 = true;

        if (p->isGrounded && v->stuckDir > 0 && v->rightPressed) check3 = false;
        if (p->isGrounded && v->stuckDir < 0 && v->leftPressed) check3 = false;
        if (!p->isGrounded && v->upPressed) check3 = false;

        if (v->stuckTimer-- >= 30 / 2 && check && check2 && check3) {
            p->canSpin = true;
            p->canMove = true;
            v->dashedIntoPussy = 0;
            v->stuckDashTimer = 8;
            v->stuckTimer = 0;
            v->dashDirX = p->isGrounded ? p->image_xscale : 0;
            v->dashDirY = p->isGrounded ? 0 : -1.4f;
            p->isJumping = false;

            if (!p->isGrounded) {
                p->image_xscale = -v->stuckDir;
                p->attackTimer = 150;
                p->isAttacking = true;
            }

            p->image_index = 0;
            v->attackSpeed = p->xspd;
            p->isJumping = false;
            p->isSpinning = false;

            p->effectTime = 20;
            // obj_achivements.alarm[5]: achievements omitted
            player_sound(p, SND_CHAOS_DASH);
        }

        if (v->stuckTimer == 0) {
            p->canMove = true;
            p->canSpin = true;
        }
    }

    if (v->stuckDashTimer > 0) {
        if ((v->prevGrounded && !p->isGrounded) || p->isBoosting || p->isZipline || p->isHurt || p->shockedTimer > 0 ||
            p->won || p->lost) {
            p->isAttacking = false;
            v->stuckDashTimer = 0;
            p->canMove = true;
            return;
        }

        if (p->isGrounded) {
            p->gspd = p->image_xscale * 12;
            p->isSpinning = true;
        } else {
            p->image_xscale = -v->stuckDir;
        }

        v->stuckDashTimer--;
        if (v->stuckDashTimer == 0) p->isAttacking = false;
    }
}

// Step_0 up to scr_collision_basic.
static void step_move(Player *p, const Keys *k)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);

    // Draw_0 "hakc": if(isDead) { xspd = 0; gspd = 0; } runs at the end of the previous frame.
    if (p->isDead) {
        p->xspd = 0;
        p->gspd = 0;
    }

    // keyboard_check / keyboard_check_released for the special and the collision hooks.
    v->relLeft = v->prevLeft && !k->left;
    v->relRight = v->prevRight && !k->right;
    v->relUp = v->prevUp && !k->up;
    v->keyLeft = v->prevLeft = k->left;
    v->keyRight = v->prevRight = k->right;
    v->keyUp = v->prevUp = k->up;

    if (p->canMove || p->shockedTimer > 0) {
        if (p->shockedTimer <= 0) {
            player_move_basic(p, k);
        } else {
            player_stun_tick(p);

            if (v->slimeTimer > 0) v->slimeTimer = 0;

            if (v->stuckTimer > 0) {
                p->canMove = true;
                v->stuckTimer = 0;
            }

            v->stuckDashTimer = 0;
        }
    } else {
        special(p, k);
    }

    if (p->won || p->lost) {
        p->gspd = 0;
        p->xspd = 0;
        p->angle = 0;
    }

    v->prevGrounded = p->isGrounded;
    // scr_collision_basic follows (shared). The obj_player_puppet bounce check after it is
    // TODO(shared): see the port report.
}

// Step_2
static void end_step(Player *p)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);

    if (!p->emotion) p->state = ST_IDLE;

    if (p->won && p->isGrounded) { p->state = CHAOS_WON; return; }
    if (p->lost && p->isGrounded) { p->state = v->lostState; return; }

    if (v->stuckTimer > 0) {
        p->state = p->isGrounded ? CHAOS_STUCKBALLS : CHAOS_STUCKDICKS;
        return;
    }

    if (p->isHurt) { p->state = ST_HURT; return; }

    if (v->slimeTimer > 0 && !v->slimeAnim) {
        p->state = !p->isGrounded ? CHAOS_AIRTRANSFORM : CHAOS_TRANSFORM;
        return;
    }

    if (p->shockedTimer > 0) {
        p->state = !p->isGrounded ? ST_HURT : CHAOS_SHOCKED;
        return;
    }

    if (p->isAttacking) {
        if (p->isGrounded) {
            if (!v->prevGrounded) {
                p->isAttacking = false;
                return;
            }
            p->state = CHAOS_ATTACK;
        } else {
            if (v->prevGrounded) {
                p->isAttacking = false;
                return;
            }
            p->state = CHAOS_AIR_ATTACK;
        }
        return;
    }

    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        p->state = CHAOS_ZIPLINE;
        return;
    }

    if (p->isLookingUp) { p->state = ST_LOOKUP; return; }
    if (p->isLookingDown) { p->state = ST_LOOKDOWN; return; }
    if (p->isSpinning) { p->state = ST_SPIN; return; }

    if (fabsf(p->xspd) > 0 && p->isGrounded) p->state = fabsf(p->xspd) < 8 ? ST_WALK : ST_RUN;

    if (p->isOnEdge) { p->state = ST_BALANCING; return; }

    if (!p->isGrounded) p->state = p->isJumping ? ST_JUMP : ST_FALL;
}

// Other_7
static void animation_end(Player *p)
{
    switch (p->state) {
    case ST_IDLE: p->image_index = player_frames(p) - 4; break;
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
    case ST_EMOTION1:
        if (p->emHeld[0]) {
            p->image_index = player_frames(p) - 1;
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
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    int idx = p->state + (v->slimeTimer > 0 && v->slimeAnim ? CHAOS_AIRTRANSFORM + 1 : 0);
    if (idx >= 0 && idx < ANIMS_EXE_COUNT[EXE_CHAOS] && ANIMS_EXE[EXE_CHAOS][idx] >= 0)
        p->sprite = ANIMS_EXE[EXE_CHAOS][idx];
    int frames = player_frames(p);

    switch (p->state) {
    case ST_IDLE: p->image_speed = 1; break;

    case ST_HURT:
    case CHAOS_ZIPLINE:
        if (v->slimeTimer > 0) {
            p->image_speed = 1;
            break;
        }
        p->image_speed = 0;
        p->image_index = 0;
        break;

    case ST_BALANCING:
        p->image_xscale = p->edgeDir;
        p->image_speed = 1;
        break;

    case ST_FALL:
        p->image_speed = 1;
        p->image_index = p->yspd > 0 ? 1 : 0;
        break;

    case ST_WALK:
        if (v->slimeTimer > 0) {
            p->image_speed = (fabsf(p->xspd) / p->maxHSpeed) * 2.5f;
            break;
        }
        p->image_speed = (fabsf(p->xspd) / p->maxHSpeed) * 1.8f;
        break;

    case ST_RUN:
        if (v->slimeTimer > 0) {
            p->image_speed = 2.6f;
            break;
        }
        p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f) * 1.2f;
        break;

    case ST_JUMP:
        if (v->slimeTimer > 0) {
            p->image_speed = 1.3f;
            if (p->image_index >= frames - 1) p->image_index = frames - 1;
            break;
        }
        p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f) * 1.2f;
        break;

    case ST_LOOKUP:
    case ST_LOOKDOWN:
        p->image_speed = 0.2f;
        if (p->image_index >= 1) p->image_index = 1;
        break;

    case ST_SPIN: p->image_speed = fabsf(p->xspd) / 5; break;

    case CHAOS_ATTACK:
        p->image_speed = 2;
        if (p->image_index >= frames - 1) {
            p->xspd = p->xspd * 0.7f;
            p->gspd = p->gspd * 0.7f;
            p->isAttacking = false;
        }
        break;

    case CHAOS_AIR_ATTACK:
        if (fabsf(p->xspd) > 0) p->image_xscale = sgnf(p->xspd);
        p->image_speed = 0.8f;
        break;

    case CHAOS_SHOCKED: p->image_speed = 0.5f; break;

    case CHAOS_LOST:
        p->image_speed = 1;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;

    case CHAOS_LOST2:
        p->image_speed = 1;
        if (p->image_index >= frames - 1) p->image_index = frames - 5;
        break;

    case CHAOS_WON:
        p->image_speed = 1;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;

    case CHAOS_STUCKBALLS:
    case CHAOS_STUCKDICKS:
        p->image_xscale = v->stuckDir;
        if (v->stuckTimer >= 30 / 2) p->image_index = (float)((osGetTime() / 10) % 2);  // current_time / 10 % 2
        else p->image_index = 2;
        break;

    case CHAOS_AIRTRANSFORM:
    case CHAOS_TRANSFORM:
        p->image_speed = 1;
        if (p->image_index >= frames - 1) v->slimeAnim = true;
        break;

    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: p->image_speed = 1; break;
    }
}

// Draw_0 (+ Draw_64)
static void draw(const Player *p, float cam_x, float cam_y)
{
    // effectTime afterimages (scr_effect_fade_quick every 3 frames): TODO(M8), shared.
    // TODO(M5b): obj_chaos_kapla at (x, y + irandom_range(-8, 8)), depth + 10, when
    //            effectTime % irandom_range(5, 6) == 0 while effectTime > 0.
    // Palette swap: omitted.
    float angle = p->isSpinning ? 0 : p->angle * 180.0f / (float)M_PI;
    float alpha = (p->hurttime > 0 || p->isHiding || p->shockedTimer > 0) ? 0.5f : 1.0f;
    sprite_draw(p->sprite, p->image_index, floorf(p->x) - cam_x, floorf(p->y) - cam_y, p->image_xscale, 1, angle,
                0xFFFFFFFF, alpha);

    // Draw_64
    if (p->isSlow) sprite_draw(SPR_FROZEN, 0, ceilf(p->x - cam_x), ceilf(p->y - cam_y) - 20, 1, 1, 0, 0xFFFFFFFF, 1);
}

// Alarm_4 (the shared code has already restored CHAOS_ACC / CHAOS_MAXSPEED and isSlow)
static void alarm4(Player *p)
{
    if (CHAR_VARS(p, ChaosVars)->slimeTimer > 0) {
        p->acc = CHAOS_ACC * 1.5f;
        p->maxHSpeed = CHAOS_MAXSPEED * 1.2f;
    }
}

// scr_move_basic: isHurt switch
static void on_hurt(Player *p)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    p->isAttacking = false;
    if (v->slimeTimer > 0) v->slimeTimer = 0;
    if (v->stuckTimer > 0) {
        p->canMove = true;
        v->stuckTimer = 0;
    }
    v->stuckDashTimer = 0;
}

// scr_move_basic: isBoosting block
static void on_boost(Player *p)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    v->stuckTimer = 0;
    v->stuckDashTimer = 0;
}

static bool jump_blocked(const Player *p) { return p->isAttacking; }

static void begin_stuck(Player *p, int dir)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    p->canSpin = false;
    v->stuckTimer = 30;
    v->dashedIntoPussy = 0;
    v->stuckDir = dir;

    v->leftPressed = false;
    v->rightPressed = false;
    v->upPressed = false;

    if (v->stuckDir > 0 && v->keyRight) v->rightPressed = true;
    if (v->stuckDir < 0 && v->keyLeft) v->leftPressed = true;
    if (v->keyUp) v->upPressed = true;

    player_sound(p, SND_CHAOS_LAND);
}

// scr_collision_check: wall hit (both sensors use stuckDir = -image_xscale)
static void on_wall(Player *p, WorldInst *wall, int dir)
{
    (void)dir;
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    if (v->stuckDashTimer <= 0) {
        if (p->attackTimer > 150 - 15) p->attackTimer = 150 - 15;
    }

    const ObjectInfo *oi = object_info(wall->object);
    bool canStuck = !(oi && (oi->flags & OBJF_NOSTICK));
    if (canStuck && v->stuckTimer <= 0 && p->state == CHAOS_AIR_ATTACK && v->dashedIntoPussy > 0)
        begin_stuck(p, (int)-p->image_xscale);
}

// scr_collision_check_bottom: start of the grounded check
static void on_grounded(Player *p)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    if (v->stuckTimer <= 0 && p->state == CHAOS_AIR_ATTACK && v->dashedIntoPussy > 0) begin_stuck(p, (int)p->image_xscale);
}

// scr_collision_objects_after: spring switch
static void on_spring(Player *p)
{
    ChaosVars *v = CHAR_VARS(p, ChaosVars);
    if (p->isAttacking) {
        p->attackTimer = p->isGrounded ? 60 : 120;
        p->isAttacking = false;
    }
    if (v->stuckTimer > 0) {
        p->canSpin = true;
        p->canMove = true;
        v->stuckTimer = 0;
    }
}

static bool flat_angle(const Player *p) { return p->state == EXE_WON_STATE || p->state == EXE_LOST_STATE; }

// scr_player_hurt: invisTimer (always 0 for Chaos) || won || lost
static bool hurt_blocked(Player *p, int damage)
{
    (void)damage;
    return p->won || p->lost;
}

static u8 net_flags(const Player *p)
{
    const ChaosVars *v = CHAR_VARS_C(p, ChaosVars);
    u8 f = p->isAttacking ? NETF_ATTACKING : 0;
    if (v->slimeTimer > 0 && p->state != CHAOS_TRANSFORM && p->state != CHAOS_AIRTRANSFORM) f |= NETF_SLIME;
    return f;
}

const CharDef CHAR_CHAOS_DEF = {
    .name = "chaos",
    .init = init,
    .special = special,
    .step_move = step_move,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .alarm4 = alarm4,
    .on_hurt = on_hurt,
    .on_boost = on_boost,
    .jump_blocked = jump_blocked,
    .on_wall = on_wall,
    .on_grounded = on_grounded,
    .on_spring = on_spring,
    .flat_angle = flat_angle,
    .hurt_blocked = hurt_blocked,
    .base_acc = CHAOS_ACC,
    .base_maxspeed = CHAOS_MAXSPEED,
    .net_flags = net_flags,
};

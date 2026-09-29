// obj_exe (EXE_ORIGINAL) + scr_exe_special.
#include <math.h>

#include "../gen/sprites.h"
#include "../sprite.h"
#include "chars.h"
#include "../achiev.h"
#include "../entities.h"
#include "../gen/sounds.h"
// after chars.h: gen/anims.h needs the libctru integer types
#include "../gen/anims.h"

#define EXE_INVIS_RECHARGE (-(60 * 20))
#define EXE_INVIS_DURATION (60 * 15)
#define EXE_MAXSPEED 12
#define EXE_ACC 0.056875f

// Step_2 macros
#define EXE_ATTACK (ST_BALANCING + 1)
#define EXE_AIR_ATTACK (ST_BALANCING + 2)
#define EXE_SHOCKED (ST_BALANCING + 3)
#define EXE_WON (ST_BALANCING + 4)
#define EXE_LOST (ST_BALANCING + 5)
#define EXE_ZIPLINE (ST_BALANCING + 6)

// Character variables. The first fields (won, lost, invisTimer, taunt1, taunt2) are the same
// prefix as ExellerVars so shared/net code can set won/lost on either EXE the same way.
typedef struct {
    float attackSpeed;
} ExeVars;

static void init(Player *p)
{
    p->hp = 10000;
    p->invisTimer = EXE_INVIS_RECHARGE;
    // No obj_unlockables yet: treat the taunts as unlocked.
    p->taunt1 = true;
    p->taunt2 = true;
    // TODO(M5b): obj_exe_indicator, obj_exe_sprindicator (HUD indicators)
}

static void special(Player *p, const Keys *k)
{
    ExeVars *v = CHAR_VARS(p, ExeVars);

    if (p->won || p->lost) {
        p->invisTimer = EXE_INVIS_RECHARGE;
        p->isAttacking = false;
        p->attackTimer = 0;
    }

    if (player_controls && p->isJumping && k->a_p) p->isJumping = false;

    if (player_controls && k->c_p) {
        if (p->invisTimer <= EXE_INVIS_RECHARGE) {
            if (!player_voice_playing())
                player_sound_local(rand() % 2 ? SND_EXE_INVISENTER : SND_EXE_INVISENTER2);
            player_sound(p, SND_EXE_APPEAR);
            achiev_round.rExeInvis = true;
            p->invisTimer = EXE_INVIS_DURATION;
        } else if (p->invisTimer > 0) {
            p->invisTimer = 0;
        }
    }

    if (p->invisTimer == 0) {
        player_sound(p, SND_EXE_APPEAR);
        achiev_round.rExeInvis = true;
        if (!player_voice_playing()) player_sound(p, rand() % 2 ? SND_EXE_APPEAR2 : SND_EXE_APPEAR3);
    }

    if (p->invisTimer > EXE_INVIS_RECHARGE) p->invisTimer--;

    if (player_controls && k->b_p && p->attackTimer <= 0 && p->invisTimer <= 0) {
        if (k->left) p->image_xscale = -1;
        else if (k->right) p->image_xscale = 1;

        p->image_index = 0;
        v->attackSpeed = p->xspd;
        p->attackTimer = p->isGrounded ? 60 : 120;
        p->isAttacking = true;
        p->isJumping = false;
        p->isSpinning = false;
        player_sound(p, SND_DASH);
    }

    if (p->invisTimer > 0) {
        p->isAttacking = false;
        return;
    }

    if (p->isAttacking) {
        if (p->yspd > 0) p->yspd = p->isGrounded ? 0 : 2.5f;

        if (p->isGrounded && fabsf(p->xspd) > 0 && (int)floorf(p->x) % 4 == 0) {
            net_quick_effect(p->x - 8 * sgnf(p->image_xscale), p->y + 20, SPR_DUST, false, 1, 0, 0, 0.5f);
        }
    }

    if (p->attackTimer > 0) p->attackTimer--;
}

// Step_0 before scr_collision_basic
static void step_move(Player *p, const Keys *k)
{
    if (p->shockedTimer <= 0) player_move_basic(p, k);
    else player_stun_tick(p);

    if (p->won || p->lost) {
        p->gspd = 0;
        p->xspd = 0;
        p->angle = 0;
    }
}

// Step_0 after scr_collision_basic: the bounce off jumping survivors (obj_player_puppet) is
// shared-code work (TODO(M5b)); hurttime-- is done by player_step.

// Step_2
static void end_step(Player *p)
{

    if (!p->emotion) p->state = ST_IDLE;

    if (p->won && p->isGrounded) { p->state = EXE_WON; return; }
    if (p->lost && p->isGrounded) { p->state = EXE_LOST; return; }

    if (p->isHurt) { p->state = ST_HURT; return; }

    if (p->shockedTimer > 0) {
        p->state = !p->isGrounded ? ST_HURT : EXE_SHOCKED;
        return;
    }

    if (p->isAttacking) {
        if (p->isGrounded) {
            if (p->state == EXE_AIR_ATTACK) { p->isAttacking = false; return; }
            p->state = EXE_ATTACK;
        } else {
            if (p->state == EXE_ATTACK) { p->isAttacking = false; return; }
            p->state = EXE_AIR_ATTACK;
        }
        return;
    }

    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        p->state = EXE_ZIPLINE;
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
    case ST_IDLE: {
        int frames = player_frames(p);
        p->image_index = frames - 25;
        while (p->image_index < 0) p->image_index += frames;  // GameMaker wraps image_index
        break;
    }
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3:
        p->image_index = 0;
        if (!p->emHeld[p->state - ST_EMOTION1]) {
            p->state = ST_IDLE;
            p->emotion = false;
        }
        break;
    }
}

// Draw_76
static void pre_draw(Player *p)
{
    // global.player_exesanims[? EXE_ORIGINAL][state + (invisTimer > 0 ? EXE_ZIPLINE+1 : 0)]
    int idx = p->state + (p->invisTimer > 0 ? EXE_ZIPLINE + 1 : 0);
    if (idx >= 0 && idx < ANIMS_EXE_COUNT[EXE_ORIGINAL] && ANIMS_EXE[EXE_ORIGINAL][idx] >= 0)
        p->sprite = ANIMS_EXE[EXE_ORIGINAL][idx];
    int frames = player_frames(p);

    switch (p->state) {
    case ST_IDLE: p->image_speed = 1; break;
    case ST_HURT:
    case EXE_ZIPLINE: p->image_speed = 0; p->image_index = 0; break;
    case ST_BALANCING: p->image_xscale = p->edgeDir; p->image_speed = 1; break;
    case ST_FALL:
        p->image_speed = 0.1f;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        if (p->xspd > 0) p->image_xscale = 1;
        else if (p->xspd < 0) p->image_xscale = -1;
        break;
    case ST_WALK: p->image_speed = fabsf(p->xspd) / p->maxHSpeed; break;
    case ST_RUN:
    case ST_JUMP: p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f); break;
    case ST_LOOKUP:
    case ST_LOOKDOWN:
        p->image_speed = 0.2f;
        if (p->image_index >= 1) p->image_index = 1;
        break;
    case ST_SPIN: p->image_speed = fabsf(p->xspd) / 5; break;
    case EXE_ATTACK:
        p->image_speed = 2;
        if (p->image_index >= frames - 1) {
            p->xspd = p->xspd * .7f;
            p->gspd = p->gspd * .7f;
            p->isAttacking = false;
        }
        break;
    case EXE_AIR_ATTACK:
        p->image_speed = 0.8f;
        if (p->image_index >= frames - 1) {
            p->xspd = p->xspd * .7f;
            p->gspd = p->gspd * .7f;
            p->isAttacking = false;
            p->isJumping = false;
        }
        break;
    case EXE_SHOCKED: p->image_speed = 0.5f; break;
    case EXE_LOST:
    case EXE_WON: p->image_speed = 0.8f; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: p->image_speed = 0.8f; break;
    }

    // Draw_0 "hakc" (runs right after Pre-Draw each frame; draw is const so it lives here)
    if (p->isDead) {
        p->xspd = 0;
        p->gspd = 0;
    }
}

// Draw_0. effectTime afterimages (scr_effect_fade_quick) and the palette swap: TODO(M8).
// Draw_64 (spr_frozen above the head while isSlow): TODO(M8).
static void draw(const Player *p, float cam_x, float cam_y)
{
    float angle = p->isSpinning ? 0 : p->angle * 180.0f / (float)M_PI;
    float alpha = (p->hurttime > 0 || p->isHiding || p->shockedTimer > 0) ? 0.5f : 1.0f;
    sprite_draw(p->sprite, p->image_index, floorf(p->x) - cam_x, floorf(p->y) - cam_y, p->image_xscale, 1, angle,
                0xFFFFFFFF, alpha);
}

// scr_move_basic "if(isHurt)": EXE_ORIGINAL
static void on_hurt(Player *p) { p->isAttacking = false; }

// scr_move_basic jump: CHARACTER_EXE cannot jump while attacking
static bool jump_blocked(const Player *p) { return p->isAttacking; }

// scr_collision_objects springs: CHARACTER_EXE
static void on_spring(Player *p)
{
    if (p->isAttacking) {
        p->attackTimer = p->isGrounded ? 60 : 120;
        p->isAttacking = false;
    }
}

// scr_collision_check_angle: flat while won/lost
static bool flat_angle(const Player *p) { return p->state == EXE_WON || p->state == EXE_LOST; }

// scr_player_hurt: invisTimer > 0 || won || lost -> return
static bool hurt_blocked(Player *p, int damage)
{
    (void)damage;
    return p->invisTimer > 0 || p->won || p->lost;
}

// obj_netclient Step_2 (exeCharacter 0)
static u8 net_flags(const Player *p)
{
    u8 flags = 0;
    bool attacking = p->isAttacking;
    if (!((p->state == EXE_ATTACK && p->image_index > 0) || (p->state == EXE_AIR_ATTACK && p->image_index >= 3)))
        attacking = false;
    if (attacking) flags |= NETF_ATTACKING;
    if (p->invisTimer > 0) flags |= NETF_INVIS;
    return flags;
}

const CharDef CHAR_EXE_DEF = {
    .name = "exe",
    .init = init,
    .special = special,
    .step_move = step_move,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .on_hurt = on_hurt,
    .jump_blocked = jump_blocked,
    .on_spring = on_spring,
    .flat_angle = flat_angle,
    .hurt_blocked = hurt_blocked,
    .net_flags = net_flags,
    .base_acc = EXE_ACC,
    .base_maxspeed = EXE_MAXSPEED,
};

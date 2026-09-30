// obj_exeller (EXE_EXELLER) + scr_exeller_special.
#include <math.h>
#include <stdlib.h>

#include "../gen/sprites.h"
#include "../net.h"
#include "../sprite.h"
#include <stddef.h>

#include "chars.h"
#include "../entities.h"
#include "../gen/sounds.h"
// after chars.h: gen/anims.h needs the libctru integer types
#include "../gen/anims.h"

#define EXELLER_CLONE_RECHARGE (60 * 30)
#define EXELLER_MAXSPEED 12
#define EXELLER_ACC 0.056875f

// Step_2 macros
#define EXELLER_ATTACK (ST_BALANCING + 1)
#define EXELLER_AIR_ATTACK (ST_BALANCING + 2)
#define EXELLER_SHOCKED (ST_BALANCING + 3)
#define EXELLER_WON (ST_BALANCING + 4)
#define EXELLER_LOST (ST_BALANCING + 5)
#define EXELLER_ZIPLINE (ST_BALANCING + 6)
#define EXELLER_LOST2 (ST_BALANCING + 7)
// scr_collision_check_angle compares against the obj_exe macros for every EXE
#define EXE_WON (ST_BALANCING + 4)
#define EXE_LOST (ST_BALANCING + 5)

// Character variables. The first fields (won, lost, invisTimer, taunt1, taunt2) are the same
// prefix as ExeVars in chars/exe.c. clones/cloneTimer/cloneCount are also written by the
// SERVER_EXELLER clone packets in net_state_game (spawn: clones[pindex] = id, cloneCount++;
// teleport: clones[pindex] = -1, cloneTimer = EXELLER_CLONE_RECHARGE, cloneCount--).
typedef struct {
    // same layout as ExellerShared (chars.h): entities.c updates these from the clone packets
    int clones[2];           // clone net ids, -1 = none
    int cloneTimer, cloneCount;
    float attackSpeed;
    int lostState;
} ExellerVars;

_Static_assert(offsetof(ExellerVars, clones) == offsetof(ExellerShared, clones) &&
               offsetof(ExellerVars, cloneTimer) == offsetof(ExellerShared, cloneTimer) &&
               offsetof(ExellerVars, cloneCount) == offsetof(ExellerShared, cloneCount),
               "ExellerVars must start with ExellerShared");

ExellerShared *exeller_shared(Player *p)
{
    if (p->character != CHARACTER_EXE || p->exe_character != EXE_EXELLER) return NULL;
    return CHAR_VARS(p, ExellerShared);
}

static void init(Player *p)
{
    ExellerVars *v = CHAR_VARS(p, ExellerVars);
    p->hp = 10000;
    p->invisTimer = 0;
    v->clones[0] = v->clones[1] = -1;
    v->cloneTimer = 0;
    v->cloneCount = 0;
    // No obj_unlockables yet: treat the taunts as unlocked.
    p->taunt1 = true;
    p->taunt2 = true;
    v->lostState = (rand() & 1) ? EXELLER_LOST2 : EXELLER_LOST;  // choose(EXELLER_LOST, EXELLER_LOST2)
    // TODO(M5b): obj_exetior_indicator, obj_exeller_indicator, obj_exeller_indicator_up,
    // obj_exeller_indicator_down, obj_exe_sprindicator (HUD indicators)
}

static void send_teleport(int nid)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_EXELLER_TELEPORT_CLONE);
    pkt_u16(&pk, (u16)nid);
    net_send(&pk, true);
}

static void special(Player *p, const Keys *k)
{
    ExellerVars *v = CHAR_VARS(p, ExellerVars);

    if (p->won || p->lost) {
        p->isAttacking = false;
        p->attackTimer = 0;
    }

    if (player_controls && p->isJumping && k->a_p) p->isJumping = false;

    if (v->cloneTimer > 0) v->cloneTimer--;

    if (player_controls && k->c_p) {
        // TODO(M5b): the GML returns early if no obj_exeller_clone instance exists and only
        // sends the packet when a clone with nid == clones[i] exists; clones[i] != -1 is used
        // as that check here until the clone entity exists.
        if (k->up && v->clones[0] != -1) {
            send_teleport(v->clones[0]);
            return;
        }

        if (k->down && v->clones[1] != -1) {
            send_teleport(v->clones[1]);
            return;
        }

        if (!k->down && !k->up && v->cloneCount < 2 && v->cloneTimer <= 0) {
            NetPacket pk;
            pkt_begin(&pk, CLIENT_EXELLER_SPAWN_CLONE);
            pkt_u16(&pk, (u16)(int)p->x);
            pkt_u16(&pk, (u16)(int)p->y);
            pkt_u8(&pk, (u8)(s8)p->image_xscale);
            net_send(&pk, true);

            if (!player_voice_playing() && rand() % 4 == 0) player_sound(p, SND_EXELLER_CLONELINE);

            v->cloneTimer = 3 * 60;
        }
    }

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
    const ExellerVars *v = CHAR_VARS_C(p, ExellerVars);

    if (!p->emotion) p->state = ST_IDLE;

    if (p->won && p->isGrounded) { p->state = EXELLER_WON; return; }
    if (p->lost && p->isGrounded) { p->state = v->lostState; return; }

    if (p->isHurt) { p->state = ST_HURT; return; }

    if (p->shockedTimer > 0) {
        p->state = !p->isGrounded ? ST_HURT : EXELLER_SHOCKED;
        return;
    }

    if (p->isAttacking) {
        if (p->isGrounded) {
            if (p->state == EXELLER_AIR_ATTACK) { p->isAttacking = false; return; }
            p->state = EXELLER_ATTACK;
        } else {
            if (p->state == EXELLER_ATTACK) { p->isAttacking = false; return; }
            p->state = EXELLER_AIR_ATTACK;
        }
        return;
    }

    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        p->state = EXELLER_ZIPLINE;
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
        p->image_index = frames - 48;
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
    // global.player_exesanims[? EXE_EXELLER][state + (invisTimer > 0 ? EXE_ZIPLINE+1 : 0)]
    int idx = p->state + (p->invisTimer > 0 ? EXELLER_ZIPLINE + 1 : 0);
    if (idx >= 0 && idx < ANIMS_EXE_COUNT[EXE_EXELLER] && ANIMS_EXE[EXE_EXELLER][idx] >= 0)
        p->sprite = ANIMS_EXE[EXE_EXELLER][idx];
    int frames = player_frames(p);

    switch (p->state) {
    case ST_IDLE: p->image_speed = 1; break;
    case ST_HURT:
    case EXELLER_ZIPLINE: p->image_speed = 0; p->image_index = 0; break;
    case ST_BALANCING: p->image_xscale = p->edgeDir; p->image_speed = 1; break;
    case ST_FALL:
        p->image_speed = 1;
        p->image_index = p->yspd > 0 ? 1 : 0;
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
    case EXELLER_ATTACK:
        p->image_speed = 2;
        if (p->image_index >= frames - 1) {
            p->xspd = p->xspd * .7f;
            p->gspd = p->gspd * .7f;
            p->isAttacking = false;
        }
        break;
    case EXELLER_AIR_ATTACK:
        p->image_speed = 0.8f;
        if (p->image_index >= frames - 1) {
            p->xspd = p->xspd * .7f;
            p->gspd = p->gspd * .7f;
            p->isAttacking = false;
            p->isJumping = false;
        }
        break;
    case EXELLER_SHOCKED: p->image_speed = 0.5f; break;
    case EXELLER_LOST:
    case EXELLER_LOST2:
    case EXELLER_WON: p->image_speed = 0.8f; break;
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

// scr_move_basic "if(isHurt)": EXE_EXELLER
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

// scr_collision_check_angle: flat in EXE_WON / EXE_LOST (not EXELLER_LOST2, as in the GML)
static bool flat_angle(const Player *p) { return p->state == EXE_WON || p->state == EXE_LOST; }

// scr_player_hurt: invisTimer > 0 || won || lost -> return
static bool hurt_blocked(Player *p, int damage)
{
    (void)damage;
    return p->invisTimer > 0 || p->won || p->lost;
}

// obj_netclient Step_2 (no per-EXE rule for exeCharacter 3)
static u8 net_flags(const Player *p) { return p->isAttacking ? NETF_ATTACKING : 0; }

// obj_playerui
static void draw_gui(const Player *p)
{
    const ExellerVars *v = CHAR_VARS_C(p, ExellerVars);
    float prog = 1.0f - fminf(p->attackTimer, 180) / 180.0f;
    gui_ability(SPR_GUI_EXEATTACK, 0, prog, 10, 240, prog >= 1);
    gui_exe_freejump(p, true);
    prog = 1.0f - v->cloneTimer / (float)EXELLER_CLONE_RECHARGE;
    int spr = SPR_GUI_EXELLERCLONE;
    if ((hidKeysHeld() & (KEY_UP | KEY_DOWN)) && v->cloneCount > 0) {
        spr = SPR_GUI_EXELLERCLONE2;
        prog = 1;
    }
    if (spr == SPR_GUI_EXELLERCLONE && v->cloneCount >= 2) prog = 0;
    gui_ability(spr, 0, prog, 4, 250, prog >= 1);
}

const CharDef CHAR_EXELLER_DEF = {
    .draw_gui = draw_gui,
    .name = "exeller",
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
    .base_acc = EXELLER_ACC,
    .base_maxspeed = EXELLER_MAXSPEED,
};

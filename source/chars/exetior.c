// obj_exetior + scr_exetior_special, and the EXE_EXETIOR branches of scr_move_basic and
// scr_collision_objects (springs).
#include <math.h>

#include "../gen/sprites.h"
#include "../net.h"
#include "../sprite.h"
#include "chars.h"
#include "../game.h"
#include "../entities.h"
#include "../gen/sounds.h"

#define EXETIOR_MAXSPEED 11
#define EXETIOR_ACC 0.051875f
#define EXETIOR_BRING_RECHARGE (42 * 60)

// Step_2 states (BALANCING + n)
#define EXETIOR_ATTACK (ST_BALANCING + 1)
#define EXETIOR_AIR_ATTACK (ST_BALANCING + 2)
#define EXETIOR_SHOCKED (ST_BALANCING + 3)
#define EXETIOR_WON (ST_BALANCING + 4)
#define EXETIOR_LOST (ST_BALANCING + 5)
#define EXETIOR_ZIPLINE (ST_BALANCING + 6)
#define EXETIOR_STOMP (ST_BALANCING + 7)
#define EXETIOR_STOMP_LAND (ST_BALANCING + 8)

// obj_exe's EXE_WON / EXE_LOST (scr_collision_check_angle compares the state with these)
#define EXE_WON_STATE (ST_BALANCING + 4)
#define EXE_LOST_STATE (ST_BALANCING + 5)

typedef struct {
    // Set by SERVER_GAME_EXE_WINS / SERVER_GAME_SURVIVOR_WIN in the GML (global.player.won/lost).
    // TODO(shared): nothing sets these yet; they should become Player fields.
    bool prevGrounded;
    float attackSpeed;
    int bringTimer;
    bool isStomping, justLanded, canSpawnRings;
} ExetiorVars;

static void init(Player *p)
{
    ExetiorVars *v = CHAR_VARS(p, ExetiorVars);
    p->hp = 10000;
    p->canSpinDash = false;
    v->prevGrounded = true;
    v->canSpawnRings = true;
    // obj_exetior_indicator / obj_exe_sprindicator: HUD, not ported here.
    // Create_0 also sets global.playerControls = false (left to the game code).
}

// scr_exetior_special
static void special(Player *p, const Keys *k)
{
    ExetiorVars *v = CHAR_VARS(p, ExetiorVars);

    if (player_controls && p->isJumping && k->a_p) p->isJumping = false;

    if (player_controls && k->b_p && !p->isZipline && p->attackTimer <= 0) {
        if (k->left) p->image_xscale = -1;
        else if (k->right) p->image_xscale = 1;

        if (!p->isGrounded) {
            if (k->down) {
                p->image_index = 0;
                p->isAttacking = true;
                p->isJumping = false;
                p->isSpinning = false;
                v->isStomping = true;
                v->justLanded = false;
                player_sound(p, SND_EXETIOR_STOMP);
            } else {
                p->image_index = 0;
                v->attackSpeed = p->xspd;
                p->attackTimer = 180;
                p->isAttacking = true;
                p->isJumping = false;
                p->isSpinning = false;
                player_sound(p, SND_DASH);
            }
        } else {
            p->image_index = 0;
            v->attackSpeed = p->xspd;
            p->attackTimer = 60;
            p->isAttacking = true;
            p->isJumping = false;
            p->isSpinning = false;
            player_sound(p, SND_DASH);
        }
    }

    v->canSpawnRings = !ents_blackring_near(p->x, p->y, 100);
    if (player_controls && k->c_p && v->bringTimer <= 0 && v->canSpawnRings) {
        NetPacket pk;
        pkt_begin(&pk, CLIENT_ERECTOR_BRING_SPAWN);
        pkt_u16(&pk, (u16)(int)(p->x - 15));
        pkt_u16(&pk, (u16)(int)(p->y - 15));
        net_send(&pk, true);

        if (!player_voice_playing()) {
            static const int lines[4] = { SND_EXETIOR_RING1, SND_EXETIOR_RING2, SND_EXETIOR_RING3, SND_EXETIOR_RING4 };
            player_sound(p, lines[rand() % 4]);
        }
        player_sound(p, SND_BLACKRING);
        game_exe_indicator_show(7 * 60);  // obj_exetior_indicator.showTimer

        v->bringTimer = EXETIOR_BRING_RECHARGE;
    }

    if (v->bringTimer > 0) v->bringTimer--;

    if (p->isAttacking) {
        if (p->isGrounded && fabsf(p->xspd) > 0 && (int)floorf(p->x) % 4 == 0) {
            net_quick_effect(p->x - 8 * sgnf(p->image_xscale), p->y + 20, SPR_DUST, false, 1, 0, 0, 0.5f);
        }

        if (p->yspd > 0) p->yspd = p->isGrounded ? 0 : 2.5f;
    }

    if (p->attackTimer > 0) p->attackTimer--;

    if (v->isStomping) {
        p->attackTimer = 240;

        if (p->isZipline || p->isHurt || p->shockedTimer > 0 || p->won || p->lost) {
            p->isAttacking = false;
            v->isStomping = false;
            p->canMove = true;
            return;
        }

        if (!p->isGrounded) {
            if (p->effectTime <= 0) p->effectTime = 6;
            // scr_camera_shake(10, 0.2, 0.01): omitted
            p->isAttacking = true;
            p->xspd = 0;
            p->yspd = 12;
            p->canMove = false;
        } else {
            if (!v->justLanded) {
                player_sound(p, SND_EXETIOR_STOMPLAND);
                NetPacket pk;
                pkt_begin(&pk, CLIENT_ERECTOR_BALLS);
                pkt_f32(&pk, p->x);
                pkt_f32(&pk, p->y);
                net_send(&pk, true);
                // TODO(M5b): the stomp balls are spawned when CLIENT_ERECTOR_BALLS is handled
                //            (net_state_game): obj_exetior_stompballs at (x - 25, y + 19) dir -1
                //            and (x + 25, y + 19) dir 1, depth -1.
                v->justLanded = true;
            }
            // scr_camera_shake(25, 1, 0.2): omitted
            p->isAttacking = false;
            p->xspd = 0;
            p->yspd = 0;
            p->canMove = false;
        }
    }
}

// Step_0 up to scr_collision_basic.
static void step_move(Player *p, const Keys *k)
{
    ExetiorVars *v = CHAR_VARS(p, ExetiorVars);

    // Draw_0 "hakc": if(isDead) { xspd = 0; gspd = 0; } runs at the end of the previous frame.
    if (p->isDead) {
        p->xspd = 0;
        p->gspd = 0;
    }

    if (p->canMove || p->shockedTimer > 0) {
        if (p->shockedTimer <= 0) {
            player_move_basic(p, k);
        } else {
            p->canMove = true;
            v->isStomping = false;
            player_stun_tick(p);
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
    ExetiorVars *v = CHAR_VARS(p, ExetiorVars);

    if (!p->emotion) p->state = ST_IDLE;

    if (p->won && p->isGrounded) { p->state = EXETIOR_WON; return; }
    if (p->lost && p->isGrounded) { p->state = EXETIOR_LOST; return; }

    if (p->isHurt) { p->state = ST_HURT; return; }

    if (p->shockedTimer > 0) {
        p->state = !p->isGrounded ? ST_HURT : EXETIOR_SHOCKED;
        return;
    }

    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        p->state = EXETIOR_ZIPLINE;
        return;
    }

    if (v->isStomping) {
        p->state = p->isGrounded ? EXETIOR_STOMP_LAND : EXETIOR_STOMP;
        return;
    }

    if (p->isAttacking) {
        if (p->isGrounded) {
            if (!v->prevGrounded) {
                p->isAttacking = false;
                return;
            }
            p->state = EXETIOR_ATTACK;
        } else {
            if (v->prevGrounded) {
                p->isAttacking = false;
                return;
            }
            p->state = EXETIOR_AIR_ATTACK;
        }
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
    case ST_IDLE: p->image_index = player_frames(p) - 14; break;
    case ST_EMOTION1:
        if (p->emHeld[0]) {
            p->image_index = 17;
        } else {
            p->image_index = 0;
            p->state = ST_IDLE;
            p->emotion = false;
        }
        break;
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
    ExetiorVars *v = CHAR_VARS(p, ExetiorVars);
    int spr = player_anim_sprite(p);
    if (spr >= 0) p->sprite = spr;
    int frames = player_frames(p);

    switch (p->state) {
    case ST_IDLE: p->image_speed = 1; break;

    case ST_HURT:
    case EXETIOR_ZIPLINE:
        p->image_speed = 0;
        p->image_index = 0;
        break;

    case ST_BALANCING:
        p->image_xscale = p->edgeDir;
        p->image_speed = 1;
        break;

    case ST_FALL:
        p->image_speed = 0.1f;
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

    case EXETIOR_ATTACK:
        p->image_speed = 2;
        if (p->image_index >= frames - 1) {
            p->xspd = p->xspd * 0.7f;
            p->gspd = p->gspd * 0.7f;
            p->isAttacking = false;
        }
        break;

    case EXETIOR_AIR_ATTACK:
        p->image_speed = 0.8f;
        if (p->image_index >= frames - 1) {
            p->xspd = p->xspd * 0.7f;
            p->gspd = p->gspd * 0.7f;
            p->isAttacking = false;
            p->isJumping = false;
        }
        break;

    case EXETIOR_SHOCKED: p->image_speed = 0.5f; break;

    case EXETIOR_LOST:
    case EXETIOR_WON: p->image_speed = 0.8f; break;

    case EXETIOR_STOMP: p->image_speed = 1; break;

    case EXETIOR_STOMP_LAND:
        p->image_speed = 1;
        if (p->image_index >= frames - 1) {
            p->canMove = true;
            v->isStomping = false;
        }
        break;

    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: p->image_speed = 0.8f; break;
    }
}

// Draw_0 (+ Draw_64)
static void draw(const Player *p, float cam_x, float cam_y)
{
    // effectTime afterimages (scr_effect_fade_quick every 3 frames): TODO(M8), shared.
    // Palette swap: omitted.
    float angle = p->isSpinning ? 0 : p->angle * 180.0f / (float)M_PI;
    float alpha = (p->hurttime > 0 || p->isHiding || p->shockedTimer > 0) ? 0.5f : 1.0f;
    sprite_draw(p->sprite, p->image_index, floorf(p->x) - cam_x, floorf(p->y) - cam_y, p->image_xscale, 1, angle,
                0xFFFFFFFF, alpha);

    // Draw_64
    if (p->isSlow) sprite_draw(SPR_FROZEN, 0, ceilf(p->x - cam_x), ceilf(p->y - cam_y) - 20, 1, 1, 0, 0xFFFFFFFF, 1);
}

// scr_move_basic: isHurt switch
static void on_hurt(Player *p)
{
    p->isAttacking = false;
    CHAR_VARS(p, ExetiorVars)->isStomping = false;
}

static bool jump_blocked(const Player *p) { return p->isAttacking; }

// scr_collision_objects_after: spring switch
static void on_spring(Player *p)
{
    if (p->isAttacking) {
        p->attackTimer = p->isGrounded ? 60 : 120;
        p->isAttacking = false;
    }
}

static bool flat_angle(const Player *p) { return p->state == EXE_WON_STATE || p->state == EXE_LOST_STATE; }

// scr_player_hurt: invisTimer (always 0 for Exetior) || won || lost
static bool hurt_blocked(Player *p, int damage)
{
    (void)damage;
    return p->won || p->lost;
}

static u8 net_flags(const Player *p) { return p->isAttacking ? NETF_ATTACKING : 0; }

const CharDef CHAR_EXETIOR_DEF = {
    .name = "exetior",
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
    .base_acc = EXETIOR_ACC,
    .base_maxspeed = EXETIOR_MAXSPEED,
    .net_flags = net_flags,
};

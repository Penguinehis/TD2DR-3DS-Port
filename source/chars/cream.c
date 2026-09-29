// obj_cream + scr_cream_special.
#include <math.h>

#include "../gen/objects.h"
#include "../gen/sprites.h"
#include "../net.h"
#include "../sprite.h"
#include "chars.h"
#include "../gen/sounds.h"

#define CREAM_FLY_RECHARGE (60 * 15)
#define CREAM_DASH_RECHARGE (60 * 30)
#define CREAM_RINGS_RECHARGE (60 * 40)
#define ECREAM_RINGS_RECHARGE (60 * 30)
#define CREAM_MAXSPEED 11
#define CREAM_ACC 0.046875f

// Step_2
#define CREAM_FLY (ST_BALANCING + 1)
#define CREAM_SPAWN (ST_BALANCING + 2)
#define CREAM_ZIPLINE (ST_BALANCING + 3)

typedef struct {
    int dashing, flyTimeout, dashTimer, ringsTimer, ringsSpawn;
    bool isColliding;
} CreamVars;

// Create_0 (flyTimer = 0 instead of the shared -420; isFlying ends up false)
static void init(Player *p)
{
    p->flyTimer = 0;
    // TODO(M5b): reviveObj = obj_revival_puppet (Step_0 keeps it at x, y - 40, master_id = nid)
}

// place_meeting(x, y, obj_angleanuller) with the current sprite bbox (obj_angleanuller_eggsafe
// is not a child of obj_angleanuller).
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

// collision_circle(cx, cy, rad, obj_floor_parent, true, true): the instance shapes are sampled
// on a 2 px grid inside the circle (early out on the first hit).
static bool circle_floor(float cx, float cy, float rad)
{
    WorldInst *list[64];
    int n = world_query_rect(cx - rad, cy - rad, cx + rad, cy + rad, WC_FLOOR, list, 64);
    float r2 = rad * rad;
    for (int i = 0; i < n; i++) {
        const WorldInst *w = list[i];
        // closest point of the instance AABB to the centre
        float nx = clampf(cx, w->l, w->r - 1), ny = clampf(cy, w->t, w->b - 1);
        if ((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) > r2) continue;
        if (inst_contains_point(w, nx, ny)) return true;
        float x0 = fmaxf(floorf(cx - rad), floorf(w->l)), x1 = fminf(cx + rad, w->r - 1);
        float y0 = fmaxf(floorf(cy - rad), floorf(w->t)), y1 = fminf(cy + rad, w->b - 1);
        for (float py = y0; py <= y1; py += 2)
            for (float px = x0; px <= x1; px += 2)
                if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= r2 && inst_contains_point(w, px, py))
                    return true;
    }
    return false;
}

static float rings_x(const Player *p) { return p->x - 12 + (p->image_xscale < 0 ? 7 : 0); }

// scr_cream_special
static void special(Player *p, const Keys *k)
{
    CreamVars *v = CHAR_VARS(p, CreamVars);

    if (p->redRingTimer > 0) {
        if (p->flyTimer <= 2) p->flyTimer = 2;
        if (v->dashTimer <= 2) v->dashTimer = 2;
        if (v->ringsTimer <= 2) v->ringsTimer = 2;
        v->ringsSpawn = 0;
    }

    if (p->isHiding) {
        if (p->flyTimer <= 60 * 2) p->flyTimer = 60 * 2;
        if (v->dashTimer <= 60 * 2) v->dashTimer = 60 * 2;
        if (v->ringsTimer <= 60 * 2) v->ringsTimer = 60 * 2;
        v->ringsSpawn = 0;
    }

    if (player_controls && p->isJumping && !p->justJumped && k->a_p && p->flyTimer <= 0) {
        v->flyTimeout = 0;
        p->isFlying = true;
        p->isJumping = false;
        p->isSpinning = false;
        p->flyTimer = CREAM_FLY_RECHARGE;
    }

    if (player_controls && k->b_p && v->dashTimer <= 0 && fabsf(p->xspd) > 0 && v->ringsSpawn <= 0) {
        v->dashing = .5 * 60;
        p->effectTime = .5 * 60;
        v->dashTimer = CREAM_DASH_RECHARGE;
        p->maxHSpeed = 13;
        player_sound(p, SND_CREAMDASH);
    }

    v->isColliding = place_angleanuller(p) || circle_floor(rings_x(p), p->y - 16, 32);

    // TODO(M5b): if (revivalTimes >= 2 && instance_exists(obj_redring) && distance_to_object(obj_redring) < 130)
    //            isColliding = true;

    if (player_controls && k->c_p && v->ringsTimer <= 0 && p->isGrounded) {
        if (!v->isColliding) {
            v->ringsTimer = p->revivalTimes >= 2 ? ECREAM_RINGS_RECHARGE : CREAM_RINGS_RECHARGE;
            v->ringsSpawn = 60;
        }
    }

    if (v->ringsSpawn > 1) {
        if (!p->isGrounded || !player_controls) {
            v->ringsTimer = p->revivalTimes >= 2 ? ECREAM_RINGS_RECHARGE : CREAM_RINGS_RECHARGE;
            v->ringsSpawn = 0;
        }
        p->gspd = 0;
        p->xspd = 0;
        p->yspd = 0;
        v->ringsSpawn--;
    } else if (v->ringsSpawn == 1) {
        if (!circle_floor(rings_x(p), p->y - 16, 32)) {
            player_sound(p, SND_CREAMRING);
            NetPacket pk;
            pkt_begin(&pk, CLIENT_CREAM_SPAWN_RINGS);
            pkt_u16(&pk, (u16)(int)rings_x(p));
            pkt_u16(&pk, (u16)(int)(p->y - 16));
            pkt_u8(&pk, p->revivalTimes >= 2);
            net_send(&pk, true);
        }
        v->ringsSpawn = 0;
    }

    if (p->isFlying) {
        p->yspd = 1;
        v->flyTimeout++;
        if (v->flyTimeout > 60 * 2) p->isFlying = false;
        if (p->isGrounded) p->isFlying = false;
    }

    if (v->dashing > 0) {
        if (p->isJumping) p->isJumping = false;
        p->acc = .8f;
        v->dashing--;
    } else if (p->isSlow) {
        // Note: 30 % here, while scr_player_slow uses 40 %; the GML overwrites it every frame.
        p->maxHSpeed = (CREAM_MAXSPEED * (100 - 30)) / 100.0f;
        p->acc = (CREAM_ACC * (100 - 30)) / 100;
    } else {
        p->maxHSpeed = CREAM_MAXSPEED;
        p->acc = CREAM_ACC;
    }

    if (!p->isFlying && p->flyTimer > 0) p->flyTimer--;
    if (v->ringsTimer > 0) v->ringsTimer--;
    if (v->dashTimer > 0) v->dashTimer--;
}

// Step_0 before scr_collision_basic: the stun branch also clears dashing and ringsSpawn.
static void step_move(Player *p, const Keys *k)
{
    CreamVars *v = CHAR_VARS(p, CreamVars);
    if (p->shockedTimer <= 0) {
        player_move_basic(p, k);
    } else {
        v->dashing = 0;
        player_stun_tick(p);
        v->ringsSpawn = 0;
    }
}

// Step_0 after scr_collision_basic only has the obj_player_puppet contact damage (shared game
// code, TODO(M5b)).

// Step_2
static void end_step(Player *p)
{
    const CreamVars *v = CHAR_VARS_C(p, CreamVars);
    if (!p->emotion) p->state = ST_IDLE;
    if (p->isDead) { p->state = ST_DEAD; return; }
    if (p->shockedTimer > 0) { p->state = ST_DEAD; return; }
    if (v->ringsSpawn > 0) { p->state = CREAM_SPAWN; return; }
    if (p->isHurt) { p->state = ST_HURT; return; }
    if (p->isFlying) { p->state = CREAM_FLY; return; }
    if (p->isZipline) {
        if (p->isGrounded) p->isZipline = false;
        else { p->state = CREAM_ZIPLINE; return; }
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
    case ST_IDLE: p->image_index = player_frames(p) - 24; break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: {
        int i = p->state - ST_EMOTION1;
        if (p->emHeld[i]) {
            p->image_index = i == 0 ? 1 : 0;
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
    case ST_WALK: p->image_speed = 0.2f + fabsf(p->xspd) / p->maxHSpeed; break;
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
    case CREAM_FLY: p->image_speed = 0.3f; break;
    case CREAM_SPAWN:
        p->image_speed = 0.5f;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;
    case ST_EMOTION1:
    case ST_EMOTION2:
    case ST_EMOTION3: p->image_speed = 1; break;
    }
}

// Draw_0 (effectTime afterimages / palette swap: TODO(M8) in player_draw)
static void draw(const Player *p, float cam_x, float cam_y)
{
    player_draw_body(p, cam_x, cam_y, p->isSpinning ? 0 : p->angle * 180.0f / (float)M_PI);
}

// scr_move_basic "if(isDead)" block
static void on_dead(Player *p)
{
    CreamVars *v = CHAR_VARS(p, CreamVars);
    v->dashing = 0;
    v->ringsSpawn = 0;
}

// scr_move_basic "if(isHurt)" switch
static void on_hurt(Player *p)
{
    CreamVars *v = CHAR_VARS(p, CreamVars);
    v->dashing = 0;
    v->ringsSpawn = 0;
}

// scr_move_basic: no ground jump while spawning rings
static bool jump_blocked(const Player *p) { return CHAR_VARS_C(p, CreamVars)->ringsSpawn > 0; }

// scr_collision_objects spring switch
static void on_spring(Player *p)
{
    if (p->isFlying) {
        p->flyTimer = CREAM_FLY_RECHARGE;
        p->isFlying = false;
    }
}

// net_state_game SERVER_GAME_DEATHTIMER_END: ability timers restart when demonized
static void on_demonize(Player *p)
{
    CreamVars *v = CHAR_VARS(p, CreamVars);
    p->flyTimer = 0;
    v->dashTimer = 0;
    v->ringsTimer = 0;
}

const CharDef CHAR_CREAM_DEF = {
    .on_demonize = on_demonize,
    .name = "cream",
    .init = init,
    .special = special,
    .step_move = step_move,
    .end_step = end_step,
    .animation_end = animation_end,
    .pre_draw = pre_draw,
    .draw = draw,
    .on_dead = on_dead,
    .on_hurt = on_hurt,
    .jump_blocked = jump_blocked,
    .on_spring = on_spring,
    .base_acc = CREAM_ACC,
    .base_maxspeed = CREAM_MAXSPEED,
};

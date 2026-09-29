#pragma once
// Per-character behaviour. Each playable object in the GML (obj_tails, obj_knux, obj_egg,
// obj_amy, obj_cream, obj_sally, obj_exe, obj_chaos, obj_exetior, obj_exeller) is one CharDef
// in source/chars/<name>.c. The shared parts (scr_move_basic, scr_collision_basic, the
// Step/End Step/animation pipeline) live in player.c and call these hooks.
//
// Mapping from the GML object events:
//   Create_0            init          (after player_init has set the shared defaults)
//   specialFunc         special       (scr_<name>_special, called from scr_move_basic)
//   Step_0 (extra)      step          (after scr_collision_basic; optional)
//   Step_2 (End Step)   end_step      (state machine: picks p->state)
//   Other_7             animation_end (optional)
//   Draw_76 (Pre-Draw)  pre_draw      (sprite_index / image_speed for the state)
//   Draw_0              draw          (optional; NULL draws the body like draw_self)
//   Alarm_4             alarm4        (optional; the slow-down timer end)
// Character variables that only one object uses live in p->vars (see CHAR_VARS below);
// variables that scr_move_basic or the collision scripts touch are fields of Player.

#include "../player.h"

typedef struct CharDef {
    const char *name;
    void (*init)(Player *p);
    void (*special)(Player *p, const Keys *k);
    // Step_0 before scr_collision_basic. NULL = the common form:
    //   if(shockedTimer <= 0) scr_move_basic(); else <stun countdown>
    // (player_move_basic / player_stun_tick below). Objects whose Step_0 differs (Tails gates on
    // canMove, Chaos on canMove || shockedTimer > 0) implement it.
    void (*step_move)(Player *p, const Keys *k);
    void (*step)(Player *p, const Keys *k);
    void (*end_step)(Player *p);
    void (*animation_end)(Player *p);
    void (*pre_draw)(Player *p);
    void (*draw)(const Player *p, float cam_x, float cam_y);
    void (*alarm4)(Player *p);
    // scr_move_basic character branches
    void (*on_dead)(Player *p);         // "if(isDead)" block
    void (*on_hurt)(Player *p);         // "if(isHurt)" switch
    void (*on_boost)(Player *p);        // "if(isBoosting)" block
    bool (*jump_blocked)(const Player *p);  // early return before a ground jump
    // scr_collision_basic / scr_collision_objects character branches
    void (*on_wall)(Player *p, WorldInst *wall, int dir);  // hit a wall while moving (dir -1 left, 1 right)
    void (*on_grounded)(Player *p);     // start of the grounded check in scr_collision_check_bottom
    void (*on_land_angle)(Player *p);   // after the floor angle is set (grounded)
    void (*on_abyss)(Player *p);        // teleported out of a pit
    void (*on_spring)(Player *p);       // the switch before a spring launches the player
    bool (*flat_angle)(const Player *p);   // keep angle 0 on the ground (EXE won/lost)
    bool (*hide_blocked)(const Player *p); // cannot hide (Sally sliding)
    // scr_player_hurt: return true to swallow the hit (Sally shield, EXE invisible/won/lost)
    bool (*hurt_blocked)(Player *p, int damage);
    // SERVER_GAME_DEATHTIMER_END demonization: reset the character's ability timers
    void (*on_demonize)(Player *p);
    // scr_player_slow / Alarm_4: base acc and max speed restored after a slow-down
    float base_acc, base_maxspeed;
    // CLIENT_PLAYER_DATA bit flags beyond PLAYER_EFFECT/HURT/REDRING (e.g. attacking)
    u8 (*net_flags)(const Player *p);
} CharDef;

// Typed view of p->vars for a character file: CHAR_VARS(p, KnuxVars)->glideTimer
#define CHAR_VARS(p, T) ((T *)(void *)(p)->vars)
#define CHAR_VARS_C(p, T) ((const T *)(const void *)(p)->vars)

extern const CharDef CHAR_TAILS_DEF, CHAR_KNUX_DEF, CHAR_EGGMAN_DEF, CHAR_AMY_DEF, CHAR_CREAM_DEF,
    CHAR_SALLY_DEF, CHAR_EXE_DEF, CHAR_CHAOS_DEF, CHAR_EXETIOR_DEF, CHAR_EXELLER_DEF;

// PLAYER_* flags of CLIENT_PLAYER_DATA (net_packets.gml)
#define NETF_EFFECT (1 << 1)
#define NETF_HURT (1 << 2)
#define NETF_REDRING (1 << 3)
#define NETF_ATTACKING (1 << 4)
#define NETF_SALLYSHIELD (1 << 5)
#define NETF_INVIS (1 << 6)
#define NETF_SLIME (1 << 7)

// Exeller's clone bookkeeping, written by the clone packets (entities.c). NULL unless the
// player is Exeller.
typedef struct {
    int clones[2];
    int cloneTimer, cloneCount;
} ExellerShared;
ExellerShared *exeller_shared(Player *p);

// Hurt-blocked check for the local player (Sally's shield), used by game.c.
bool player_hurt_blocked(Player *p);
// Demonization (SERVER_GAME_DEATHTIMER_END): hp 100, revivalTimes 2, hurttime 120 and the
// character's on_demonize.
void player_demonize(Player *p);

// Helpers shared with the character files (player.c)
void player_move_basic(Player *p, const Keys *k);   // scr_move_basic
// The common stun branch of Step_0: isBoosting/isLookingDown/isLookingUp/isAttacking/isJumping
// = false, decelerate gspd/xspd by acc, shockedTimer--, hurttime = 120.
void player_stun_tick(Player *p);
float sgnf(float v);
float clampf(float v, float lo, float hi);
// Sprite for the current state from the exported tables (gen/anims.h), demon table when
// revivalTimes >= 2 for survivors. -1 if the state has no entry.
int player_anim_sprite(const Player *p);
// draw_sprite_ext(sprite_index, image_index, floor(x), floor(y), image_xscale, 1, angle_deg, ...)
// with the standard alpha rule (0.5 while hurt or hiding).
void player_draw_body(const Player *p, float cam_x, float cam_y, float angle_deg);
// Frame count of p->sprite (1 if unknown).
int player_frames(const Player *p);
// place_meeting(x, y, obj_floor_parent) at an offset from the player origin using the
// player's current sprite bbox.
bool player_place_floor(const Player *p, float dx, float dy);

// chars/basic.c: generic End Step / Pre-Draw (placeholder for unported characters)
void char_basic_end_step(Player *p);
void char_basic_pre_draw(Player *p);

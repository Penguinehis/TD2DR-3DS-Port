#pragma once
// Local player: scr_move_basic + scr_collision_basic + scr_collision_objects and the
// Step/End Step/animation pipeline shared by every playable object. Character-specific
// behaviour lives in source/chars/ (see chars/chars.h). Variable names follow the GML.

#include "common.h"
#include "world.h"

// Survivor animation states (scr_survivors); states from 14 up are per character.
enum {
    ST_IDLE, ST_WALK, ST_RUN, ST_JUMP, ST_FALL, ST_HURT, ST_DEAD, ST_LOOKUP, ST_LOOKDOWN,
    ST_EMOTION1, ST_EMOTION2, ST_EMOTION3, ST_SPIN, ST_BALANCING,
    ST_TAILS_FLY = ST_BALANCING + 1, ST_TAILS_ATTACK1, ST_TAILS_ZIPLINE,
};

enum { CHARACTER_EXE, CHARACTER_TAILS, CHARACTER_KNUX, CHARACTER_EGGMAN, CHARACTER_AMY, CHARACTER_CREAM, CHARACTER_SALLY };
enum { EXE_ORIGINAL, EXE_CHAOS, EXE_EXETIOR, EXE_EXELLER };

// GameMaker-style keys: held this frame and pressed this frame. em1..em3 are the emotion
// (taunt) keys, pressed only.
typedef struct {
    bool left, right, up, down, a, b, c;
    bool left_p, right_p, up_p, down_p, a_p, b_p, c_p;
    bool em1, em2, em3, em1_p, em2_p, em3_p;
} Keys;

typedef struct {
    bool coll;
    float x, y;
} Sensor;

struct CharDef;

typedef struct Player {
    int character;         // CHARACTER_*
    int exe_character;     // EXE_* when character == CHARACTER_EXE
    const struct CharDef *def;

    float x, y;
    float gspd, xspd, yspd, angle;
    int state;
    int hurttime, rings, hp;
    float effectTime;
    int flyTimer;
    float flyGrv;
    int attackTimer, attackCharge;
    float attackAfter, recoil;
    float attackKDir;
    int revivalTimes, shockedTimer, redRingTimer, bounceTimer;
    int deadTimer;
    bool isGrounded, isJumping, isLookingUp, isLookingDown, isDead, isHurt, isFlying, isSpinning;
    bool justJumped, isHiding, isAttacking, emotion, isBoosting, isZipline, isOnEdge, isSlow;
    int edgeDir;
    bool canMove, canLookDown, canLookUp, canSpinDash, canSpin;
    float acc, maxHSpeed, maxYSpeed, yAccel, jumpForce;
    int alarm4;            // alarm[4]: slow-down end (scr_player_slow)
    bool emHeld[3];
    // EXE state shared with the network code (net_state_game sets won / lost)
    bool won, lost;
    int invisTimer;
    bool taunt1, taunt2;   // EXE taunt unlocks (EMOTION1 / EMOTION2)        // keyboard_check(KeyEm1..3) this frame (Animation End taunt loops)

    // Sensors (obj_player_sensor*)
    Sensor sTL, sTR, sL, sR, sBL, sBR, sAL, sAR;

    // Drawing (sprite_index, image_index, image_speed, image_xscale)
    int sprite;
    float image_index, image_speed;
    float image_xscale;

    // Character-only variables (CHAR_VARS in chars/chars.h)
    _Alignas(8) u8 vars[512];
} Player;

// player_init: shared Create defaults, then the character's init. exe_character is used
// when character == CHARACTER_EXE.
void player_init(Player *p, int character, int exe_character, float x, float y);
void player_step(Player *p, const Keys *k, int room_w, int room_h);  // Step + End Step + animation
void player_draw(const Player *p, float cam_x, float cam_y);

// scr_player_hurt. With ignore, a hit during invulnerability still knocks the player back.
void player_hurt(Player *p, int damage, float xpw, float ypw);
// scr_player_hurt with its sound argument (snd_spike, snd_lavahit)
void player_hurt_snd(Player *p, int damage, float xpw, float ypw, int snd);
void player_hurt_ex(Player *p, int damage, float xpw, float ypw, bool ignore);
// scr_player_slow(seconds)
void player_slow(Player *p, float seconds);

// Online hooks (game.c). player_on_death runs when hp reaches 0 (scr_player_instakill);
// player_short_hurt shortens hurt invulnerability to 1 s in the last minute;
// player_controls mirrors global.playerControls (inputs ignored when false).
extern void (*player_on_death)(Player *p);
extern bool player_short_hurt;
extern bool player_controls;
// Level code: player_controls_lock keeps global.playerControls false (zipline, lift);
// player_swap_dirs swaps left/right and up/down (Marijuna crystals).
extern bool player_controls_lock, player_swap_dirs;
// The current room (ROOM_*), for the few room checks in the shared player scripts.
extern int player_room_id;
// global.timeMinutes / global.timeSeconds (match clock; 9:00 offline)
extern int player_time_min, player_time_sec;

// audio_play_sound + net_sound_emit: play locally and let the other players hear it.
// An invisible EXE only emits snd_spring. local_only skips the network part.
void player_sound(const Player *p, int snd);
void player_sound_local(int snd);
// A character voice line (global.net_snds from index 46) is playing: taunts and kill lines
// do not overlap.
bool player_voice_playing(void);

// CLIENT_PLAYER_DATA bit flags for the local player.
u8 player_net_flags(const Player *p);

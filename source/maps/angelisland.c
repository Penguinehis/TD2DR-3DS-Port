// room_angelisland: obj_aiz_zipline (the handle between an obj_aiz_zipline_start and an
// obj_aiz_zipline_end with the same gid). The room creation code only sets the parallax
// table, splits the level art (both done by the room exporter) and clears layer effects on
// Android (nothing to do here).
#include <math.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

// Character variables the zipline resets. These mirror the leading fields of KnuxVars
// (chars/knux.c) and AmyVars (chars/amy.c); keep them in sync.
typedef struct {
    int glideTimer, glideTimeout;
    float glide_xspd;
    bool isGliding, isStuck;
} ZipKnuxVars;

typedef struct {
    int hjumpTimer;
    bool isHJ;
} ZipAmyVars;

// The ZIPLINE state of each character (the *_ZIPLINE macros of chars/*.c).
static int zipline_state(const Player *p)
{
    switch (p->character) {
    case CHARACTER_EXE:
        return p->exe_character == EXE_CHAOS ? ST_BALANCING + 7 : ST_BALANCING + 6;
    case CHARACTER_TAILS: return ST_TAILS_ZIPLINE;
    case CHARACTER_KNUX: return ST_BALANCING + 4;
    case CHARACTER_EGGMAN: return ST_BALANCING + 2;
    case CHARACTER_AMY: return ST_BALANCING + 4;
    case CHARACTER_CREAM: return ST_BALANCING + 3;
    case CHARACTER_SALLY: return ST_BALANCING + 4;
    default: return -1;
    }
}

// Instance variable of a placed instance without a MapObj (like mapobj_prop).
static float inst_prop(const RoomInstance *in, const char *name, float def)
{
    for (int i = in->prop_count - 1; i >= 0; i--)
        if (!strcmp(in->props[i].name, name)) {
            const char *v = in->props[i].value;
            if (!v || !*v) return def;
            if (!strcmp(v, "true")) return 1;
            if (!strcmp(v, "false")) return 0;
            return (float)atof(v);
        }
    return def;
}

// ------------------------------------------------------------------ obj_aiz_zipline

typedef struct {
    bool found;                // zstart and zend exist
    float sx, sy, ex, ey;      // zstart.x/y, zend.x/y
    float zspeed, zvspeed, progress, camY;
    int timeout;
    bool hasPlayer;
} Zipline;

static bool find_end(int object, int gid, float *x, float *y)
{
    bool found = false;
    for (int i = 0;; i++) {
        RoomInstance *in = room_instance_find(&level.room, object, i);
        if (!in) break;
        if ((int)inst_prop(in, "gid", 0) != gid) continue;
        *x = in->x;  // the GML with() loop keeps the last match
        *y = in->y;
        found = true;
    }
    return found;
}

static void zipline_create(MapObj *o)
{
    Zipline *z = MAPOBJ_VARS(o, Zipline);
    z->zspeed = 0.001f;
    int gid = (int)mapobj_prop(o, "gid", 0);
    z->found = find_end(OBJ_AIZ_ZIPLINE_START, gid, &z->sx, &z->sy) &&
               find_end(OBJ_AIZ_ZIPLINE_END, gid, &z->ex, &z->ey);
}

// _holdPlayer
static void hold_player(const Zipline *z)
{
    Player *p = &level.player;
    p->xspd = 0;
    p->yspd = 0;
    float dir = z->ex - z->sx;
    if (dir != 0) p->image_xscale = dir > 0 ? 1 : -1;
    p->isSpinning = false;
    p->isJumping = false;
    p->isGrounded = false;
    p->isHurt = false;
    p->isAttacking = false;
    p->isZipline = true;
    p->angle = 0;
    player_controls_lock = true;  // global.playerControls = false
}

// Step_0 (and Draw_72 / Draw_76, which repeat it)
static void zipline_step(MapObj *o)
{
    Zipline *z = MAPOBJ_VARS(o, Zipline);
    if (level.has_player && z->hasPlayer) hold_player(z);
}

// Step_2
static void zipline_end_step(MapObj *o)
{
    Zipline *z = MAPOBJ_VARS(o, Zipline);
    if (z->timeout > 0) z->timeout--;
    if (!z->found || !level.has_player) return;
    Player *p = &level.player;

    if (p->hp > 0 && mapobj_meets_player(o) && !p->isGrounded && !z->hasPlayer && z->progress <= 0 && z->timeout <= 0) {
        if (p->state == zipline_state(p)) return;
        player_sound(p, SND_SNAP);
        z->hasPlayer = true;
        z->progress = 0;
        z->zspeed = 0.001f;
        z->zvspeed = 0;
        p->x = o->x + 12;
        p->y = o->y + 16;
        // obj_camera: centred on the player, look offset cleared
        level.cam_x = floorf(p->x) - TOP_W / 2;
        level.cam_y = floorf(p->y) - TOP_H / 2;
        level.cam_dist = 0;
    }

    if (z->hasPlayer && z->progress < 1) {
        o->x = z->sx + floorf((z->ex - z->sx) * z->progress);
        o->y = z->sy + floorf((z->ey - z->sy) * z->progress);
        p->x = o->x + 12;
        switch (p->character) {
        case CHARACTER_EXE: p->y = o->y + 22; break;
        case CHARACTER_TAILS:
            p->y = o->y + 11;
            p->isFlying = false;
            break;
        case CHARACTER_KNUX: {
            ZipKnuxVars *v = CHAR_VARS(p, ZipKnuxVars);
            p->y = o->y + 22;
            v->isGliding = false;
            if (v->isStuck) {
                p->canMove = true;
                v->isStuck = false;
            }
            break;
        }
        case CHARACTER_EGGMAN: p->y = o->y + 25; break;
        case CHARACTER_AMY:
            p->y = o->y + 21;
            CHAR_VARS(p, ZipAmyVars)->isHJ = false;
            break;
        case CHARACTER_CREAM:
            p->y = o->y + 8;
            p->isFlying = false;
            break;
        case CHARACTER_SALLY: p->y = o->y + 18; break;
        }
        const SpriteInfo *s = sprite_info(o->sprite);
        if (s) p->y += s->height * o->yscale;  // sprite_height

        // keyboard_check_pressed(global.KeyA) (read directly, like the GML: it ignores
        // global.playerControls)
        if (p->isDead || p->hp <= 0 || (z->progress >= 0.1f && (hidKeysDown() & KEY_A))) {
            p->isZipline = false;
            z->progress = 1;
        } else {
            hold_player(z);
        }

        if (z->zspeed < 0.016f) z->zspeed += 0.0001f;
        z->progress += z->zspeed;
        if (z->progress >= 1) {
            z->camY = level.cam_y;
            float dir = z->ex - z->sx;
            p->xspd = (dir > 0 ? 1 : dir < 0 ? -1 : 0) * z->zspeed * 150;
        }
    }

    if (z->progress >= 1) {
        z->zvspeed += 0.32f;
        z->timeout = 60 * 3;
        o->y += z->zvspeed;
        // The GML camera is 270 high; 480 below its top is well off screen either way.
        if (o->y >= z->camY + 480) {
            o->x = z->sx;
            o->y = z->sy;
            z->progress = 0;
            z->zspeed = 0.001f;
            z->zvspeed = 0;
        }
        player_controls_lock = false;  // global.playerControls = true
        z->hasPlayer = false;
    }
}

// ------------------------------------------------------------------ level

static const ObjDef OBJECTS[] = {
    { .object = OBJ_AIZ_ZIPLINE, .create = zipline_create, .step = zipline_step, .end_step = zipline_end_step },
    { .object = -1 },
};

const MapModule MAP_ANGELISLAND = {
    .room = ROOM_ANGELISLAND,
    .objects = OBJECTS,
};

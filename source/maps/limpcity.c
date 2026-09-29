// room_limpcity: obj_limpcity_echain1/2 (electric chains), obj_limpcity_eyeA (the eye the
// players stand on to look through an eyeB), obj_limpcity_eyeB (camera eyes),
// obj_limpcity_eyechain/2 (swinging chains the eyes hang from), obj_redring_screen2 (the faint
// red overlay created by the room), SERVER_LCEYE_STATE, SERVER_LCCHAIN_STATE,
// CLIENT_LCEYE_REQUEST_ACTIVATE.
//
// The eye view (global.cameraMode 4 with obj_camera.target = the eyeB) is exposed through
// maps_limpcity_camera(): the camera code has to call it (see the report / level.c).
#include <math.h>

#include "../audio.h"
#include "../game.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

#define SOUNDEMT_LCCHAIN 1

static struct {
    bool eye_view;          // global.cameraMode == 4 (looking through an eyeB)
    MapObj *eye_target;     // obj_camera.target
} lc;

static float current_time_ms(void) { return level.time * 1000.0f / 60; }
static float deg2rad(float d) { return d * (float)M_PI / 180.0f; }
static float lengthdir_x(float len, float dir) { return len * cosf(deg2rad(dir)); }
static float lengthdir_y(float len, float dir) { return -len * sinf(deg2rad(dir)); }
static float sgn(float v) { return (v > 0) - (v < 0); }

// Room position the camera centres on while looking through an eye (obj_camera cameraMode 4).
// Returns false when the normal camera applies.
bool maps_limpcity_camera(float *x, float *y);
bool maps_limpcity_camera(float *x, float *y)
{
    if (level.room_id != ROOM_LIMPCITY || !lc.eye_view || !lc.eye_target) return false;
    *x = floorf(lc.eye_target->x);
    *y = floorf(lc.eye_target->y);
    return true;
}

// ------------------------------------------------------------------ obj_limpcity_echain1 (+ echain2)

typedef struct {
    bool active;
} EChain;

static void echain_create(MapObj *o)
{
    MAPOBJ_VARS(o, EChain)->active = false;
    o->image_speed = 0;
}

static void echain_step(MapObj *o)
{
    bool active = MAPOBJ_VARS(o, EChain)->active;
    o->image_index = active ? (float)((int)floorf(current_time_ms() / 50) % 2) : 0;
    if (!level.has_player) return;
    Player *p = &level.player;
    if (active && mapobj_meets_player(o)) player_hurt(p, 20, -p->image_xscale * 4, -6);
}

// ------------------------------------------------------------------ obj_limpcity_eyechain / eyechain2

typedef struct {
    float oX, oY, range, delay;
    int sid, tid;
} EyeChain;

static void eyechain_create(MapObj *o)
{
    EyeChain *v = MAPOBJ_VARS(o, EyeChain);
    v->oX = o->x;
    v->oY = o->y;
    v->sid = (int)mapobj_prop(o, "sid", -1);
    v->tid = (int)mapobj_prop(o, "tid", -1);
    v->range = mapobj_prop(o, "range", 1);
    v->delay = mapobj_prop(o, "delay", 0);
}

static void eyechain_step(MapObj *o)
{
    EyeChain *v = MAPOBJ_VARS(o, EyeChain);
    float s = sinf(current_time_ms() / 200 + v->delay);
    o->angle = (o->object == OBJ_LIMPCITY_EYECHAIN2 ? 180 : 0) + s * v->range * 8;
    o->x = v->oX + s * v->range;
    o->y = v->oY + cosf(current_time_ms() / 200 + v->delay) * v->range;
}

static float spr_w(const MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    return s ? s->width * o->xscale : 0;
}

static float spr_h(const MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    return s ? s->height * o->yscale : 0;
}

// obj_limpcity_eyechain End Step: hang from the chain link with sid == tid
static void eyechain_end_step(MapObj *o)
{
    EyeChain *v = MAPOBJ_VARS(o, EyeChain);
    if (o->object != OBJ_LIMPCITY_EYECHAIN || v->tid == -1) return;
    for (int i = 0;; i++) {
        MapObj *c = mapobj_find(OBJ_LIMPCITY_EYECHAIN, i);
        if (!c) break;
        if (c->object != OBJ_LIMPCITY_EYECHAIN || MAPOBJ_VARS(c, EyeChain)->sid != v->tid) continue;
        v->oX = c->x - lengthdir_y(spr_h(c), c->angle) - spr_w(c) / 2 - 2;
        v->oY = c->y + lengthdir_x(spr_h(c), c->angle) - 2;
    }
}

// ------------------------------------------------------------------ obj_limpcity_eyeA / eyeB

typedef struct {
    float oX, oY, offset, delay, angle;
    int tid, angle_timer;
    // eyeA
    bool used, requested, over;
    int useID, charge, target1, target2;
} Eye;

static void eye_create(MapObj *o)
{
    Eye *v = MAPOBJ_VARS(o, Eye);
    v->oX = o->x;
    v->oY = o->y;
    v->offset = 14;
    v->delay = 40;
    v->tid = (int)mapobj_prop(o, "tid", -1);
    v->angle_timer = 60;
    v->angle = 0;
    v->used = v->requested = v->over = false;
    v->useID = -1;
    v->charge = 100;
    v->target1 = (int)mapobj_prop(o, "target1", 0);
    v->target2 = (int)mapobj_prop(o, "target2", 0);
    if (o->nid < 0) o->nid = 0;
    o->image_speed = 0;
    o->image_index = o->object == OBJ_LIMPCITY_EYEA ? floorf(v->charge / 100.0f * 5) : 0;
}

static void eye_swing(MapObj *o, Eye *v)
{
    o->x = v->oX + sinf(current_time_ms() / 200 + v->delay) * 2;
    o->y = v->oY + v->offset;
    if (v->angle_timer-- <= 0) {
        static const float choices[] = { -16, 16, -8, 8 };
        v->angle = choices[rand() % 4];
        v->angle_timer = 60;
    }
    o->angle = v->angle;
}

static void send_eye_request(bool val, int nid, int target)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_LCEYE_REQUEST_ACTIVATE);
    pkt_u8(&pk, val);
    pkt_u8(&pk, (u8)nid);
    pkt_u8(&pk, (u8)target);
    net_send(&pk, true);
}

static void eyeA_step(MapObj *o)
{
    Eye *v = MAPOBJ_VARS(o, Eye);
    eye_swing(o, v);
    o->image_index = floorf(v->charge / 100.0f * 5);
    bool use = v->used && v->useID == net.id;
    if (fabsf(v->angle) > 0) v->angle -= 0.5f * sgn(v->angle);

    if (!level.has_player) return;
    const Player *p = &level.player;
    if (p->isDead) return;

    if (mapobj_meets_player(o)) {
        bool lookDown = p->isLookingDown, lookUp = p->isLookingUp;
        v->over = true;
        if (!use && !v->requested) {
            if (v->charge < 20) return;
            if (lookDown) {
                send_eye_request(true, o->nid, v->target1);
                v->requested = true;
            } else if (lookUp) {
                send_eye_request(true, o->nid, v->target2);
                v->requested = true;
            }
        }
        if (use && !lookDown && !lookUp) {
            send_eye_request(false, o->nid, v->target2);
            lc.eye_view = false;
        }
    } else {
        if (use) {
            send_eye_request(false, o->nid, v->target2);
            lc.eye_view = false;
        }
        v->requested = false;
        v->over = false;
    }
}

static void eyeB_step(MapObj *o)
{
    Eye *v = MAPOBJ_VARS(o, Eye);
    eye_swing(o, v);
    if (fabsf(v->angle) > 0) v->angle -= 0.5f * sgn(v->angle);
}

// End Step (both eyes): hang from the end of their chain
static void eye_end_step(MapObj *o)
{
    Eye *v = MAPOBJ_VARS(o, Eye);
    for (int i = 0;; i++) {
        MapObj *c = mapobj_find(OBJ_LIMPCITY_EYECHAIN, i);
        if (!c) break;
        if (MAPOBJ_VARS(c, EyeChain)->sid != v->tid) continue;
        v->oX = c->x - lengthdir_y(spr_h(c), c->angle) - spr_w(c) / 2 - 2;
        v->oY = c->y + lengthdir_x(spr_h(c), c->angle) - 2;
        v->offset = 14;
    }
    for (int i = 0;; i++) {
        MapObj *c = mapobj_find(OBJ_LIMPCITY_EYECHAIN2, i);
        if (!c) break;
        if (MAPOBJ_VARS(c, EyeChain)->sid != v->tid) continue;
        v->oX = c->x - lengthdir_y(spr_h(c), c->angle) + spr_w(c) / 2 + 2;
        v->oY = c->y + lengthdir_x(spr_h(c), c->angle) + 2;
        v->offset = -14;
    }
}

static void eyeA_draw(MapObj *o)
{
    mapobj_draw_self(o);
    if (MAPOBJ_VARS(o, Eye)->over)
        sprite_draw(SPR_LIMPCITY_EYE_HINT, (float)((int)floorf(current_time_ms() / 400) % 4), o->x - level.cam_x,
                    o->y - level.cam_y, 1, 1, 0, 0xFFFFFFFF, 1);
}

static void eyeB_draw(MapObj *o)
{
    float x = o->x - level.cam_x, y = o->y - level.cam_y;
    sprite_draw(o->sprite, o->image_index, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    if (!MAPOBJ_VARS(o, Eye)->used) return;
    // The pupil follows the nearest player (us or a visible puppet within 512 px) if within 270 px
    float angle = 0, tx = 0, ty = 0, best = 1e9f;
    bool found = false, any = game_nearest_puppet(o->x, o->y, 512, &tx, &ty);
    if (any) best = sqrtf((tx - o->x) * (tx - o->x) + (ty - o->y) * (ty - o->y));
    if (level.has_player) {
        float d = sqrtf((level.player.x - o->x) * (level.player.x - o->x) + (level.player.y - o->y) * (level.player.y - o->y));
        if (!any || d <= best) {
            tx = level.player.x;
            ty = level.player.y;
            best = d;
            any = true;
        }
    }
    if (any && best < 270) {
        angle = atan2f(-(ty - o->y), tx - o->x) * 180.0f / (float)M_PI;  // point_direction
        found = true;
    }
    sprite_draw(found ? SPR_LIMPCITY_EYELID : SPR_LIMPCITY_EYE_RECHARGE, 5, x, y, 1, 1, angle, 0xFFFFFFFF,
                o->image_alpha);
}

// ------------------------------------------------------------------ level

static void draw_gui(void)
{
    // obj_redring_screen2 (created by the room): spr_redring_fore over the view at alpha 0.1
    // (a flat red tint: the animated full-screen sprite costs ~500 tiles a frame on Old 3DS)
    C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32(150, 0, 0, 22));
}

static bool chain_emitter(const MapObj *e)
{
    const char *s = mapobj_prop_str(e, "stype");
    if (!s) return false;
    return !strcmp(s, "SOUNDEMT_LCCHAIN") || atoi(s) == SOUNDEMT_LCCHAIN;
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    switch (type) {
    case SERVER_LCCHAIN_STATE: {
        if (pass) return true;
        u8 active = rd_u8(r);
        if (!mapobj_number(OBJ_LIMPCITY_ECHAIN1)) return true;
        for (int i = 0;; i++) {
            MapObj *c = mapobj_find(OBJ_LIMPCITY_ECHAIN1, i);
            if (!c) break;
            MAPOBJ_VARS(c, EChain)->active = active == 1;
        }
        for (int i = 0;; i++) {
            MapObj *e = mapobj_find(OBJ_SOUNDEMITTER, i);
            if (!e) break;
            if (!chain_emitter(e)) continue;
            if (active == 0) maps_sound_at(SND_ECHAIN_PREPARE, e->x, e->y - 16);
            else if (active == 1) maps_sound_at(SND_ECHAIN, e->x, e->y - 16);
        }
        return true;
    }
    case SERVER_LCEYE_STATE: {
        if (pass) return true;
        if (!mapobj_number(OBJ_LIMPCITY_EYEA)) return true;
        int id = rd_u8(r);
        bool used = rd_u8(r);
        int useID = rd_u16(r);
        int target = rd_u8(r);
        int charge = rd_u8(r);
        for (int i = 0;; i++) {
            MapObj *e = mapobj_find(OBJ_LIMPCITY_EYEA, i);
            if (!e) break;
            if (e->nid != id) continue;
            Eye *v = MAPOBJ_VARS(e, Eye);
            v->requested = false;
            v->used = used;
            v->useID = useID;
            v->charge = charge;
            break;
        }
        for (int i = 0;; i++) {
            MapObj *e = mapobj_find(OBJ_LIMPCITY_EYEB, i);
            if (!e) break;
            if (e->nid != target) continue;
            Eye *v = MAPOBJ_VARS(e, Eye);
            v->used = used;
            if (used && useID == net.id) {
                lc.eye_view = true;  // cameraMode 0 -> 4 (not while spectating: the player is alive)
                lc.eye_target = e;
            }
            e->sprite = used ? SPR_LIMPCITY_EYE : SPR_LIMPCITY_EYE_RECHARGE;
            break;
        }
        if (!used && useID == net.id) lc.eye_view = false;
        return true;
    }
    default:
        return false;
    }
}

static void step(void)
{
    // The eye view ends when the player dies (the GML switches cameraMode to 3 in
    // scr_player_instakill, which also leaves mode 4).
    if (lc.eye_view && (!level.has_player || level.player.isDead)) lc.eye_view = false;
}

static void leave(void) { memset(&lc, 0, sizeof lc); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_LIMPCITY_ECHAIN1, .create = echain_create, .step = echain_step },  // echain2 inherits
    { .object = OBJ_LIMPCITY_EYECHAIN, .create = eyechain_create, .step = eyechain_step, .end_step = eyechain_end_step },
    { .object = OBJ_LIMPCITY_EYECHAIN2, .create = eyechain_create, .step = eyechain_step },
    { .object = OBJ_LIMPCITY_EYEA, .create = eye_create, .step = eyeA_step, .end_step = eye_end_step, .draw = eyeA_draw },
    { .object = OBJ_LIMPCITY_EYEB, .create = eye_create, .step = eyeB_step, .end_step = eye_end_step, .draw = eyeB_draw },
    { .object = OBJ_SOUNDEMITTER },
    { .object = -1 },
};

const MapModule MAP_LIMPCITY = {
    .room = ROOM_LIMPCITY,
    .objects = OBJECTS,
    .step = step,
    .draw_gui = draw_gui,
    .packet = packet,
    .leave = leave,
};

// room_torturecave: obj_am_controller (acid damage + water drops), obj_abadon_cloud (acid
// clouds, SERVER_TCGOM_STATE), obj_abandon_mine (the face that fades in during the last
// minute), obj_abandon_face (faces turning towards the player). obj_am_hideobject is a plain
// obj_hiding_parent child (handled by the world) and obj_am_collision a solid.
//
// The GML room has no darkness or searchlight object: the only overlays are the acid clouds,
// the fading face (Draw GUI) and the level art at depth -200 drawn with image_alpha 0.8.
#include <math.h>

#include "../audio.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

static struct {
    bool collision;            // obj_am_controller.collision (set by the clouds this frame)
    int timer;                 // obj_am_controller.timer
    int drops;                 // snd_waterdrops loop
    float mine_alpha;          // obj_abandon_mine.image_alpha
} tc;

// ------------------------------------------------------------------ obj_am_controller

static void controller_create(MapObj *o)
{
    (void)o;
    tc.timer = 0;
    tc.collision = false;
    tc.drops = audio_play_ex(SND_WATERDROPS, 1, true);
}

static void controller_begin_step(MapObj *o)
{
    (void)o;
    tc.collision = false;
}

static void controller_end_step(MapObj *o)
{
    (void)o;
    if (!level.has_player) return;
    Player *p = &level.player;
    if (tc.collision) {
        if (tc.timer > 0) {
            tc.timer--;
        } else {
            if (p->rings <= 0) {
                player_hurt(p, 20, -p->image_xscale * 4, -6);
            } else {
                player_sound(p, SND_RINGABSORB);
                p->rings--;
            }
            tc.timer = (int)(60 * .6);
        }
    } else {
        tc.timer = (int)(60 * .1);
    }
}

static void controller_destroy(MapObj *o)
{
    (void)o;
    audio_stop(tc.drops);
    tc.drops = -1;
}

// ------------------------------------------------------------------ obj_abadon_cloud

typedef struct {
    bool damage;
} Cloud;

static void cloud_create(MapObj *o)
{
    MAPOBJ_VARS(o, Cloud)->damage = false;
    o->depth = 101;
    o->visible = false;
}

static void cloud_step(MapObj *o)
{
    int last = mapobj_frames(o) - 1;
    if (o->image_index >= last) {
        o->image_index = last;
        o->image_speed = 0;  // stays on the last frame (the GML clamps it every step)
        o->visible = false;
        return;
    }
    if (!o->visible || !level.has_player) return;
    if (o->image_index >= 18 && MAPOBJ_VARS(o, Cloud)->damage && mapobj_meets_player(o)) tc.collision = true;
}

// ------------------------------------------------------------------ obj_abandon_mine

static void mine_draw(MapObj *o) { (void)o; }  // Draw_0 is empty

static void mine_draw_gui(MapObj *o)
{
    // if(global.timeMinutes > 0 || !obj_netclient.isServerReady) image_alpha = 0. The clock
    // shows the full match time until the server is ready, so the minutes test covers both.
    if (player_time_min > 0) tc.mine_alpha = 0;
    else if (tc.mine_alpha < 1) tc.mine_alpha += 0.016f / 60;
    // draw_self at GUI (0, 0); the 480x270 GUI is mapped onto the 400x240 screen
    sprite_draw(o->sprite, o->image_index, 0, 0, o->xscale * TOP_W / 480.0f, o->yscale * TOP_H / 270.0f, 0,
                o->blend, tc.mine_alpha);
}

// ------------------------------------------------------------------ obj_abandon_face

static void face_step(MapObj *o)
{
    if (!level.has_player) return;
    // point_direction(x, y, player.x, player.y): degrees counter-clockwise, y down
    float dx = level.player.x - o->x, dy = level.player.y - o->y;
    float a = atan2f(-dy, dx) * 180.0f / (float)M_PI;
    if (a < 0) a += 360;
    o->angle = a;
}

// ------------------------------------------------------------------ level

static void init(void)
{
    // RoomCreationCode: the level art in front of the player (depth -200) at alpha 0.8
    for (int i = 0; i < level.room.layer_count; i++)
        if (level.room.layers[i].type == LAYER_LEVELART && level.room.layers[i].depth <= -200)
            level.room.layers[i].art_alpha = 0.8f;
    // obj_abadon_cloud sets depth = 101 in Create: behind the player (the room places the
    // clouds on a layer at depth -20, in front of it). Placed instances are drawn with their
    // layer, so each cloud is replaced by a runtime copy, which the framework draws before
    // the player.
    int n = mapobj_number(OBJ_ABADON_CLOUD);
    for (int i = 0; i < n; i++) {
        MapObj *c = mapobj_find(OBJ_ABADON_CLOUD, 0);
        if (!c || !c->src) break;
        MapObj *copy = mapobj_create(OBJ_ABADON_CLOUD, c->x, c->y);
        if (copy) {
            copy->nid = (int)mapobj_prop(c, "nid", 0);  // the object's default nid is 0
            copy->xscale = c->xscale;
            copy->yscale = c->yscale;
            copy->angle = c->angle;
            copy->image_index = c->image_index;
            copy->image_speed = c->image_speed;
            copy->blend = c->blend;
            copy->image_alpha = c->image_alpha;
        }
        mapobj_destroy(c);
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    if (type != SERVER_TCGOM_STATE) return false;
    if (pass) return true;
    int gid = rd_u8(r);
    bool pus = rd_u8(r) != 0;
    if (pus)
        for (int i = 0;; i++) {
            MapObj *e = mapobj_find(OBJ_SOUNDEMITTER, i);
            if (!e) break;
            if ((int)mapobj_prop(e, "stype", -1) == gid) maps_sound_at(SND_ACID, e->x, e->y);
        }
    for (int i = 0;; i++) {
        MapObj *c = mapobj_find(OBJ_ABADON_CLOUD, i);
        if (!c) break;
        if (c->nid != gid) continue;
        if (pus) {
            c->image_index = 0;
            c->image_speed = 1;
            c->visible = true;
        }
        MAPOBJ_VARS(c, Cloud)->damage = pus;
    }
    return true;
}

static void leave(void)
{
    audio_stop(tc.drops);  // obj_am_controller CleanUp
    memset(&tc, 0, sizeof tc);
    tc.drops = -1;
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_AM_CONTROLLER, .create = controller_create, .begin_step = controller_begin_step,
      .end_step = controller_end_step, .destroy = controller_destroy },
    { .object = OBJ_ABADON_CLOUD, .create = cloud_create, .step = cloud_step },
    { .object = OBJ_ABANDON_MINE, .draw = mine_draw, .draw_gui = mine_draw_gui },
    { .object = OBJ_ABANDON_FACE, .step = face_step },
    // No behaviour: makes the emitters MapObjs so the packet can find them by stype.
    { .object = OBJ_SOUNDEMITTER },
    { .object = -1 },
};

const MapModule MAP_TORTURECAVE = {
    .room = ROOM_TORTURECAVE,
    .objects = OBJECTS,
    .init = init,
    .packet = packet,
    .leave = leave,
};

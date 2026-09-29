#pragma once

#include "common.h"
#include "gen/objects.h"
#include "gen/rooms.h"

enum LayerType { LAYER_BACKGROUND, LAYER_INSTANCES, LAYER_ASSETS, LAYER_LEVELART };

typedef struct {
    const char *name;
    const char *value;
} InstProp;

typedef struct {
    s16 object;
    float x, y, xscale, yscale, angle, image_speed;
    u32 colour;
    s16 image_index;
    u8 flags;            // bit0: has GML creation code (see ref/rooms/)
    const char *name;    // GameMaker instance name, e.g. inst_1E8FB004
    u8 prop_count;
    InstProp *props;
    int world;           // index into the collision world, -1 if none (set by world_build)
    u16 creation;        // position in the room's instance creation order
    int mapobj;          // runtime object (mapobj.c), -1 if none
} RoomInstance;

typedef struct {
    s16 sprite;
    float x, y, xscale, yscale, angle, head, anim_speed;
    u32 colour;
} RoomAsset;

typedef struct {
    u8 type;
    s32 depth;
    bool visible;
    const char *name;
    bool manual_pos;     // layer_x / layer_y set by code: ignore the parallax table
    // background
    s16 bg_sprite;
    s32 bg_x, bg_y;
    bool htiled, vtiled, stretch;
    u32 bg_colour;
    float hspeed, vspeed;
    // parallax (from global.parallax in the room creation code):
    //   layer pos = (camera_x * px, (camera_y + pyoff) * py)
    bool parallax;
    float px, py, pyoff;
    // level art (scr_level_split): frame k of art_sprite is drawn at (k * width, art_yoff)
    s16 art_sprite;
    float art_yoff;
    float art_alpha;     // image_alpha of the split level art (1 by default)
    // instances / assets
    int count;
    RoomInstance *instances;
    RoomAsset *assets;
} RoomLayer;

typedef struct {
    int id;
    u32 width, height;
    u16 view_w, view_h;
    int layer_count;
    RoomLayer *layers;     // sorted by depth, deepest (drawn first) at index 0
    u8 *blob;
} Room;

#define OBJF_VISIBLE 1
#define OBJF_SOLID 2
#define OBJF_PERSISTENT 4
#define OBJF_CUSTOM_DRAW 8
#define OBJF_NOSTICK 16      // canStuck = false (Knuckles cannot cling to it)

typedef struct PACKED {
    s16 sprite, mask, parent;
    u8 flags;              // bit0 visible, bit1 solid, bit2 persistent, bit3 custom Draw event
} ObjectInfo;

void objects_init(void);
const ObjectInfo *object_info(int obj);
bool object_is_child_of(int obj, int ancestor);

bool room_load(Room *room, int id);
// GameMaker instance_find(obj, n): the n-th instance of obj (or a child) in creation order.
RoomInstance *room_instance_find(Room *room, int obj, int n);
int room_instance_number(Room *room, int obj);
void room_free(Room *room);

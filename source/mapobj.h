#pragma once
// Level objects with behaviour (M6): runtime instances of the GML objects placed in a room
// (controllers, lifts, doors, traps...) or created while playing (instance_create). One
// MapModule per level (source/maps/<level>.c) lists its objects' events and handles the
// level's packets; source/maps/common.c holds objects shared by several levels.
//
// Event mapping for an ObjDef:
//   Create_0 -> create     Step_1 (Begin Step) -> begin_step    Step_0 -> step
//   Step_2 (End Step) -> end_step    Draw_0 -> draw (world pixels; NULL = draw_self)
//   Draw_64 (Draw GUI) -> draw_gui (top-screen pixels)    Alarm_N -> alarm (n = N)
// Instances of an object without an ObjDef keep the default behaviour (drawn by level.c).

#include "common.h"
#include "net.h"
#include "room.h"
#include "world.h"

typedef struct MapObj MapObj;

typedef struct ObjDef {
    int object;                 // OBJ_*; -1 ends a table
    void (*create)(MapObj *o);
    void (*begin_step)(MapObj *o);
    void (*step)(MapObj *o);
    void (*end_step)(MapObj *o);
    void (*draw)(MapObj *o);
    void (*draw_gui)(MapObj *o);
    void (*alarm)(MapObj *o, int n);
    void (*destroy)(MapObj *o);
    bool dynamic;               // its collision shape moves (see world_move)
} ObjDef;

struct MapObj {
    bool used, destroyed;
    const ObjDef *def;
    int object;
    RoomInstance *src;          // placed in the room (NULL when created at runtime)
    WorldInst *world;           // collision shape (placed instances with a category)
    float x, y, xstart, ystart, xscale, yscale, angle;
    int sprite;                 // sprite_index (-1 none)
    int mask;                   // mask_index (-1: the sprite)
    float image_index, image_speed, image_alpha;
    u32 blend;                  // image_blend (0xAARRGGBB, white = no tint)
    bool visible;
    int depth;
    int layer_depth;            // depth of the room layer it was placed on (depth differs: moved)
    int alarm[12];              // counts down; the event runs when it reaches 0
    int nid;                    // network id (instance variable "nid" when present)
    _Alignas(8) u8 vars[192];   // per-object variables: MAPOBJ_VARS(o, T)
};

#define MAPOBJ_VARS(o, T) ((T *)(void *)(o)->vars)

typedef struct MapModule {
    int room;                   // ROOM_*
    const ObjDef *objects;      // terminated by { .object = -1 }
    void (*init)(void);         // after every object's create (room creation code)
    void (*step)(void);         // once per frame after the objects
    void (*draw_front)(void);   // above everything (darkness, fog), world pixels
    void (*draw_gui)(void);     // top-screen overlay
    bool (*packet)(PacketType type, bool pass, NetReader *r, bool reliable);
    void (*leave)(void);
} MapModule;

// Called by level.c
void mapobj_room_start(Room *room);   // before world_build: marks dynamic objects
void mapobj_room_ready(Room *room);   // after world_build: creates the objects
void mapobj_room_end(void);
void mapobj_step(void);
bool mapobj_owns(const RoomInstance *in);      // drawn by the framework, not level.c
void mapobj_draw_instance(const RoomInstance *in);
void mapobj_draw_spawned(bool front);          // runtime instances (front: depth < 0)
// Placed instances whose depth was changed from their layer's: those with lo <= depth < hi
void mapobj_draw_moved(int lo, int hi);
void mapobj_draw_front(void);
void mapobj_draw_gui(void);
bool mapobj_packet(PacketType type, bool pass, NetReader *r, bool reliable);

// GML-style helpers for object code
MapObj *mapobj_create(int object, float x, float y);   // instance_create_depth
void mapobj_destroy(MapObj *o);                        // instance_destroy
MapObj *mapobj_find(int object, int n);                // instance_find (creation order, children too)
int mapobj_number(int object);                         // instance_number
MapObj *mapobj_by_nid(int object, int nid);
void mapobj_draw_self(const MapObj *o);                // draw_self
void mapobj_move(MapObj *o, float x, float y);         // x = ..., y = ... (moves the collision too)
int mapobj_frames(const MapObj *o);                    // image_number
bool mapobj_meets_player(const MapObj *o);
// bbox_left/top/right/bottom (right/bottom exclusive) from the mask or sprite.
bool mapobj_bbox(const MapObj *o, float *l, float *t, float *r, float *b);             // place_meeting(x, y, global.player)
bool mapobj_meets_player_at(const MapObj *o, float x, float y);
// Instance variable from the room (properties or creation code); def when missing.
float mapobj_prop(const MapObj *o, const char *name, float def);
const char *mapobj_prop_str(const MapObj *o, const char *name);

// Room layers (layer_get_id / layer_x / layer_y / layer_set_visible / layer_hspeed)
RoomLayer *layer_find(const char *name);
void layer_set_pos(RoomLayer *l, float x, float y);    // stops following the parallax table

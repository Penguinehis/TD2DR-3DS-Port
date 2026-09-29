#pragma once
// Collision world: every room instance with a collision shape, queried with GameMaker
// semantics (position_meeting / instance_position / collision_rectangle / place_meeting).

#include "common.h"
#include "room.h"

// Categories come from the object parent chain (see world_build).
enum {
    WC_FLOOR = 1 << 0,        // obj_floor_parent and children
    WC_JUMPTHROUGH = 1 << 1,  // obj_platform_jumptrough and children
    WC_SPRING = 1 << 2,       // obj_spring_parent and children
    WC_HD_SPRING = 1 << 3,    // obj_hd_spring
    WC_DAMAGE = 1 << 4,       // obj_damage
    WC_SPIKE = 1 << 5,        // obj_spike
    WC_ANGLE_NULL = 1 << 6,   // obj_angleanuller / obj_angleanuller_eggsafe
    WC_ANGLE_ALLOW = 1 << 7,  // obj_angleallower
    WC_HIDING = 1 << 8,       // obj_hiding_parent and children
    WC_ABYSS_TARGET = 1 << 9, // obj_abyss_target
    WC_DEATH_TP = 1 << 10,    // obj_deathtp_point
    WC_SPAWN = 1 << 11,       // obj_spawnpoint
};

typedef struct WorldInst {
    int object;
    u32 cats;
    float x, y, xscale, yscale, angle;
    int mask_sprite;         // sprite providing the collision shape (-1: none)
    float frame;
    float l, t, r, b;        // world AABB (half-open: l <= x < r)
    const RoomInstance *src;
    // per-object runtime state
    float spring_x, spring_y;
    int spring_cooldown;     // frames until usable again (image_blend != c_white)
    int spring_anim;         // frames showing the pressed frame
    bool dynamic;            // moves at runtime (not in the grid; see world_move)
    bool disabled;           // excluded from every query (a broken or open object)
} WorldInst;

// Objects that move at runtime (lifts, doors, walls) are kept out of the spatial grid and
// tested on every query. Set before world_build (NULL: none).
extern bool (*world_is_dynamic)(int object);

void world_build(Room *room);
// Move a dynamic instance (updates its bounding box); frame changes the mask frame.
void world_move(WorldInst *w, float x, float y);
void world_free(void);

int world_count(void);
WorldInst *world_get(int i);

// Instances whose AABB overlaps the rectangle and match any of cats. Returns count.
int world_query_rect(float l, float t, float r, float b, u32 cats, WorldInst **out, int max);

// GameMaker position_meeting / instance_position against a candidate list, or against
// all instances of the categories when list is NULL.
bool inst_contains_point(const WorldInst *w, float px, float py);
WorldInst *world_point(float px, float py, u32 cats);
WorldInst *list_point(WorldInst **list, int n, float px, float py);

// collision_rectangle: rectangle vs instance shape (precise shapes sampled per pixel).
WorldInst *world_rect(float l, float t, float r, float b, u32 cats);

// Nearest instance of a category to (x, y) by origin distance.
WorldInst *world_nearest(float x, float y, u32 cats);

// Advance timers/animations of world objects (springs).
void world_step(void);

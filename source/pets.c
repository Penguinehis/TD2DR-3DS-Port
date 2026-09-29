#include "pets.h"

#include <math.h>

#include "gen/objects.h"
#include "gen/rooms.h"
#include "level.h"
#include "room.h"
#include "sprite.h"

// global.pets order (scr_pets)
static const int PET_OBJECTS[7] = { OBJ_PET_FLICKY, OBJ_PET_CHAO, OBJ_PET_METAL, OBJ_PET_DADOL, OBJ_PET_MAJIN,
                                    OBJ_PET_MKNUX, OBJ_PET_EGG };

int pet_sprite(int pet)
{
    if (pet < 0 || pet > 6) return -1;
    const ObjectInfo *o = object_info(PET_OBJECTS[pet]);
    return o ? o->sprite : -1;
}

void pet_follow(Pet *pet, int spr, float tx, float ty, float txs, bool hiding)
{
    float dx = pet->x - tx, dy = pet->y - ty;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > 200) {
        pet->x = tx - txs * 32;
        pet->y = ty - 10;
    }
    pet->x += (tx - txs * (hiding ? 16 : 32) - pet->x) * 0.21f;
    // sin(current_time / 500): current_time is in ms
    pet->y += (ty - 10 + sinf(level.time * (1000.0f / 60) / 500) * 5 - pet->y) * 0.21f;
    pet->frame += (0.2f + dist / 150.0f) * sprite_frame_step(spr);
}

void pet_draw(const Pet *pet, int spr, float xscale, float alpha)
{
    if (spr < 0 || alpha <= 0) return;
    u32 blend = level.room_id == ROOM_ACT9 ? 0xFF000000 : 0xFFFFFFFF;
    sprite_draw(spr, pet->frame, floorf(pet->x) - level.cam_x, floorf(pet->y) - level.cam_y, xscale, 1, 0, blend,
                alpha);
}

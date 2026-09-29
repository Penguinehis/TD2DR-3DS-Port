#pragma once
// Pets (obj_pet_flicky .. obj_pet_egg): a small follower behind a player.

#include "common.h"

typedef struct {
    float x, y, frame;
} Pet;

int pet_sprite(int pet);  // -1 for none / the secret pet
// Draw_76 follow: 32 px behind the target (16 while hiding), bobbing; jumps there when 200+ px away
void pet_follow(Pet *pet, int spr, float tx, float ty, float txs, bool hiding);
void pet_draw(const Pet *pet, int spr, float xscale, float alpha);

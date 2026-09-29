#pragma once
// Level modules (M6). Each level file defines a MapModule; maps.c maps rooms to modules.

#include "../mapobj.h"

extern const ObjDef MAP_COMMON_OBJECTS[];  // objects used by several levels (maps/common.c)

const MapModule *maps_module_for(int room);

// Packets for objects shared by several levels (moving spikes...).
bool maps_common_packet(PacketType type, bool pass, NetReader *r, bool reliable);
// scr_audio_play_3d: a sound at a room position, fading with the distance to the camera.
void maps_sound_at(int snd, float x, float y);
// Limp City eye camera (obj_camera mode 4): true with the point to centre on.
bool maps_limpcity_camera(float *x, float *y);
// The level track with in-level swaps (Dot Dot Dot: mus_dotdotdot2 in the trigger3 area).
int maps_music(int room, int music);

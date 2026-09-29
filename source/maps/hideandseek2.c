// room_hideandseek2: CLIENT_SPRING_USE (receive side) and obj_exe_sprindicator, the arrow
// that shows the EXE where a survivor just used a spring.
//
// The room has no controller object; obj_jack_in_the_box has no events (decoration) and the
// springs are the shared obj_spring_parent children. The send side of CLIENT_SPRING_USE lives in
// the shared spring code (scr_collision_objects, see player.c spring_launch).
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
    bool spring;        // obj_exe_sprindicator.spring
    float spr_x, spr_y; // sprX / sprY
    int alarm0;         // alarm[0]: frames left
    float x, y;         // indicator position (End Step)
} hs2;

static void step(void)
{
    // obj_exe_sprindicator Alarm_0
    if (hs2.alarm0 > 0 && --hs2.alarm0 == 0) hs2.spring = false;
    // End Step: 32 px from the player towards the spring
    if (level.has_player && hs2.spring) {
        const Player *p = &level.player;
        float a = atan2f(-(hs2.spr_y - p->y), hs2.spr_x - p->x);
        hs2.x = ceilf(p->x) + cosf(a) * 32;
        hs2.y = ceilf(p->y) - sinf(a) * 32;
    }
}

static void draw_gui(void)
{
    if (!level.has_player || !hs2.spring || level.player.character != CHARACTER_EXE) return;
    float alpha = 0.7f * (hs2.alarm0 / (60 * 2.0f));
    // image_angle is never set in the GML (0)
    sprite_draw(SPR_INDICATOR3, 0, ceilf(hs2.x - level.cam_x), ceilf(hs2.y - level.cam_y), 1, 1, 0, 0xFFFFFFFF, alpha);
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)pass;  // sent by another client with passthrough set: handled either way (as the GML)
    (void)reliable;
    if (type != CLIENT_SPRING_USE) return false;
    rd_u16(r);  // nid
    float x = rd_u16(r), y = rd_u16(r);
    if (!level.has_player || level.player.character != CHARACTER_EXE) return true;
    hs2.spring = true;
    hs2.spr_x = x;
    hs2.spr_y = y;
    hs2.alarm0 = 60 * 2;
    // point_distance(obj_camera.x, obj_camera.y, ...) > 600: a distant reverb
    // (audio_play_sound_at with falloff 250..500 plays it faintly)
    float dx = x - level.cam_x, dy = y - level.cam_y;
    if (sqrtf(dx * dx + dy * dy) > 600) audio_play_ex(SND_SPRING_REVERB, 0.35f, false);
    return true;
}

static void leave(void) { memset(&hs2, 0, sizeof hs2); }

static const ObjDef OBJECTS[] = {
    { .object = -1 },
};

const MapModule MAP_HIDEANDSEEK2 = {
    .room = ROOM_HIDEANDSEEK2,
    .objects = OBJECTS,
    .step = step,
    .draw_gui = draw_gui,
    .packet = packet,
    .leave = leave,
};

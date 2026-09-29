// room_majongforest: obj_majong_controller (the horizontal wrap + teleport flash) and
// obj_majong_trigger (falling into the pit sends the player to the nearest
// obj_majong_trigger_point).
//
// "Wherever you run you will never reach the end": the first and the last 480 pixels of the
// room are identical, and the controller moves the player between x = 240 and
// x = room_width - 240 when it runs towards an edge. The GML camera is clamped to the room,
// so at both positions the view shows the same pixels; the 3DS view (400 wide) is narrower
// than the duplicated strip, so the same holds here and the wrap is seamless.
//
// Wrapped copies drawn by other code in this room: obj_majong_controller Draw_64 draws the
// puppets' name tags a second time at x +- (room_width - 480) (game.c draw_puppet); obj_blackring Draw_0 draws a second black ring at
// x +- (room_width - 512) + sprite_width; scr_audio_play_3d plays a second emitter
// (obj_majong_sound) at x +- (room_width - 512).
#include <math.h>

#include "../audio.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../level.h"
#include "maps.h"

static struct {
    float fade;                // obj_majong_controller.fade (white flash after a teleport)
} mf;

// ------------------------------------------------------------------ obj_majong_controller

static void controller_create(MapObj *o)
{
    o->depth = -900;
    mf.fade = 0;
}

static void controller_step(MapObj *o)
{
    (void)o;
    if (!level.has_player) return;
    Player *p = &level.player;
    float rw = (float)level.room.width;
    if (p->x <= 240 && p->xspd < 0) p->x = rw - 240;
    if (p->x >= rw - 240 && p->xspd > 0) p->x = 240;
}

// ------------------------------------------------------------------ obj_majong_trigger

static void trigger_step(MapObj *o)
{
    if (!level.has_player || !mapobj_meets_player(o)) return;
    Player *p = &level.player;
    // instance_nearest(x, y, obj_majong_trigger_point)
    RoomInstance *best = NULL;
    float best_d = 0;
    for (int i = 0;; i++) {
        RoomInstance *pt = room_instance_find(&level.room, OBJ_MAJONG_TRIGGER_POINT, i);
        if (!pt) break;
        float dx = pt->x - p->x, dy = pt->y - p->y, d = dx * dx + dy * dy;
        if (!best || d < best_d) {
            best = pt;
            best_d = d;
        }
    }
    if (best) {
        p->x = best->x;
        p->y = best->y;
        p->xspd = p->gspd = p->yspd = 0;
    }
    mf.fade = 1;
    audio_play(SND_NPTELEPORT);
}

// ------------------------------------------------------------------ level

static void draw_gui(void)
{
    // obj_majong_controller Draw_64: spr_white at the flash alpha
    if (mf.fade > 0) {
        C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32f(1, 1, 1, fminf(mf.fade, 1)));
        mf.fade -= 0.32f;
    }
}

static void leave(void) { memset(&mf, 0, sizeof mf); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_MAJONG_CONTROLLER, .create = controller_create, .step = controller_step },
    { .object = OBJ_MAJONG_TRIGGER, .step = trigger_step },
    { .object = -1 },
};

const MapModule MAP_MAJINFOREST = {
    .room = ROOM_MAJONGFOREST,
    .objects = OBJECTS,
    .draw_gui = draw_gui,
    .leave = leave,
};

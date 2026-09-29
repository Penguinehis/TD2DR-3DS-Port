// room_fartzone (the practice map): obj_fart_controller (the "hot potato" mermer curse,
// CLIENT_PLAYER_POTATER), obj_fart_mermer, obj_fart_dummy (SERVER_FART_STATE, CLIENT_FART_PUSH)
// with its obj_fart_text damage numbers, obj_fart_ass, obj_darktower_ball (SERVER_DTBALL_STATE)
// and obj_eggstatue. The moving spikes (obj_movingspike + obj_soundemitter) are in common.c.
#include <math.h>
#include <stdio.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "../ui.h"
#include "maps.h"

#define AMY_HJUMP (ST_BALANCING + 2)   // chars/amy.c
#define DUMMY_TEXT "\\im @dumb~"         // scr_text_spr colour codes: red "im ", green "dumb"

static struct {
    bool potater;
    int potatoTimer;
    bool exploded;             // obj_fart_ass created
} fz;

static float current_time_ms(void) { return level.time * 1000.0f / 60; }
static float rnd(float a, float b) { return a + (b - a) * (rand() % 10000) / 10000.0f; }

static void send_potater(u8 type, u16 id, int timer)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_PLAYER_POTATER);
    pk.buf[0] = 1;  // passthrough
    pkt_u8(&pk, type);
    pkt_u16(&pk, id);
    if (type == 0) pkt_u8(&pk, (u8)timer);
    net_send(&pk, true);
}

// ------------------------------------------------------------------ obj_fart_controller

static void controller_create(MapObj *o)
{
    (void)o;
    fz.potater = false;
    fz.potatoTimer = 0;
    fz.exploded = false;
}

// Step_0 hands the curse to a puppet touching the player (CLIENT_PLAYER_POTATER 0 with its id and
// 4, then 1 with ours) — the puppets live in game.c and are not reachable from here (see the
// report). Draw_72 (the countdown) runs here, after the player's step, so the sprite set for
// the curse is the one drawn this frame.
static void controller_end_step(MapObj *o)
{
    (void)o;
    if (!level.has_player || !fz.potater) return;
    Player *p = &level.player;
    fz.potatoTimer--;
    p->sprite = SPR_MERFURMU;
    if (fz.potatoTimer % 60 == 0) {
        send_potater(0, net.id, fz.potatoTimer / 60 - 1);
        player_sound(p, SND_ROAR);
    }
    if (fz.potatoTimer <= 0 && !fz.exploded) {
        fz.exploded = true;
        mapobj_create(OBJ_FART_ASS, 0, 0);
    }
}

static void controller_draw(MapObj *o)
{
    (void)o;
    if (!level.has_player || !fz.potater) return;
    const Player *p = &level.player;
    sprite_draw(SPR_GOODPERSON, fz.potatoTimer / 60, p->x - level.cam_x, p->y - level.cam_y, 1, 1, 0, 0xFFFFFFFF, 1);
}

// ------------------------------------------------------------------ obj_fart_ass

// The GML ends the game (game_end) half a second after the explosion; the port only shows it.
static void ass_create(MapObj *o)
{
    o->alarm[0] = 30;
    o->depth = -999;
    audio_play(SND_BOOM);
}

static void ass_alarm(MapObj *o, int n)
{
    if (n == 0) mapobj_destroy(o);  // game_end() in the GML
}

static void ass_draw(MapObj *o) { (void)o; }

static void ass_draw_gui(MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    float xs = TOP_W / 480.0f, ys = TOP_H / 270.0f;
    sprite_draw(o->sprite, o->image_index, (s ? s->xorigin : 0) * xs, (s ? s->yorigin : 0) * ys, xs, ys, 0,
                0xFFFFFFFF, 1);
}

// ------------------------------------------------------------------ obj_fart_mermer

typedef struct {
    float sx, sy;
} Mermer;

static void mermer_create(MapObj *o)
{
    Mermer *m = MAPOBJ_VARS(o, Mermer);
    m->sx = o->x;
    m->sy = o->y;
}

static void mermer_step(MapObj *o)
{
    Mermer *m = MAPOBJ_VARS(o, Mermer);
    o->x = m->sx + rnd(-3, 3);
    o->y = m->sy + rnd(-3, 3);
    if (level.has_player && !fz.potater && mapobj_meets_player(o)) {
        fz.potater = true;
        fz.potatoTimer = 60 * 4;
    }
}

// ------------------------------------------------------------------ obj_fart_text

typedef struct {
    char text[8];
    float vel, scale;
    bool ignore, doShit;
    u32 color;
} FartText;

static void text_create(MapObj *o)
{
    FartText *t = MAPOBJ_VARS(o, FartText);
    t->text[0] = 0;
    t->vel = 5;
    t->scale = 1;
    t->ignore = t->doShit = false;
}

static void text_alarm(MapObj *o, int n)
{
    if (n == 0) MAPOBJ_VARS(o, FartText)->doShit = true;
}

static void text_step(MapObj *o)
{
    FartText *t = MAPOBJ_VARS(o, FartText);
    o->y -= t->vel;
    // merge_color(#ee9428, #874d05, (sin(current_time / 100) + 1) / 2)
    float k = (sinf(current_time_ms() / 100) + 1) / 2;
    t->color = C2D_Color32((u8)(0xee + (0x87 - 0xee) * k), (u8)(0x94 + (0x4d - 0x94) * k),
                           (u8)(0x28 + (0x05 - 0x28) * k), 255);
    if (!t->ignore) {
        t->vel -= 0.15f;
        if (t->vel < 0) {
            t->vel = 0;
            t->ignore = true;
            o->alarm[0] = 60;
        }
    }
    if (t->doShit) {
        o->y -= 0.05f;
        t->scale -= 1.05f - t->scale;
        if (t->scale <= 0) mapobj_destroy(o);
    }
}

static void text_draw(MapObj *o)
{
    FartText *t = MAPOBJ_VARS(o, FartText);
    if (t->scale <= 0) return;
    float size = 0.7f * t->scale;  // fnt_big
    ui_text_center(o->x - level.cam_x, o->y - level.cam_y - 15 * size, size, t->color, "%s", t->text);
}

// ------------------------------------------------------------------ obj_fart_dummy

typedef struct {
    int timer;
} Dummy;

static void dummy_hit(MapObj *o, int push, const char *dmg, int timer)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_FART_PUSH);
    pkt_u8(&pk, (u8)(s8)push);
    net_send(&pk, true);
    static const int snds[3] = { SND_DUMMY, SND_DUMMY2, SND_DUMMY3 };
    audio_play(snds[rand() % 3]);
    MapObj *t = mapobj_create(OBJ_FART_TEXT, o->x, o->y);
    if (t) snprintf(MAPOBJ_VARS(t, FartText)->text, sizeof MAPOBJ_VARS(t, FartText)->text, "%s", dmg);
    MAPOBJ_VARS(o, Dummy)->timer = timer;
}

static void dummy_step(MapObj *o)
{
    Dummy *d = MAPOBJ_VARS(o, Dummy);
    if (!level.has_player) return;
    if (d->timer > 0) {
        d->timer--;
        return;
    }
    const Player *p = &level.player;
    bool demon = p->character == CHARACTER_EXE || p->revivalTimes >= 2;
    o->image_index = !demon;
    // Hits by obj_tails_projectile ("+<dmg>s" / "-<dmg/20>", push sign * charge) and
    // obj_exetior_stompballs ("-1", push image_xscale) need entities.c (see the report).
    if (!mapobj_meets_player(o)) return;
    bool attacking = p->isAttacking;
    if (p->character == CHARACTER_AMY && p->state == AMY_HJUMP && p->image_index < 3) attacking = true;
    if (demon && (p->state == ST_JUMP || p->state == ST_FALL || p->state == ST_SPIN) &&
        p->character != CHARACTER_EGGMAN && p->character != CHARACTER_AMY && p->character != CHARACTER_SALLY) {
        dummy_hit(o, (int)(p->image_xscale * 3), "-1", 20);
        return;
    }
    if (!attacking) return;
    const char *dmg = "0";
    float push = 0, xs = p->image_xscale;
    if (p->character == CHARACTER_EXE) {
        if (p->exe_character == EXE_CHAOS) { dmg = "-1"; push = xs; }
        else { dmg = "-2"; push = xs * 2; }
    } else if (p->revivalTimes < 2) {
        switch (p->character) {
        case CHARACTER_KNUX:
        case CHARACTER_AMY: dmg = "+3s"; push = xs * 1.5f; break;
        case CHARACTER_EGGMAN:
        case CHARACTER_SALLY: dmg = "+2s"; push = xs; break;
        }
    } else {
        switch (p->character) {
        case CHARACTER_KNUX:
        case CHARACTER_EGGMAN:
        case CHARACTER_AMY:
        case CHARACTER_SALLY: dmg = "-1"; push = xs; break;
        }
    }
    dummy_hit(o, (int)(push * 3), dmg, 20);
}

static void dummy_draw(MapObj *o)
{
    mapobj_draw_self(o);
    const SpriteInfo *s = sprite_info(o->sprite);
    float h = s ? s->height * o->yscale : 0;
    char plain[32];
    ui_strip_codes(DUMMY_TEXT, plain, sizeof plain);
    const float size = 0.45f;
    float w = ui_text_width(size, plain);
    ui_text_coded(o->x - w / 2 - level.cam_x, o->y - h - 10 - level.cam_y - 12, size, UI_WHITE, DUMMY_TEXT);
}

// ------------------------------------------------------------------ obj_darktower_ball

typedef struct {
    float sY, dir, dist;
} Ball;

static void ball_create(MapObj *o)
{
    Ball *b = MAPOBJ_VARS(o, Ball);
    b->sY = o->y;
    b->dir = mapobj_prop(o, "dir", 1);
    b->dist = mapobj_prop(o, "dist", 100);
}

static void ball_step(MapObj *o)
{
    if (level.has_player && mapobj_meets_player(o))
        player_hurt(&level.player, 20, -level.player.image_xscale * 4, -6);
}

// ------------------------------------------------------------------ obj_eggstatue

static void eggstatue_step(MapObj *o) { o->image_speed = 0; }

// ------------------------------------------------------------------ level

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    MapObj *o;
    switch (type) {
    case SERVER_FART_STATE: {
        float x = rd_u16(r), y = rd_u16(r);
        if (r->bad) return true;
        for (int i = 0; (o = mapobj_find(OBJ_FART_DUMMY, i)); i++) mapobj_move(o, x, y);
        return true;
    }
    case SERVER_DTBALL_STATE: {
        if (pass) return true;
        float state = rd_f32(r);
        if (r->bad) return true;
        for (int i = 0; (o = mapobj_find(OBJ_DARKTOWER_BALL, i)); i++) {
            Ball *b = MAPOBJ_VARS(o, Ball);
            mapobj_move(o, o->x, b->sY + state * b->dir * b->dist);
        }
        return true;
    }
    case CLIENT_PLAYER_POTATER: {
        // The puppets' curse (potater / potatoTimer on obj_player_puppet) is game.c's; here only
        // the part for the local player. Read from a copy and leave the packet to game.c.
        NetReader c = *r;
        u8 t = rd_u8(&c);
        u16 pid = rd_u16(&c);
        if (t == 0) {
            u8 timer = rd_u8(&c);
            if (!c.bad && timer >= 4 && pid == net.id) {
                fz.potater = true;
                fz.potatoTimer = 60 * 4;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

static void leave(void) { memset(&fz, 0, sizeof fz); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_FART_CONTROLLER, .create = controller_create, .end_step = controller_end_step,
      .draw = controller_draw },
    { .object = OBJ_FART_ASS, .create = ass_create, .alarm = ass_alarm, .draw = ass_draw, .draw_gui = ass_draw_gui },
    { .object = OBJ_FART_MERMER, .create = mermer_create, .step = mermer_step },
    { .object = OBJ_FART_TEXT, .create = text_create, .step = text_step, .draw = text_draw, .alarm = text_alarm },
    { .object = OBJ_FART_DUMMY, .step = dummy_step, .draw = dummy_draw },
    { .object = OBJ_DARKTOWER_BALL, .create = ball_create, .step = ball_step },
    { .object = OBJ_EGGSTATUE, .step = eggstatue_step },
    { .object = -1 },
};

const MapModule MAP_FARTZONE = {
    .room = ROOM_FARTZONE,
    .objects = OBJECTS,
    .packet = packet,
    .leave = leave,
};

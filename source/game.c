#include "game.h"

#include <math.h>
#include <stdarg.h>

#include "audio.h"
#include "achiev.h"
#include "pets.h"
#include "maps/maps.h"
#include "chars/chars.h"
#include "entities.h"
#include "font.h"
#include "mapobj.h"
#include "gen/anims.h"
#include "gen/objects.h"
#include "gen/rooms.h"
#include "gen/sounds.h"
#include "gen/sprites.h"
#include "level.h"
#include "levels.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "sprite.h"
#include "ui.h"

// PLAYER_* bit flags in CLIENT_PLAYER_DATA (net_packets.gml)
#define PF_EFFECT (1 << 1)
#define PF_HURT (1 << 2)
#define PF_REDRING (1 << 3)
#define PF_ATTACKING (1 << 4)
#define PF_SALLYSHIELD (1 << 5)
#define PF_INVIS (1 << 6)
#define PF_SLIME (1 << 7)

#define MAX_RINGS 128

typedef struct {
    bool used;
    bool seen;             // received at least one data packet
    u16 id;
    char nickname[32];
    s8 character, exe_character;
    float x, y, xspd, yspd;
    int state;
    float angle;           // degrees
    int image_index;
    int xscale;
    int hp, revival, rings;
    bool attacking, hurt, effect, visible, red_ring, slime;
    bool sally_shield;     // PLAYER_SALLYSHIELD
    bool potater;          // Fart Zone curse (CLIENT_PLAYER_POTATER)
    int potato_timer;
    int tails_charge;
    float tail_angle;
    int alarm_inactive;    // alarm[1]: frames until the puppet greys out
    int alarm_extrap;      // alarm[2]: frames of dead reckoning left
    int dead_timer;        // death timer shown above a downed survivor (31 = none)
    bool escaped;
    int pet;               // -1 none, 0..6 (global.pets)
    Pet petf;
    // obj_revival_puppet / obj_heal_progress above the puppet
    int revive_over;       // -1 hidden, 1 we can revive (3+ rings), 0 not enough rings
    bool revive_show;      // someone is reviving it
    float revive_progress;
    bool heal_visible;
    float heal_progress;
} Puppet;

typedef struct {
    bool used;
    u8 iid;
    u16 nid;
    float x, y, alpha;
    bool red, taken;
} Ring;

typedef enum { END_NONE, END_EXE_WINS, END_SURVIVORS_WIN, END_TIME_OVER } Ending;

typedef struct {
    char name[32];
    s8 character;
    u8 type, quit;
    u16 rings, kills, damage;
} ResultRow;

static struct {
    bool active;
    bool server_ready;     // isServerReady (SERVER_GAME_PLAYERS_READY)
    int send_timeout;      // sendTimeout: frames after ready before controls unlock
    u32 frame;
    u16 time_frames;       // SERVER_GAME_TIME_SYNC
    int time_min, time_sec;
    bool time_known;
    Puppet puppets[NET_MAX_PLAYERS];
    Ring rings[MAX_RINGS];
    float ring_frame;      // global.ringFrame
    bool bigring, bigring_ready, escape_sent;
    bool escaped_dead;     // unused guard (kept false)
    bool chase;            // chase music (exit ring spawned)
    float bigring_x, bigring_y, bigring_alpha;
    Ending ending;
    bool escaped;
    int dead_timer;        // local player's death timer (31 = none)
    bool self_revive_show; // our own obj_revival_puppet (others reviving us)
    float self_revive_progress;
    int ping;
    // indicators (obj_exe_indicator, obj_exetior_indicator, obj_surv_indicator, obj_demon_indicator)
    int exe_show_timer;
    int surv_target;       // puppet id revealed by an Eggman tracker, -1 none
    // global.cameraMode: 0 follow, 2 spectate, 3 wait 3 s then spectate
    int cam_mode, cam_timer, spectate;
    // obj_level GUI
    float blood_fade, hide_fade, title_card, card_x, card_y, clock_angle;
    int time_frame;
    int surv_timer;
    bool demon_indicator;
    ResultRow results[NET_MAX_PLAYERS];
    int result_count;
    char status[96];       // last notable event, shown on the HUD
    int status_timer;
    // scr_move_basic chunk check + obj_player_warning (staying in one screen for 20 s)
    float chunk_x, chunk_y;
    int chunk_timer;
    bool warn, warn_moved, warn_terraria;
    int warn_text;
    float warn_fade, warn_timer, music_gain;
} g;

static bool is_exe(void) { return net.exe_id == net.id; }
static void heal_particle(float x, float y);
static void pets_update(void);
static void warning_update(void);

static void set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof g.status, fmt, ap);
    va_end(ap);
    g.status_timer = 60 * 4;
}

static Puppet *puppet(u16 id)
{
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (g.puppets[i].used && g.puppets[i].id == id) return &g.puppets[i];
    return NULL;
}

// Sprite of a puppet (obj_player_puppet Draw_72)
static int puppet_sprite(const Puppet *p)
{
    if (p->character == CHARACTER_EXE) {
        int e = p->exe_character;
        int st = p->state + (p->slime ? 13 + 11 + 1 : 0);  // + CHAOS_AIRTRANSFORM + 1
        if (e < 0 || e > 3 || st >= ANIMS_EXE_COUNT[e]) return -1;
        return ANIMS_EXE[e][st];
    }
    if (p->character < 1 || p->character > 6) return -1;
    if (p->revival >= 2) {
        if (p->state >= ANIMS_DEMON_COUNT[p->character]) return -1;
        return ANIMS_DEMON[p->character][p->state];
    }
    if (p->state >= ANIMS_SURVIVOR_COUNT[p->character]) return -1;
    return ANIMS_SURVIVOR[p->character][p->state];
}

// ------------------------------------------------------------------ bounding boxes

typedef struct { float l, t, r, b; } Box;

static bool sprite_box(int spr, float x, float y, float xscale, Box *out)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s) return false;
    float l = x + (s->bbox_left - s->xorigin) * xscale;
    float r = x + (s->bbox_right + 1 - s->xorigin) * xscale;
    out->l = fminf(l, r);
    out->r = fmaxf(l, r);
    out->t = y + s->bbox_top - s->yorigin;
    out->b = y + s->bbox_bottom + 1 - s->yorigin;
    return true;
}

static bool boxes_meet(const Box *a, const Box *b)
{
    return a->l < b->r && b->l < a->r && a->t < b->b && b->t < a->b;
}

static bool player_box(Box *out)
{
    const Player *p = &level.player;
    return level.has_player && sprite_box(p->sprite, floorf(p->x), floorf(p->y), p->image_xscale, out);
}

// ------------------------------------------------------------------ sending

static void send_player_data(void)
{
    // obj_netclient Step_2
    const Player *p = &level.player;
    NetPacket pk;
    pkt_begin(&pk, CLIENT_PLAYER_DATA);
    pkt_u16(&pk, (u16)(int)p->x);
    pkt_u16(&pk, (u16)(int)p->y);
    pkt_f16(&pk, p->xspd);
    pkt_f16(&pk, p->yspd);
    pkt_u8(&pk, (u8)p->state);
    pkt_u16(&pk, (u16)(s16)roundf(p->angle * 57.29578f));
    pkt_u8(&pk, (u8)(int)p->image_index);
    pkt_u8(&pk, (u8)(s8)(p->image_xscale < 0 ? -1 : 1));
    u8 flags = player_net_flags(p);
    if (!is_exe()) {
        pkt_u8(&pk, (u8)(s8)p->hp);
        pkt_u8(&pk, (u8)p->revivalTimes);
        pkt_u16(&pk, (u16)(s16)p->rings);
        pkt_u8(&pk, flags);
        if (net.my_character == CHARACTER_TAILS) {
            pkt_u8(&pk, (u8)p->attackCharge);
            float a = -atan2f(p->yspd, p->isGrounded ? p->gspd : p->xspd) * 57.29578f;
            pkt_u16(&pk, (u16)(s16)a);
        }
    } else {
        pkt_u8(&pk, flags);
    }
    net_send(&pk, false);
}

static void send_death_state(bool dead, int revival_times)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_PLAYER_DEATH_STATE);
    pkt_u8(&pk, dead);
    pkt_u8(&pk, (u8)revival_times);
    net_send(&pk, true);
}

static void send_stats(u8 type, u8 value)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_STATS_REPORT);
    pkt_u8(&pk, type);
    pkt_u8(&pk, value);
    net_send(&pk, true);
}

// scr_player_instakill (called from player_hurt when hp reaches 0)
static void on_local_death(Player *p)
{
    if (g.ending != END_NONE) return;
    send_death_state(true, p->revivalTimes);
    g.dead_timer = 31;
    g.cam_mode = 3;
    g.cam_timer = 60 * 3;
    set_status("you died");
}

// ------------------------------------------------------------------ packets

static void spawn_ring(u8 iid, u16 nid, float x, float y, bool red)
{
    Ring *slot = NULL;
    for (int i = 0; i < MAX_RINGS; i++)
        if (!g.rings[i].used) { slot = &g.rings[i]; break; }
    if (!slot) return;
    *slot = (Ring){ .used = true, .iid = iid, .nid = nid, .x = x, .y = y, .red = red };
}

static void handle_results_data(NetReader *r)
{
    if (g.result_count >= NET_MAX_PLAYERS) return;
    ResultRow *row = &g.results[g.result_count++];
    snprintf(row->name, sizeof row->name, "%s", rd_str(r));
    row->character = (s8)rd_u8(r);
    rd_u8(r);   // end
    rd_u16(r);  // time
    row->quit = rd_u8(r);
    row->type = rd_u8(r);
    row->rings = rd_u16(r);
    row->kills = rd_u16(r);
    row->damage = rd_u16(r);
}

static void handle_player_data(NetReader *r)
{
    // net_udpprocess CLIENT_PLAYER_DATA
    u16 pid = rd_u16(r);
    Puppet *p = puppet(pid);
    if (!p) {
        dbg_log("game: data for unknown player %u", pid);
        return;
    }
    if (!p->seen) dbg_log("game: first data from %u (character %d/%d)", pid, p->character, p->exe_character);
    p->seen = true;
    p->alarm_inactive = 60;
    p->alarm_extrap = 5;
    float x = rd_u16(r), y = rd_u16(r);
    if (x > 65535 - 4096) x -= 65536;  // net_unwrap_coord
    if (y > 65535 - 4096) y -= 65536;
    p->x = x;
    p->y = y;
    float xs = rd_f16(r), ys = rd_f16(r);
    p->xspd = isfinite(xs) ? fmaxf(-64, fminf(64, xs)) : 0;
    p->yspd = isfinite(ys) ? fmaxf(-64, fminf(64, ys)) : 0;
    p->state = rd_u8(r);
    p->angle = (s16)rd_u16(r);
    p->image_index = rd_u8(r);
    s8 xscale = (s8)rd_u8(r);
    p->xscale = xscale < 0 ? -1 : 1;
    if (p->character == CHARACTER_EXE) {
        u8 flags = rd_u8(r);
        p->attacking = flags & PF_ATTACKING;
        p->hurt = flags & PF_HURT;
        p->effect = flags & PF_EFFECT;
        p->visible = p->exe_character != 0 || !(flags & PF_INVIS);
        p->slime = p->exe_character == EXE_CHAOS && (flags & PF_SLIME);
    } else {
        p->hp = (s8)rd_u8(r);
        p->revival = rd_u8(r);
        p->rings = (s16)rd_u16(r);
        u8 flags = rd_u8(r);
        if (p->character == CHARACTER_TAILS) {
            p->tails_charge = rd_u8(r);
            p->tail_angle = (s16)rd_u16(r);
        }
        p->attacking = (p->character == CHARACTER_KNUX || p->character == CHARACTER_EGGMAN ||
                        p->character == CHARACTER_AMY || p->character == CHARACTER_SALLY) &&
                       (flags & PF_ATTACKING);
        p->hurt = flags & PF_HURT;
        p->red_ring = flags & PF_REDRING;
        p->effect = flags & PF_EFFECT;
        p->sally_shield = p->character == CHARACTER_SALLY && (flags & PF_SALLYSHIELD);
        p->visible = true;
    }
}

static void handle_game(PacketType type, bool pass, NetReader *r, bool reliable)
{
    if (!g.active && type != SERVER_RESULTS && type != SERVER_RESULTS_DATA) return;
    Player *me = &level.player;
    if (ents_packet(type, pass, r, reliable)) return;
    if (mapobj_packet(type, pass, r, reliable)) return;
    switch (type) {
    case CLIENT_PLAYER_DATA:
        handle_player_data(r);
        break;
    case CLIENT_PLAYER_POTATER: {
        if (pass) break;
        u8 t = rd_u8(r);
        u16 pid = rd_u16(r);
        u8 timer = t == 0 ? rd_u8(r) : 0;
        Puppet *p = puppet(pid);
        if (p && !r->bad) {
            p->potater = t == 0;
            p->potato_timer = timer;
        }
        break;
    }
    case SERVER_PONG:
        g.ping = rd_u16(r);
        break;
    case SERVER_PLAYER_LEFT: {
        u16 id = rd_u16(r);
        Puppet *p = puppet(id);
        if (p) {
            set_status("%s left", p->nickname);
            p->used = false;
        }
        break;
    }
    case SERVER_GAME_PLAYERS_READY:
        if (pass) break;
        g.server_ready = true;
        dbg_log("game: players ready");
        break;
    case SERVER_GAME_TIME_SYNC: {
        if (pass) break;
        u16 t = rd_u16(r);
        g.time_frames = t;
        g.time_min = (int)ceilf((t + 1) / 3600.0f) - 1;
        g.time_sec = (int)ceilf(t / 60.0f) % 60;
        g.time_known = true;
        player_short_hurt = g.time_min <= 0 && g.time_sec < 60;
        player_time_min = g.time_min;
        player_time_sec = g.time_sec;
        g.time_frame++;
        break;
    }
    case SERVER_RING_STATE: {
        if (pass) break;
        u8 st = rd_u8(r);
        if (st == 0) {
            u8 id = rd_u8(r);
            u16 uid = rd_u16(r);
            bool red = rd_u8(r);
            RoomInstance *sp = room_instance_find(&level.room, OBJ_RING_SPAWNER, id);
            if (sp) spawn_ring(id, uid, sp->x, sp->y, red);
        } else if (st == 1) {
            rd_u8(r);
            u16 uid = rd_u16(r);
            for (int i = 0; i < MAX_RINGS; i++)
                if (g.rings[i].used && g.rings[i].nid == uid) { g.rings[i].used = false; break; }
        } else if (st == 2) {
            float x = (s16)rd_u16(r), y = (s16)rd_u16(r);
            u8 id = rd_u8(r);
            u16 uid = rd_u16(r);
            bool red = rd_u8(r);
            spawn_ring(id, uid, x, y, red);
        }
        break;
    }
    case SERVER_RING_COLLECTED: {
        if (pass || !level.has_player || is_exe() || me->revivalTimes >= 2) break;
        rd_u8(r);
        rd_u16(r);
        bool red = rd_u8(r);
        bool can_heal = rd_u8(r);
        if (red) {
            if (me->redRingTimer > 0) break;
            player_sound(me, SND_REDRING);
            me->redRingTimer = 60 * 10;  // TODO(M8): mus_mindfuck + red ring screen
            break;
        }
        me->rings++;
        if (me->redRingTimer >= 60) {
            int cnt = (g.time_min <= 0 && g.time_sec < 60) ? 120 : 30;
            me->redRingTimer = me->redRingTimer - cnt > 60 ? me->redRingTimer - cnt : 60;
        }
        if (me->rings >= 10 && me->hp < 100 && can_heal) {
            me->rings = 0;
            me->hp += 20;
            player_sound(me, SND_HEAL);
            send_stats(0, 0);
            achiev_heal_restore();
        } else {
            player_sound(me, SND_RING);
        }
        break;
    }
    case SERVER_GAME_SPAWN_RING: {
        if (pass || g.ending != END_NONE) break;
        bool ready = rd_u8(r);
        if (!ready) {
            if (g.bigring) { g.bigring_ready = false; break; }
            u8 ind = rd_u8(r);
            int n = room_instance_number(&level.room, OBJ_RINGSPAWN);
            RoomInstance *sp = n ? room_instance_find(&level.room, OBJ_RINGSPAWN, ind % n) : NULL;
            if (!sp) break;
            g.bigring = true;
            g.bigring_ready = false;
            g.bigring_x = sp->x;
            g.bigring_y = sp->y;
            g.bigring_alpha = 0;
            g.chase = true;
            set_status("the exit ring appeared!");
        } else if (g.bigring) {
            g.bigring_ready = true;
            set_status("the exit ring is open!");
        }
        break;
    }
    case SERVER_PLAYER_ESCAPED:
        if (level.has_player) achiev_escaped(me->hp);
        g.escaped = true;
        g.cam_mode = 3;
        g.cam_timer = 60 * 3;
        level.has_player = false;
        set_status("you escaped!");
        break;
    case SERVER_GAME_PLAYER_ESCAPED: {
        Puppet *p = puppet(rd_u16(r));
        achiev_round.rPlayersEscaped++;
        if (p) {
            audio_play(SND_TELEPORT);
            p->escaped = true;
            set_status("%s escaped", p->nickname);
        }
        break;
    }
    case SERVER_GAME_DEATHTIMER_TICK: {
        rd_u8(r);  // exe near
        u16 id = rd_u16(r);
        u8 t = rd_u8(r);
        if (id == net.id) g.dead_timer = t;
        else if (puppet(id)) puppet(id)->dead_timer = t;
        break;
    }
    case SERVER_GAME_DEATHTIMER_END: {
        if (pass || !level.has_player) break;
        bool demonize = rd_u8(r);
        if (!demonize) {
            g.dead_timer = 31;
            me->revivalTimes = 1;
            break;
        }
        // Demonized: back in the game as a demon (demon abilities: M5)
        player_demonize(me);
        achiev_round.demonLast = true;
        g.cam_mode = 0;
        g.dead_timer = 31;
        set_status("you were demonized!");
        break;
    }
    case SERVER_PLAYER_DEATH_STATE: {
        if (pass) break;
        u16 id = rd_u16(r);
        bool dead = rd_u8(r);
        u8 rev = rd_u8(r);
        if (!dead) break;
        if (id == net.id && level.has_player) me->revivalTimes = rev;
        else if (puppet(id)) set_status("%s is down", puppet(id)->nickname);
        break;
    }
    case SERVER_FORCE_DAMAGE:
        break;
    case SERVER_REVIVAL_PROGRESS: {
        if (pass) break;
        u16 pid = rd_u16(r);
        float prog = (float)rd_f64(r);
        if (pid == net.id) g.self_revive_progress = prog;
        else if (puppet(pid)) puppet(pid)->revive_progress = prog;
        break;
    }
    case SERVER_REVIVAL_STATUS: {
        if (pass) break;
        bool status = rd_u8(r);
        u16 pid = rd_u16(r);
        if (pid == net.id) g.self_revive_show = status;
        else if (puppet(pid)) puppet(pid)->revive_show = status;
        break;
    }
    case SERVER_REVIVAL_RINGSUB:
        if (pass || !level.has_player) break;
        me->rings = 0;
        achiev_revival_ringsub();
        break;
    case SERVER_REVIVAL_REVIVED:
        if (pass || !level.has_player || me->revivalTimes >= 2) break;
        send_death_state(false, me->revivalTimes);
        me->hp = 40;
        me->isDead = false;
        me->revivalTimes++;
        me->hurttime = 60 * 4;
        g.cam_mode = 0;
        g.self_revive_show = false;
        g.dead_timer = 31;
        set_status("you were revived!");
        break;
    case CLIENT_PLAYER_HEAL: {
        u16 pid = rd_u16(r);
        if (pid != net.id || !level.has_player || me->hp >= 100) break;
        player_sound(me, SND_HEAL);
        me->hp += 20;
        break;
    }
    case CLIENT_PLAYER_HEAL_PART: {
        float x = rd_u16(r), y = rd_u16(r);
        heal_particle(x, y);
        break;
    }
    case SERVER_PLAYER_BACKTRACK:
        if (pass || !level.has_player) break;
        me->x = rd_f32(r);
        me->y = rd_f32(r);
        break;
    case SERVER_GAME_EXE_WINS:
    case SERVER_GAME_SURVIVOR_WIN:
    case SERVER_GAME_TIME_OVER:
        if (pass) break;
        g.ending = type == SERVER_GAME_EXE_WINS ? END_EXE_WINS
                 : type == SERVER_GAME_SURVIVOR_WIN ? END_SURVIVORS_WIN : END_TIME_OVER;
        {
            bool counted = rd_u8(r);
            if (is_exe() && g.ending != END_SURVIVORS_WIN) {
                achiev_exe_end(counted, net.my_exe_character, net_player_count() + 1);
            } else if (g.ending == END_SURVIVORS_WIN && !is_exe() && level.has_player && !g.escaped_dead) {
                const Puppet *exe = puppet(net.exe_id);
                if (me->revivalTimes < 2 && me->hp > 0)
                    achiev_survivor_win(counted, me->character, exe ? exe->exe_character : 0);
            } else if (g.ending == END_SURVIVORS_WIN && g.escaped) {
                const Puppet *exe = puppet(net.exe_id);
                achiev_survivor_win(counted, net.my_character, exe ? exe->exe_character : 0);
            }
        }
        if (is_exe() && level.has_player) {
            if (g.ending == END_SURVIVORS_WIN) me->lost = true;
            else me->won = true;
        }
        audio_play(g.ending == END_SURVIVORS_WIN ? SND_SURVIVOR_WIN : SND_EXE_WINS);
        dbg_log("game: ending %d", g.ending);
        break;
    case SERVER_RESULTS: {
        net.map = rd_u8(r);
        net.state = NET_RESULTS;
        g.result_count = 0;
        NetPacket pk;
        pkt_begin(&pk, CLIENT_RESULTS_REQUEST);
        net_send(&pk, true);
        dbg_log("game: results");
        break;
    }
    case SERVER_RESULTS_DATA:
        handle_results_data(r);
        break;
    case CLIENT_MERCOIN_BONUS: {
        u16 tid = rd_u16(r);
        u8 kind = rd_u8(r);
        if (tid == net.id) achiev_mercoin_bonus(kind, r);
        break;
    }
    case CLIENT_SOUND_EMIT: {
        // net_state_game: the sender's sound at its puppet, fading with distance
        u16 pid = rd_u16(r);
        u8 sid = rd_u8(r);
        bool should_play = rd_u8(r);
        if (sid >= NET_SNDS_COUNT || NET_SNDS[sid] < 0) break;
        if (should_play && pid == net.id) {
            audio_play(NET_SNDS[sid]);
            break;
        }
        Puppet *p = puppet(pid);
        if (!p || !p->seen) break;
        float dx = p->x - (level.cam_x + TOP_W / 2), dy = p->y - (level.cam_y + TOP_H / 2);
        float d = sqrtf(dx * dx + dy * dy);
        float gain = 1.0f - fminf(d, 700) / 700;  // obj_player_puppet emitter gain
        if (d > 250) gain *= fmaxf(0, 1 - (d - 250) / 250);  // falloff 250..500
        if (gain > 0.02f) audio_play_ex(NET_SNDS[sid], gain, false);
        break;
    }
    case CLIENT_CHAT_MESSAGE: {
        u16 id = rd_u16(r);
        const char *msg = rd_str(r);
        NetPlayer *np = net_player(id);
        set_status("%s: %s", id == 0 ? "(server)" : np ? np->nickname : "?", msg);
        break;
    }
    default:
        break;
    }
    (void)reliable;
}

// ------------------------------------------------------------------ lifecycle

// obj_camera modes 2/3: after death or escape, watch the living survivors (any key: next one)
static bool spectatable(const Puppet *p)
{
    return p->used && p->seen && !p->escaped && p->id != net.exe_id && p->revival < 2 && p->state != ST_DEAD &&
           p->hp > 0;
}

static void spectate_camera(u32 down)
{
    if (g.cam_mode == 3 && --g.cam_timer <= 0) {
        g.cam_mode = 2;
        g.spectate = -1;
    }
    if (g.cam_mode != 2) return;
    if (g.spectate < 0 || !spectatable(&g.puppets[g.spectate]) || (down & (KEY_A | KEY_B | KEY_L | KEY_R))) {
        int start = g.spectate;
        g.spectate = -1;
        for (int k = 1; k <= NET_MAX_PLAYERS; k++) {
            int i = ((start < 0 ? -1 : start) + k) % NET_MAX_PLAYERS;
            if (spectatable(&g.puppets[i])) { g.spectate = i; break; }
        }
    }
    if (g.spectate < 0) return;
    const Puppet *t = &g.puppets[g.spectate];
    float tx = floorf(t->x) - TOP_W / 2, ty = floorf(t->y) - TOP_H / 2;
    level.cam_x = fabsf(tx - level.cam_x) > 10 ? level.cam_x + (tx - level.cam_x) * 0.5f : tx;
    level.cam_y = fabsf(ty - level.cam_y) > 10 ? level.cam_y + (ty - level.cam_y) * 0.5f : ty;
    float max_x = (float)level.room.width - TOP_W, max_y = (float)level.room.height - TOP_H;
    level.cam_x = floorf(fmaxf(0, fminf(level.cam_x, max_x)));
    level.cam_y = floorf(fmaxf(0, fminf(level.cam_y, max_y)));
}

static void tracker_reveal(u16 target)
{
    g.surv_target = target;
    g.surv_timer = 60 * 3;
}

void game_init(void)
{
    net_set_game_handler(handle_game);
    ents_on_tracker_reveal = tracker_reveal;
}

bool game_active(void) { return g.active; }

void game_begin(void)
{
    memset(&g, 0, sizeof g);
    ents_clear();
    g.active = true;
    player_controls_lock = player_swap_dirs = false;
    g.send_timeout = 30;  // sendTimeout = 60 * .5 on room start
    g.dead_timer = 31;
    g.surv_target = -1;
    g.card_x = 480;
    g.card_y = -270;
    g.chunk_x = g.chunk_y = -1000;
    g.music_gain = 1;
    int map = net.map >= 0 && net.map < LEVEL_COUNT ? net.map : 12;
    level_load(LEVELS[map].room, -1);
    achiev_round_start(LEVELS[map].room);

    // obj_spawnpoint / obj_exespawn: a random spawn, 18 px above it
    int spawn_obj = is_exe() ? OBJ_EXESPAWN : OBJ_SPAWNPOINT;
    int n = room_instance_number(&level.room, spawn_obj);
    RoomInstance *sp = n ? room_instance_find(&level.room, spawn_obj, rand() % n) : NULL;
    if (sp) level_spawn(is_exe() ? CHARACTER_EXE : net.my_character, net.my_exe_character, sp->x, sp->y - 18);
    level.player.hp = 100;

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        const NetPlayer *np = &net.players[i];
        if (!np->used || np->id == net.id || np->character < 0) continue;
        Puppet *p = &g.puppets[i];
        memset(p, 0, sizeof *p);
        p->used = true;
        p->id = np->id;
        p->character = np->character;
        p->exe_character = np->exe_character;
        p->hp = 100;
        p->xscale = 1;
        p->visible = true;
        p->dead_timer = 31;
        p->revive_over = -1;
        p->pet = np->pet >= 0 && np->pet < 7 ? np->pet : -1;
        snprintf(p->nickname, sizeof p->nickname, "%s", np->nickname);
    }
    player_on_death = on_local_death;
    level_draw_objects = NULL;
    // net_state_charselect SERVER_LOBBY_GAME_START: the EXE laughs
    int exe_char = is_exe() ? net.my_exe_character : (net_player(net.exe_id) ? net_player(net.exe_id)->exe_character : 0);
    static const int laughs[4] = { SND_EXE_LAUGH, SND_CHAOS_LAUGH, SND_EXETIOR_LAUGH, SND_EXELLER_LAUGH };
    audio_play(laughs[exe_char & 3]);
    dbg_log("game: map %d (%s), %s, spawn %s", map, LEVELS[map].name, is_exe() ? "exe" : "survivor",
            sp ? "ok" : "missing");
}

bool game_puppet_of(int character, u16 *id, bool *demon)
{
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        const Puppet *p = &g.puppets[i];
        if (!p->used || p->character != character) continue;
        *id = p->id;
        *demon = p->character == CHARACTER_EXE || p->revival >= 2;
        return true;
    }
    return false;
}

int game_exe_state(void)
{
    const Puppet *p = puppet(net.exe_id);
    return p ? p->state : -1;
}

float game_ring_frame(void) { return g.ring_frame; }

void game_exe_indicator_show(int frames) { g.exe_show_timer = frames; }

bool game_puppet_status(int character, int *hp, int *revival)
{
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        const Puppet *p = &g.puppets[i];
        if (!p->used || p->character != character) continue;
        *hp = p->hp;
        *revival = p->revival;
        return true;
    }
    return false;
}

// point_direction in GameMaker degrees (0 right, 90 up)
static float point_dir(float x0, float y0, float x1, float y1)
{
    return atan2f(-(y1 - y0), x1 - x0) * 180.0f / (float)M_PI;
}

// The EXE's arrow: nearest living survivor, or a survivor holding a red ring.
static bool exe_target_angle(float *angle, bool *red)
{
    const Player *me = &level.player;
    float best = 1e30f;
    const Puppet *target = NULL;
    *red = false;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        const Puppet *p = &g.puppets[i];
        if (!p->used || !p->seen || p->escaped || p->hp <= 0 || p->revival >= 2 || p->character == CHARACTER_EXE)
            continue;
        if (p->red_ring && me->invisTimer <= 0) {
            target = p;
            *red = true;
            break;
        }
        float d = (p->x - me->x) * (p->x - me->x) + (p->y - me->y) * (p->y - me->y);
        if (d < best) {
            best = d;
            target = p;
        }
    }
    if (!target) return false;
    *angle = point_dir(me->x, me->y, target->x, target->y);
    return true;
}

static void draw_indicators(void)
{
    if (!level.has_player) return;
    const Player *me = &level.player;
    float px = ceilf(me->x) - level.cam_x, py = ceilf(me->y) - level.cam_y;
    if (is_exe()) {
        float angle;
        bool red;
        bool have = exe_target_angle(&angle, &red);
        bool show;
        switch (me->exe_character) {
        case EXE_ORIGINAL: show = me->invisTimer > 0; break;
        case EXE_CHAOS: show = player_net_flags(me) & NETF_SLIME; break;
        default: show = g.exe_show_timer > 0; break;  // Exetior / Exeller
        }
        if (have && (show || red)) sprite_draw(SPR_INDICATOR, 0, px, py, 1, 1, angle, 0xFFFFFFFF, 1);
        return;
    }
    if (me->revivalTimes >= 2) {
        // obj_demon_indicator: a demon sees where the EXE is
        const Puppet *exe = puppet(net.exe_id);
        if (exe && exe->seen) {
            int pos = me->character == CHARACTER_TAILS || me->character == CHARACTER_CREAM ? 20
                    : me->character == CHARACTER_EGGMAN ? 36 : 25;
            sprite_draw(SPR_INDICATOR2, 0, px, py - pos - 9, 1, 1, point_dir(me->x, me->y, exe->x, exe->y), 0xFFFFFFFF, 1);
        }
    }
    if (g.surv_timer > 0 && !me->isDead) {
        const Puppet *t = puppet((u16)g.surv_target);
        if (t && t->seen) sprite_draw(SPR_SINDICATOR, 0, px, py, 1, 1, point_dir(me->x, me->y, t->x, t->y), 0xFFFFFFFF, 1);
    }
}

bool game_nearest_puppet(float x, float y, float max_dist, float *px, float *py)
{
    if (!g.active) return false;
    bool found = false;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        const Puppet *p = &g.puppets[i];
        if (!p->used || !p->seen || p->escaped || !p->visible) continue;
        float d = sqrtf((p->x - x) * (p->x - x) + (p->y - y) * (p->y - y));
        if (d < max_dist) {
            max_dist = d;
            *px = p->x;
            *py = p->y;
            found = true;
        }
    }
    return found;
}

int game_music(void)
{
    if (net.state == NET_RESULTS || g.ending != END_NONE) {
        if (net.state != NET_RESULTS) return -2;  // keep what is playing until the results
        return g.ending == END_EXE_WINS ? SND_MUS_EXEWIN : g.ending == END_TIME_OVER ? SND_MUS_TIMEOVER : SND_MUS_SURVWIN;
    }
    if (net.map < 0 || net.map >= LEVEL_COUNT) return -1;
    return g.chase ? LEVELS[net.map].chase : maps_music(level.room_id, LEVELS[net.map].music);
}

void game_end(void)
{
    g.active = false;
    audio_stop_sound(SND_BOS);
    audio_music_gain(1);
    player_controls = true;
    player_time_min = 9;
    player_time_sec = 0;
    player_on_death = NULL;
    player_short_hurt = false;
    level_draw_objects = NULL;
}

// ------------------------------------------------------------------ update

static bool puppet_demon(const Puppet *p) { return p->character == CHARACTER_EXE || p->revival >= 2; }

// place_meeting between a puppet and the local player (both use their sprite bbox).
static bool puppet_touches_player(const Puppet *p)
{
    int spr = puppet_sprite(p);
    Box a, b;
    if (spr < 0 || !sprite_box(spr, floorf(p->x), floorf(p->y), p->xscale, &a) || !player_box(&b)) return false;
    return boxes_meet(&a, &b);
}

static void send_player_hurt(const Puppet *p, u16 stun)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_PLAYER_HURT);
    pk.buf[0] = 1;
    pkt_u16(&pk, p->id);
    pkt_u16(&pk, (u16)(int)p->x);
    pkt_u16(&pk, (u16)(int)p->y);
    pkt_u16(&pk, stun);
    net_send(&pk, true);
}

static void send_mercoin(u16 master_id, u8 kind)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_MERCOIN_BONUS);
    pk.buf[0] = 1;
    pkt_u16(&pk, master_id);
    pkt_u8(&pk, kind);
    net_send(&pk, true);
}

// scr_exe_checkwin: the EXE killed the local survivor
static void exe_checkwin(const Puppet *exe)
{
    if (level.player.hp > 0) return;
    send_mercoin(exe->id, 0);  // TODO(M7): the EXE's kill line
}

// obj_heal_particle: rises with a wobble and fades
static void heal_particle(float x, float y)
{
    const ObjectInfo *o = object_info(OBJ_HEAL_PARTICLE);
    effect_quick(x, y, o ? o->sprite : -1, 0.016f * (1 << (rand() % 3)), true, 0, -1 - (rand() % 100) / 100.0f, 1);
}

// scr_survivor_revive + scr_survivor_heal (obj_player_puppet Step_2)
static void puppet_revive_heal(Puppet *p)
{
    Player *me = &level.player;
    bool demon = p->character == CHARACTER_EXE || p->revival >= 2;
    // revive
    if (p->hp > 0 || p->revival != 0 || demon) {
        p->revive_over = -1;
        p->revive_show = false;
    } else if (!level.has_player || is_exe() || me->revivalTimes >= 2 || me->hp <= 0) {
        p->revive_over = -1;
    } else if (puppet_touches_player(p)) {
        p->revive_over = me->rings >= 3 ? 1 : 0;
        if (player_controls && me->isLookingDown && me->rings >= 3) {
            NetPacket pk;
            pkt_begin(&pk, CLIENT_REVIVAL_PROGRESS);
            pkt_u16(&pk, p->id);
            pkt_u8(&pk, (u8)(me->rings - 3));
            net_send(&pk, true);
        }
    } else {
        p->revive_over = -1;
    }

    // heal
    if (p->hp <= 0 || p->hp >= 100 || p->revival >= 2 || demon) {
        p->heal_progress = 0;
        return;
    }
    if (!level.has_player || me->rings < 10) {
        p->heal_visible = false;
        p->heal_progress = 0;
        return;
    }
    if (is_exe() || me->revivalTimes >= 2 || me->hp <= 0) return;
    if (!puppet_touches_player(p)) {
        p->heal_visible = false;
        p->heal_progress = 0;
        return;
    }
    p->heal_visible = true;
    if (player_controls && me->isLookingUp) {
        if ((int)floorf(p->heal_progress * 100) % 32 == 0) {
            NetPacket pk;
            pkt_begin(&pk, CLIENT_PLAYER_HEAL_PART);
            pk.buf[0] = 1;
            pkt_u16(&pk, (u16)(int)p->x);
            pkt_u16(&pk, (u16)(int)p->y);
            pkt_u16(&pk, (u16)me->rings);
            net_send(&pk, true);
            heal_particle(p->x, p->y);
        }
        p->heal_progress += 0.016f;
        if (p->heal_progress >= 1) {
            NetPacket pk;
            pkt_begin(&pk, CLIENT_PLAYER_HEAL);
            pk.buf[0] = 1;
            pkt_u16(&pk, p->id);
            pkt_u16(&pk, (u16)me->rings);
            net_send(&pk, true);
            p->heal_progress = 0;
            me->rings -= 10;
            achiev_healed_player(p->id, me->character);
        }
    } else if (p->heal_progress > 0) {
        p->heal_progress = 0;
    }
}

// obj_player_puppet Step_0: an attacking puppet touching the local player.
//  - demon/EXE attacking a survivor: damage (scr_player_hurt)
//  - survivor attacking the EXE or a demon: stun
static void puppet_contact(Puppet *p)
{
    Player *me = &level.player;
    if (!level.has_player || !p->seen || p->escaped) return;
    bool attacking = p->attacking;
    if (p->character == CHARACTER_AMY && p->state == 15 /* AMY_HJUMP */ && p->image_index < 3) attacking = true;
    if (!attacking || !puppet_touches_player(p)) return;
    bool global_exe = is_exe() || me->revivalTimes >= 2;
    int mc = me->character;

    if (puppet_demon(p) && p->visible) {
        if (is_exe()) return;
        if (p->character != CHARACTER_AMY && p->character != CHARACTER_EGGMAN &&
            (mc == CHARACTER_KNUX || mc == CHARACTER_SALLY) && me->isAttacking && me->revivalTimes < 2) {
            // clash: both attacking
            me->isAttacking = false;
            me->isHurt = true;
            me->hurttime = 60;
            me->xspd = sgnf(me->x - p->x) * 3;
            me->yspd = -2;
            me->isGrounded = false;
            player_sound(me, SND_BUBLE);
            return;
        }
        if (me->hurttime > 0 || me->hp <= 0 || me->revivalTimes >= 2) return;
        if (p->character != CHARACTER_AMY && mc == CHARACTER_EGGMAN && me->isAttacking) return;
        if (mc == CHARACTER_AMY && me->isAttacking) return;

        int damage = 20;
        if (p->character == CHARACTER_EXE && p->exe_character != EXE_CHAOS) damage = 40;
        if (p->character == CHARACTER_EGGMAN && me->hurttime <= 0) player_slow(me, 3);
        int hp_before = me->hp;
        player_hurt(me, damage, p->xscale * 3, -3);  // Sally's shield is handled by hurt_blocked
        if (me->hp == hp_before && !me->isDead) return;
        if (p->character == CHARACTER_EXE) exe_checkwin(p);
        send_player_hurt(p, 0);
        NetPacket pk;
        pkt_begin(&pk, CLIENT_STATS_REPORT);
        pkt_u8(&pk, 2);
        pkt_u16(&pk, p->id);
        pkt_u16(&pk, damage);
        pkt_u16(&pk, (u16)me->hp);
        net_send(&pk, true);
        g.blood_fade = 1;
        dbg_log("game: hurt by %u (%d -> %d)", p->id, hp_before, me->hp);
        return;
    }

    if (!puppet_demon(p) && global_exe) {
        if (is_exe() && (player_net_flags(me) & NETF_INVIS)) return;
        if (mc != CHARACTER_AMY && mc != CHARACTER_EGGMAN && me->isAttacking &&
            (p->character == CHARACTER_KNUX || p->character == CHARACTER_SALLY)) {
            me->isAttacking = false;
            me->isHurt = true;
            me->hurttime = 60;
            me->xspd = sgnf(me->x - p->x) * 2;
            me->yspd = -2;
            me->isGrounded = false;
            player_sound(me, SND_BUBLE);
            return;
        }
        if (me->shockedTimer > 0 || me->hurttime > 0) return;
        if (mc == CHARACTER_SALLY && player_hurt_blocked(me)) return;  // demon Sally's shield
        if (mc == CHARACTER_AMY && me->isAttacking) return;
        int stun = 3;
        float xxspd = -sgnf(p->x - me->x) * 4;
        if (p->character == CHARACTER_EGGMAN) {
            stun = 2;
            xxspd = 0;
        }
        send_player_hurt(p, stun);
        me->shockedTimer = stun * 60;
        me->xspd = xxspd;
        me->yspd = -2;
        me->isGrounded = false;
        NetPacket pk;
        pkt_begin(&pk, CLIENT_STATS_REPORT);
        pkt_u8(&pk, 1);
        pkt_u16(&pk, net.id);
        pkt_u16(&pk, p->id);
        pkt_u8(&pk, stun);
        net_send(&pk, true);
        player_sound(me, p->character == CHARACTER_EGGMAN ? SND_ELECTROSHOCK : SND_EXE_STUN);
        dbg_log("game: stunned by %u", p->id);
    }
}

// The players' Step_0 after collision: jump contacts with puppets.
//  - survivor: a jumping demon lands on us (hurt 20, or a bounce if we jump too)
//  - EXE: bounce off jumping survivors when we jump too
static void local_contacts(void)
{
    Player *me = &level.player;
    if (!level.has_player) return;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Puppet *p = &g.puppets[i];
        if (!p->used || !p->seen || p->escaped || !puppet_touches_player(p)) continue;
        bool p_jump = p->state == ST_JUMP || p->state == ST_SPIN;
        bool me_jump = me->isJumping || me->isSpinning;
        bool no_bounce = p->character == CHARACTER_EGGMAN || p->character == CHARACTER_AMY ||
                         p->character == CHARACTER_SALLY;
        if (is_exe()) {
            if ((player_net_flags(me) & (NETF_INVIS | NETF_SLIME)) || me->bounceTimer > 0) return;
            if (puppet_demon(p) || p->alarm_inactive <= 0 || p->hurt) continue;
            if (p_jump && me_jump && !no_bounce) {
                player_hurt_ex(me, 0, -sgnf(p->x - me->x) * 3, -sgnf(p->y - me->y) * 4, true);
                me->bounceTimer = 6;
                player_sound(me, SND_BUBLE);
            }
            continue;
        }
        // Survivor Step_0 variants: Tails/Knuckles/Cream bounce when jumping themselves,
        // Amy and Eggman always take 20, Sally's shield turns the hit into a knockback.
        int mc = me->character;
        bool bouncer = mc == CHARACTER_TAILS || mc == CHARACTER_KNUX || mc == CHARACTER_CREAM;
        if (me->hurttime > 0 || (me->hp <= 0 && me->revivalTimes < 2)) return;
        if (bouncer && me->bounceTimer > 0) return;
        if (mc == CHARACTER_AMY && (me->isAttacking || (me->state == 15 /* isHJ */ && me->image_index <= 3))) return;
        if (mc == CHARACTER_SALLY && me->isAttacking && me->image_index <= 2) return;
        if (!puppet_demon(p) || !p->visible || !p_jump) continue;
        if (mc != CHARACTER_TAILS && mc != CHARACTER_AMY && me->isAttacking) continue;
        if (no_bounce || me->revivalTimes >= 2 || p->slime) continue;
        int dmg = bouncer && me_jump ? 0 : 20;
        float dir_x = -sgnf(p->x - me->x) * 3;
        float dir_y = dmg == 0 ? -sgnf(p->y - me->y) * 4 : -4;
        bool shield = mc == CHARACTER_SALLY && (player_net_flags(me) & NETF_SALLYSHIELD);
        player_hurt_ex(me, dmg, dir_x, dir_y, bouncer);
        if (shield) {
            me->isHurt = true;
            me->isJumping = me->isGrounded = me->isSpinning = me->isLookingDown = me->isLookingUp = false;
            me->xspd = dir_x;
            me->yspd = -4;
        }
        if (me->hp <= 0 && p->revival >= 2) send_mercoin(p->id, 4);
        if (dmg > 0 && (!bouncer || me->hurttime >= 0)) {
            g.blood_fade = 0.4f;
            if (p->character == CHARACTER_EXE) exe_checkwin(p);
            NetPacket pk;
            pkt_begin(&pk, CLIENT_STATS_REPORT);
            pkt_u8(&pk, 2);
            pkt_u16(&pk, p->id);
            pkt_u16(&pk, 20);
            pkt_u16(&pk, (u16)me->hp);
            net_send(&pk, true);
        }
        if (dmg <= 0 && me->bounceTimer <= -1) me->bounceTimer = 6;
    }
}

static void update_rings(void)
{
    const Player *me = &level.player;
    g.ring_frame += 0.1f;
    if (g.ring_frame > 4) g.ring_frame = 0;
    Box pb;
    bool have_box = player_box(&pb);
    for (int i = 0; i < MAX_RINGS; i++) {
        Ring *ring = &g.rings[i];
        if (!ring->used) continue;
        if (ring->alpha < 1) ring->alpha += 1 / 30.0f;
        if (ring->taken || ring->alpha < 1 || !have_box) continue;
        // obj_ring Step: survivors collect; EXEs and demons cannot (ring breaking: M5)
        if (is_exe() || me->revivalTimes >= 2 || me->hp <= 0) continue;
        Box rb;
        if (!sprite_box(ring->red ? SPR_REDRING : SPR_RING, ring->x, ring->y, 1, &rb) || !boxes_meet(&rb, &pb))
            continue;
        if (ring->red && me->redRingTimer > 0) continue;
        NetPacket pk;
        pkt_begin(&pk, CLIENT_RING_COLLECTED);
        pkt_u8(&pk, ring->iid);
        pkt_u16(&pk, ring->nid);
        net_send(&pk, true);
        ring->taken = true;
    }

    if (g.bigring && g.bigring_alpha < 1) g.bigring_alpha += 0.01f;
    // obj_bigring Step
    if (g.bigring && g.bigring_ready && have_box && !g.escape_sent && !is_exe() && !me->isDead &&
        me->revivalTimes < 2 && me->redRingTimer <= 0) {
        Box bb;
        if (sprite_box(SPR_BIGRING_READY, g.bigring_x, g.bigring_y, 1, &bb) && boxes_meet(&bb, &pb)) {
            NetPacket pk;
            pkt_begin(&pk, CLIENT_PLAYER_ESCAPED);
            net_send(&pk, true);
            g.escape_sent = true;
        }
    }
}

void game_update(u32 held, u32 down)
{
    Player *me = &level.player;
    g.frame++;
    if (g.status_timer > 0) g.status_timer--;
    if (g.exe_show_timer > 0) g.exe_show_timer--;
    if (g.surv_timer > 0) g.surv_timer--;

    // global.playerControls: locked until everyone loaded, then half a second more
    bool controls = g.server_ready && g.ending == END_NONE;
    if (g.server_ready && g.send_timeout > 0) {
        g.send_timeout--;
        controls = false;
    }
    if (g.server_ready && g.send_timeout <= 0 && g.frame % 60 == 0) {
        NetPacket pk;
        pkt_begin(&pk, CLIENT_PING);
        net_send(&pk, false);
    }
    if (down & KEY_X) {
        char msg[64];
        if (ui_keyboard("Chat message", NULL, msg, sizeof msg, 40)) net_send_chat(msg);
    }
    player_controls = controls && !player_controls_lock;

    Keys k = level_keys(held, down);
    level_update(&k, held);

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Puppet *p = &g.puppets[i];
        if (!p->used) continue;
        if (p->alarm_extrap > 0) {
            p->x += p->xspd;
            p->y += p->yspd;
            p->alarm_extrap--;
        }
        if (p->alarm_inactive > 0) p->alarm_inactive--;
        puppet_contact(p);
        puppet_revive_heal(p);
        if (dbg_flag('x') && g.frame % 60 == 0)
            dbg_log("game: puppet %u at %.0f,%.0f state %d atk %d spr %d | me %.0f,%.0f hp %d", p->id, p->x, p->y,
                    p->state, p->attacking, puppet_sprite(p), me->x, me->y, me->hp);
    }
    local_contacts();
    update_rings();
    pets_update();
    warning_update();
    if (level.has_player) achiev_rings(me->rings);
    ents_step();
    spectate_camera(down);

    // Player data is sent from the first frame in the level: the server marks us loaded
    // (plr.ready) on the first message and starts the match when everyone is.
    if (level.has_player && !g.escaped) send_player_data();
}

// ------------------------------------------------------------------ drawing

static void draw_puppet_tag(const Puppet *p, float x, float y)
{
    u32 name_col = p->character == CHARACTER_EXE || p->revival >= 2 ? UI_RED : UI_WHITE;
    ui_text_center(x, y - 44, 0.4f, name_col, "%.12s", p->nickname);
}

static void draw_puppet(const Puppet *p)
{
    if (!p->seen || p->escaped || !p->visible) return;
    if (level.room_id == ROOM_MAJONGFOREST) {
        // obj_majong_controller Draw_64: the tag again where the wrapped edge shows the puppet
        float w = level.room.width, wx;
        if (p->x >= 0 && p->x <= 480) wx = w - 480 + p->x;
        else if (p->x >= w - 480 && p->x <= w) wx = p->x - (w - 480);
        else wx = -1e9f;
        float tx = floorf(wx) - level.cam_x, ty = floorf(p->y) - level.cam_y;
        if (tx > -64 && tx < TOP_W + 64 && ty > -64 && ty < TOP_H + 64) draw_puppet_tag(p, tx, ty);
    }
    int spr = puppet_sprite(p);
    if (spr < 0) return;
    float x = floorf(p->x) - level.cam_x, y = floorf(p->y) - level.cam_y;
    if (x < -64 || y < -64 || x > TOP_W + 64 || y > TOP_H + 64) return;
    u32 blend = level.room_id == ROOM_ACT9 ? 0xFF000000 : p->alarm_inactive > 0 ? 0xFFFFFFFF : 0xFF404040;
    float alpha = p->hurt ? 0.5f : 1;

    // obj_puppet_tail (drawn behind the body)
    if (p->character == CHARACTER_TAILS && p->hp > 0) {
        int tail = -1;
        float tx = x, ty = y, txs = p->xscale, ta = 0;
        bool demon = p->revival >= 2;
        if (p->state == ST_BALANCING) {
            tail = demon ? SPR_ETAILS_TAIL1 : SPR_TAILS_TAIL1;
            tx += p->xscale * 4;
            ty += 8;
        } else if (p->state == ST_IDLE || p->state == ST_EMOTION1 || p->state == ST_EMOTION2 ||
                   p->state == ST_EMOTION3 || p->state == ST_LOOKDOWN || p->state == ST_LOOKUP) {
            tail = demon ? SPR_ETAILS_TAIL2 : SPR_TAILS_TAIL2;
            tx += p->xscale * 2;
            ty += 4;
        } else if (p->state == ST_JUMP || p->state == ST_SPIN) {
            tail = demon ? SPR_ETAILS_TAIL1 : SPR_TAILS_TAIL1;
            ty += 6;
            txs = 1;
            ta = p->tail_angle;
        }
        if (tail >= 0)
            sprite_draw(tail, g.frame * 0.2f, tx, ty, txs, 1, ta, blend, alpha);
    }
    sprite_draw(spr, p->image_index, x, y, p->xscale, 1, p->angle, blend, alpha);
    if (p->potater) sprite_draw(SPR_GOODPERSON, p->potato_timer, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    // spr_aiz_zipline handle above the hands (height per character)
    static const int ZIPLINE_SPRITES[] = { SPR_EXE_ZIPLINE, SPR_CHAOS_ZIPLINE, SPR_EXELLER_ZIPLINE, SPR_EXETIOR_ZIPLINE,
        SPR_TAILS_ZIPLINE, SPR_ETAILS_ZIPLINE, SPR_KNUX_ZIPLINE, SPR_EKNUX_ZIPLINE, SPR_EGG_ZIPLINE, SPR_EEGG_ZIPLINE,
        SPR_AMY_ZIPLINE, SPR_EAMY_ZIPLINE, SPR_CREAM_ZIPLINE, SPR_ECREAM_ZIPLINE, SPR_SALLY_ZIPLINE, SPR_ESALLY_ZIPLINE };
    bool zipline = false;
    for (unsigned i = 0; i < sizeof ZIPLINE_SPRITES / sizeof *ZIPLINE_SPRITES; i++) zipline |= spr == ZIPLINE_SPRITES[i];
    if (zipline) {
        static const int HAND_Y[7] = { 22, 11, 22, 25, 21, 8, 18 };  // exe tails knux egg amy cream sally
        int c = p->character >= 0 && p->character < 7 ? p->character : 0;
        sprite_draw(SPR_AIZ_ZIPLINE, 0, x - 12, y - HAND_Y[c] - 19, 1, 1, 0, 0xFFFFFFFF, 1);
    }
    float fx = (float)((int)(level.time * 1000.0f / 60 / 30) % 41);  // (current_time / 30) % 41
    if (p->character == CHARACTER_EGGMAN && p->attacking) sprite_draw(SPR_ELECTROSHIELD, fx, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    if (p->sally_shield)
        sprite_draw(p->revival >= 2 ? SPR_SALLYSHIELD2 : SPR_SALLYSHIELD, fx, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    if (p->character == CHARACTER_TAILS && p->tails_charge > 0)
        sprite_draw(SPR_TAILSCHARGE, floorf(p->tails_charge / 60.0f / 5.0f * 100), x + p->xscale * 24, y, p->xscale,
                    1, 0, 0xFFFFFFFF, 1);
    // obj_revival_puppet (x, y - 40) and obj_heal_progress (x, y - 45)
    if (p->revive_show || p->revive_over >= 0) {
        int spr = p->revive_show || p->revive_over == 1 ? SPR_REVIVAL : SPR_REVIVAL2;
        float frame = p->revive_show ? p->revive_progress * sprite_info(SPR_REVIVAL)->frame_count : 0;
        sprite_draw(spr, frame, x, y - 40, 1, 1, 0, 0xFFFFFFFF, 1);
    }
    if (p->heal_visible && sprite_info(SPR_HEAL))
        sprite_draw(SPR_HEAL, p->heal_progress * sprite_info(SPR_HEAL)->frame_count, x, y - 45, 1, 1, 0, 0xFFFFFFFF, 1);
    draw_puppet_tag(p, x, y);
    if (p->dead_timer < 31) ui_text_center(x, y - 58, 0.5f, UI_YELLOW, "%d", p->dead_timer);
}

// ------------------------------------------------------------------ pets (obj_pet_*) of the other players

static void pets_update(void)
{
    level.pet_hidden = g.escaped;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Puppet *p = &g.puppets[i];
        int spr = pet_sprite(p->pet);
        if (!p->used || !p->seen || spr < 0) continue;
        pet_follow(&p->petf, spr, p->x, p->y, p->xscale, false);
    }
}

static void draw_pets(void)
{
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        const Puppet *p = &g.puppets[i];
        if (!p->used || !p->seen || p->escaped || !p->visible) continue;
        pet_draw(&p->petf, pet_sprite(p->pet), p->xscale, 1);
    }
}

static void draw_objects(void)
{
    for (int i = 0; i < MAX_RINGS; i++) {
        const Ring *ring = &g.rings[i];
        if (!ring->used || ring->taken) continue;
        float x = ring->x - level.cam_x, y = ring->y - level.cam_y;
        if (x < -32 || y < -32 || x > TOP_W + 32 || y > TOP_H + 32) continue;
        sprite_draw(ring->red ? SPR_REDRING : SPR_RING, g.ring_frame, x, y, 1, 1, 0, 0xFFFFFFFF, ring->alpha);
    }
    if (g.bigring)
        sprite_draw(g.bigring_ready ? SPR_BIGRING_READY : SPR_BIGRING, g.frame * sprite_frame_step(SPR_BIGRING),
                    g.bigring_x - level.cam_x, g.bigring_y - level.cam_y, 1, 1, 0, 0xFFFFFFFF, g.bigring_alpha);
    ents_draw();
    draw_pets();
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (g.puppets[i].used) draw_puppet(&g.puppets[i]);
}

// ------------------------------------------------------------------ obj_level GUI

// The GUI is 480x270 on PC: centred elements move left by 40, bottom ones up by 30, and
// full-screen pictures are scaled to 400x240.
#define GX(x) ((x) - 40.0f)
#define GY_BOTTOM(y) ((y) - 30.0f)

static void gui_fullscreen(int spr, float frame, float alpha)
{
    if (alpha <= 0) return;
    const SpriteInfo *s = sprite_info(spr);
    if (!s) return;
    sprite_draw(spr, frame, s->xorigin * TOP_W / 480.0f, s->yorigin * TOP_H / 270.0f, TOP_W / 480.0f, TOP_H / 270.0f, 0,
                0xFFFFFFFF, alpha);
}

static bool room_has_overlay(int room)
{
    switch (room) {
    case ROOM_NOTPERFECT: case ROOM_ACT9: case ROOM_YOUCANTRUN: case ROOM_NASTYPARADISE: case ROOM_RAVINEMIST:
    case ROOM_VOLCANOVALLEY: case ROOM_GREENHILL: case ROOM_ANGELISLAND: case ROOM_FARTZONE: case ROOM_WEEDZONE:
        return false;
    default:
        return true;
    }
}

static void counter_m(int number, float x, float y)
{
    char str[8];
    snprintf(str, sizeof str, "%02d", number);
    for (int i = 0; str[i]; i++) sprite_draw(SPR_COUNTER, str[i] - '0', x + 11 * i, y, 1, 1, 0, 0xFFFFFFFF, 1);
}

// spr_playerhealth frame for a survivor (7 per character)
static int health_frame(int character, int hp, int revival)
{
    int f = (character - CHARACTER_TAILS) * 7;
    if (hp <= 0) return f + (revival == 0 ? 5 : 6);
    return f + (int)roundf((1.0f - hp / 100.0f) * 5);
}

static void draw_health_icon(float x, int character, int hp, int revival, bool red, bool escaped, int dead_timer)
{
    float y = GY_BOTTOM(268);
    if (escaped) {
        sprite_draw(SPR_PLAYERESCAPED, character - CHARACTER_TAILS, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
        return;
    }
    if (revival >= 2) {
        sprite_draw(SPR_PLAYERHEALTH_DEMON, character - CHARACTER_TAILS, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
        return;
    }
    sprite_draw(red ? SPR_PLAYERHEALTH_REDRING : SPR_PLAYERHEALTH, health_frame(character, hp, revival), x, y, 1, 1, 0,
                0xFFFFFFFF, 1);
    if (revival >= 1 && hp > 0) sprite_draw(SPR_PLAYERHEALTH_HIT, character - 1, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    if (revival == 0 && hp <= 0 && dead_timer < 31) {
        char n[16];
        snprintf(n, sizeof n, "%d", dead_timer);
        number_spr(x + 12, GY_BOTTOM(264), n, 0xFFFFFF, 1);
    }
}

// ------------------------------------------------------------------ obj_player_warning

static const char *WARN_TEXTS[6] = {
    "the fear of leaving the shelter has shackled you", "your legs refuse to move...",
    "you try to be careful about your movements", "the fear of unknown disarms you",
    "your limbs are cramping with fear...", "you feel way too immobilized",
};
static const char *TERRA_TEXTS[5] = {
    "You feel an evil presence watching you...", "Impending doom approaches...",
    "You feel vibrations from deep below...", "The air is getting colder around you...",
    "This is going to be a terrible night...",
};

static void warning_update(void)
{
    Player *me = &level.player;
    // scr_move_basic: survivors (not in Act 9) past the first minute
    if (level.has_player && me->character != CHARACTER_EXE && net.map != 8 && !me->isDead && me->revivalTimes < 2 &&
        player_time_min >= 1) {
        if (++g.chunk_timer % 60 == 0) {
            float nx = me->x - 480 / 2, ny = me->y - 270 / 2;
            // rectangle_in_rectangle == 0: the new screen no longer overlaps the old one
            if (nx > g.chunk_x + 480 || nx + 480 < g.chunk_x || ny > g.chunk_y + 270 || ny + 270 < g.chunk_y) {
                g.warn_moved = true;
                g.chunk_x = nx;
                g.chunk_y = ny;
                g.chunk_timer = 0;
            }
        }
        if (g.chunk_timer == 20 * 60 && !g.warn) {
            g.warn = true;
            g.warn_moved = false;
            g.warn_fade = g.warn_timer = 0;
            g.warn_text = rand() % 6;
            g.warn_terraria = rand() % 51 == 20;
            audio_play_ex(g.warn_terraria ? SND_BOS : SND_FNAC, 1, g.warn_terraria);
        }
    }
    if (level.has_player && me->isDead) {
        g.chunk_x = g.chunk_y = -1000;
        g.chunk_timer = 0;
    }

    // music: audio_sound_gain(global.music, 0, 2000) while shown, back to 1 in 500 ms
    float want = g.warn ? 0 : 1;
    if (g.music_gain != want || g.music_gain < 1) {  // reapplied: a new track starts at gain 1
        g.music_gain = want < g.music_gain ? fmaxf(want, g.music_gain - 1 / 120.0f) : fminf(want, g.music_gain + 1 / 30.0f);
        audio_music_gain(g.music_gain);
    }
    if (!g.warn) return;
    if (player_time_min < 1 || !level.has_player || me->isDead || me->revivalTimes >= 2) g.warn_moved = true;
    if (!g.warn_moved) {
        g.warn_fade = fminf(1, g.warn_fade + 0.016f);
    } else if (g.warn_fade > 0) {
        g.warn_fade -= 0.016f;
    } else {
        audio_stop_sound(SND_BOS);
        g.warn = false;
    }
    g.warn_timer += 0.5f;
    if (level.has_player) player_slow(me, 5);
}

static void draw_warning(void)
{
    if (!g.warn) return;
    if (g.warn_terraria) {
        const char *t = TERRA_TEXTS[g.warn_text % 5];
        float x = GX(4), y = GY_BOTTOM(240 - 60);
        text_spr(x - 1, y, t, 0, 1);
        text_spr(x, y + 1, t, 0, 1);
        text_spr(x + 1, y, t, 0, 1);
        text_spr(x, y - 1, t, 0, 1);
        text_spr(x, y, t, 0x82FF32, 1);  // #32FF82
        return;
    }
    const char *t = WARN_TEXTS[g.warn_text];
    int len = strlen(t);
    float left = GX(480 / 2 - len * 8 / 2);
    char ch[2] = { 0, 0 };
    for (int i = 1; i <= len; i++) {
        ch[0] = t[i - 1];
        float jx = (rand() % 401 - 200) / 100.0f, jy = (rand() % 2001 - 1000) / 100.0f;
        text_spr(left + i * 8 + jx, 160 - 15.0f + sinf(g.warn_timer + i * 10 + jy), ch, 0x0000FF, g.warn_fade);
    }
}

static void draw_level_gui(void)
{
    const Player *me = &level.player;
    if (room_has_overlay(level.room_id) && settings.gfx_overlays) gui_fullscreen(SPR_SCREENOVERLAY, 0, 0.7f);
    // obj_redring_screen
    // Red ring screen: a steady dark red tint (the animated spr_redring_fore is too flashy on
    // the small screen, and 500 tiles per frame on Old 3DS)
    if (level.has_player && me->redRingTimer > 0)
        C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32(70, 0, 0, 110));
    gui_fullscreen(SPR_HIDEGUI, 0, g.hide_fade);
    gui_fullscreen(SPR_ATTACKGUI, 0, g.blood_fade);
    if (g.blood_fade > 0) g.blood_fade -= 0.02f;
    if (level.has_player && me->isHiding) { if (g.hide_fade < 1) g.hide_fade += 0.02f; }
    else if (g.hide_fade > 0) g.hide_fade -= 0.02f;

    if (g.title_card > 4) {
        // clock and time
        sprite_draw(SPR_CLOCK, 0, GX(211), 0, 1, 1, 0, 0xFFFFFFFF, 1);
        g.clock_angle += (-45.0f * g.time_frame - g.clock_angle) * 0.2f;
        sprite_draw(SPR_CLOCKHAND, 0, GX(212), 11.5f, 1, 1, g.clock_angle, 0xFFFFFFFF, 1);
        sprite_draw(SPR_COUNTER, 10, GX(233), 4, 1, 1, 0, 0xFFFFFFFF, 1);
        sprite_draw(SPR_COUNTER, g.time_min < 10 ? g.time_min : 9, GX(224), 4, 1, 1, 0, 0xFFFFFFFF, 1);
        counter_m(g.time_sec, GX(244), 4);

        // "escape!" once the exit ring is out
        if (g.bigring && (level.time * 1000 / 60 / 30) % 20 < 10) {
            bool demon_side = is_exe() || (level.has_player && me->revivalTimes >= 2);
            sprite_draw(SPR_STATUS, demon_side ? 1 : 0, TOP_W / 2, 32, 1, 1, 0, 0xFFFFFFFF, 1);
        }

        // health icons: ours first, then the other survivors
        int n = 0;
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            if (g.puppets[i].used && g.puppets[i].id != net.exe_id) n++;
        if (!is_exe()) n++;
        float pos = TOP_W / 2 - n * 28 / 2.0f;
        if (!is_exe()) {
            int ch = level.has_player ? me->character : net.my_character;
            if (ch >= CHARACTER_TAILS)
                draw_health_icon(pos, ch, me->hp, me->revivalTimes, me->redRingTimer > 0, g.escaped || !level.has_player,
                                 g.dead_timer);
            pos += 28;
        }
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            const Puppet *p = &g.puppets[i];
            if (!p->used || p->id == net.exe_id || p->character < CHARACTER_TAILS) continue;
            draw_health_icon(pos, p->character, p->hp, p->revival, p->red_ring, p->escaped, p->dead_timer);
            pos += 28;
        }
    }

    // title card and fade-in
    if (g.title_card < 8) {
        if (g.card_x > 0) g.card_x -= 12;
        if (g.card_y < 0) g.card_y += 12;
    }
    if (g.title_card < 18) {
        float a = 1.0f - (fminf(fmaxf(g.title_card, 8), 16) - 8) / 8.0f;
        C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, a));
    }
    if (g.title_card > 18) {
        if (g.card_x < 480) g.card_x += 12;
        if (g.card_y > -270) g.card_y -= 12;
    }
    if (g.title_card < 30) {
        g.card_x = fminf(fmaxf(g.card_x, 0), 480);
        g.card_y = fminf(fmaxf(g.card_y, -270), 0);
        int map = net.map >= 0 ? net.map : 0;
        float sx = TOP_W / 480.0f, sy = TOP_H / 270.0f;
        sprite_draw(SPR_TITLECARD2, map, 0, g.card_y * sy, sx, sy, 0, 0xFFFFFFFF, 1);
        sprite_draw(SPR_TITLECARD1, map, g.card_x * sx, 0, sx, sy, 0, 0xFFFFFFFF, 1);
        g.title_card += 0.12f;
    }
}

void game_draw_top(void)
{
    level_draw_objects = draw_objects;
    level_draw();
    level_draw_objects = NULL;
    ents_draw_front();
    mapobj_draw_gui();
    draw_indicators();
    draw_level_gui();
    draw_warning();
    achiev_draw_popup();

    if (!g.server_ready) {
        C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, 0.6f));
        ui_text_center(TOP_W / 2, 110, 0.6f, UI_WHITE, "Waiting for players...");
    }
    if (g.ending != END_NONE) {
        const char *text = g.ending == END_EXE_WINS ? "EXE WINS"
                         : g.ending == END_TIME_OVER ? "TIME OVER" : "SURVIVORS WIN";
        C2D_DrawRectSolid(0, 96, 0, TOP_W, 48, C2D_Color32f(0, 0, 0, 0.6f));
        ui_text_center(TOP_W / 2, 104, 1.2f, g.ending == END_SURVIVORS_WIN ? UI_GREEN : UI_RED, "%s", text);
    }
    if (level.has_player && g.self_revive_show && sprite_info(SPR_REVIVAL))
        sprite_draw(SPR_REVIVAL, g.self_revive_progress * sprite_info(SPR_REVIVAL)->frame_count,
                    floorf(level.player.x) - level.cam_x, floorf(level.player.y) - 40 - level.cam_y, 1, 1, 0,
                    0xFFFFFFFF, 1);
    if (level.has_player && level.player.isDead && g.dead_timer < 31)
        ui_text_center(TOP_W / 2, 60, 1.0f, UI_YELLOW, "%d", g.dead_timer);
}

void game_draw_bottom(void)
{
    const Player *me = &level.player;
    if (net.state == NET_RESULTS) {
        ui_text(10, 6, 0.6f, UI_WHITE, "Results");
        for (int i = 0; i < g.result_count; i++) {
            const ResultRow *row = &g.results[i];
            u32 col = row->quit ? UI_GRAY : row->type == 0 ? UI_RED : row->type == 1 ? UI_PURPLE : UI_WHITE;
            ui_text(10, 28 + i * 16, 0.45f, col, "%-14.14s rings %u  kills %u  damage %u", row->name, row->rings,
                    row->kills, row->damage);
        }
        return;
    }
    if (g.time_known) ui_text(10, 6, 0.7f, g.time_min == 0 && g.time_sec <= 10 ? UI_RED : UI_WHITE, "%d:%02d",
                              g.time_min, g.time_sec);
    ui_text(90, 10, 0.5f, UI_GRAY, "%s", net.map >= 0 && net.map < LEVEL_COUNT ? LEVELS[net.map].name : "");
    if (g.ping) ui_text(250, 10, 0.45f, g.ping < 80 ? UI_GREEN : g.ping < 160 ? UI_YELLOW : UI_RED, "%dms", g.ping);

    if (is_exe()) {
        ui_text(10, 34, 0.55f, UI_RED, "You are the EXE: catch them all");
    } else if (g.escaped) {
        ui_text(10, 34, 0.55f, UI_GREEN, "Escaped!");
    } else if (level.has_player) {
        u32 hp_col = me->hp > 40 ? UI_GREEN : me->hp > 20 ? UI_YELLOW : UI_RED;
        ui_text(10, 34, 0.55f, hp_col, "HP %d", me->hp);
        ui_text(90, 34, 0.55f, UI_YELLOW, "rings %d", me->rings);
        if (me->revivalTimes >= 2) ui_text(190, 34, 0.55f, UI_RED, "DEMON");
        else if (me->isDead) ui_text(190, 34, 0.55f, UI_RED, "DOWN");
        if (me->redRingTimer > 0) ui_text(190, 50, 0.45f, UI_RED, "red ring %d", me->redRingTimer / 60);
    }

    // players
    int row = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        const Puppet *p = &g.puppets[i];
        if (!p->used) continue;
        u32 col = p->character == CHARACTER_EXE || p->revival >= 2 ? UI_RED : p->escaped ? UI_GREEN : UI_WHITE;
        ui_text(10, 70 + row * 14, 0.42f, col, "%.16s%s%s", p->nickname, p->escaped ? "  (escaped)" : "",
                p->hp <= 0 && p->character != CHARACTER_EXE ? "  (down)" : "");
        row++;
    }
    if (g.status_timer > 0) ui_text_coded(10, 196, 0.45f, UI_YELLOW, g.status);
    ui_text(10, 214, 0.42f, UI_GRAY, "X: chat");
    if (settings.show_perf) main_draw_perf(10, 180);
}

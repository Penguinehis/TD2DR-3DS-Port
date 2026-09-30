#include "menu.h"

#include "level.h"
#include "levels.h"
#include "audio.h"
#include "gen/sounds.h"
#include "font.h"
#include "achiev.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "sprite.h"
#include "ui.h"

AppMode app_mode;

static int title_sel;
static int practice_choice = 1;  // 1..6 survivors, 7..10 EXEs
static const char *PRACTICE_NAMES[11] = { "", "Tails", "Knuckles", "Eggman", "Amy", "Cream", "Sally",
                                          "Exe", "Chaos", "Exetior", "Exeller" };
static int char_sel = 1;   // character select cursor (1-based like obj_lobby_icon.selected)
static int vote_sel;
static bool leave_armed;   // B pressed once in the lobby: press again to leave
static u32 frame;

enum { TITLE_PLAY, TITLE_SERVER, TITLE_NICK, TITLE_PRACTICE, TITLE_ACHIEVEMENTS, TITLE_SHOP, TITLE_SETTINGS,
       TITLE_VIEWER, TITLE_COUNT };

// Title pages
enum { PAGE_MAIN, PAGE_SETTINGS, PAGE_ACHIEVEMENTS, PAGE_SHOP, PAGE_CONTROLS };
static int page = PAGE_MAIN;
static int set_sel;
static float ach_scroll, ach_target;

enum { SET_CONTROLS, SET_LOOKAHEAD, SET_BACKGROUNDS, SET_PARALLAX, SET_EFFECTS, SET_WEATHER, SET_OVERLAYS, SET_PERF,
       SET_MUSIC, SET_SFX, SET_COUNT };
static const char *SET_NAMES[SET_COUNT] = { "Button mapping  >", "Camera look-ahead", "Backgrounds",
                                            "Parallax scrolling", "Effects", "Weather (rain, snow)", "Screen overlays",
                                            "Show performance", "Music volume", "Sound volume" };
static const char *SET_HELP[SET_COUNT] = {
    "Choose the button for each action.",
    "Camera looks ahead where you face.",
    "Layers behind the level.",
    "Depth scrolling of the backgrounds.",
    "Dust, sparkles, shards, hit effects.",
    "Rain, snow and similar particles.",
    "Vignettes and full-screen overlays.",
    "Frame timings on the bottom screen.", "", "",
};

static bool *set_flag(int i)
{
    switch (i) {
    case SET_BACKGROUNDS: return &settings.gfx_backgrounds;
    case SET_PARALLAX: return &settings.gfx_parallax;
    case SET_EFFECTS: return &settings.gfx_effects;
    case SET_WEATHER: return &settings.gfx_weather;
    case SET_OVERLAYS: return &settings.gfx_overlays;
    case SET_LOOKAHEAD: return &settings.cam_lookahead;
    case SET_PERF: return &settings.show_perf;
    default: return NULL;
    }
}

// Button mapping: A picks an action, then the next button pressed is bound to it (an action
// that had that button gets this one's old buttons). START resets, B goes back.
static int ctl_sel;
static bool ctl_waiting;

static void controls_update(u32 down)
{
    if (ctl_waiting) {
        u32 k = down & BIND_BUTTONS;
        if (down & KEY_START) {
            ctl_waiting = false;
        } else if (k) {
            k &= -k;  // one button
            u32 old = settings.bind[ctl_sel];
            for (int i = 0; i < BIND_COUNT; i++)
                if (i != ctl_sel && (settings.bind[i] & k)) {
                    settings.bind[i] &= ~k;
                    if (!settings.bind[i]) settings.bind[i] = old;
                }
            settings.bind[ctl_sel] = k;
            ctl_waiting = false;
            audio_play(SND_MENU_SELECT);
        }
        return;
    }
    if (down & KEY_DOWN) ctl_sel = (ctl_sel + 1) % BIND_COUNT;
    if (down & KEY_UP) ctl_sel = (ctl_sel + BIND_COUNT - 1) % BIND_COUNT;
    if (down & KEY_A) {
        ctl_waiting = true;
        audio_play(SND_MENU_PRESS);
    }
    if (down & KEY_START) {
        memcpy(settings.bind, BIND_DEFAULTS, sizeof settings.bind);
        audio_play(SND_MENU_SELECT);
    }
    if (down & KEY_B) {
        settings_save();
        page = PAGE_SETTINGS;
    }
}

static void draw_controls_bottom(void)
{
    ui_text(10, 6, 0.6f, UI_WHITE, "Button mapping");
    for (int i = 0; i < BIND_COUNT; i++) {
        float y = 32 + i * 20;
        if (i == ctl_sel) C2D_DrawRectSolid(10, y - 3, 0, 300, 20, C2D_Color32(80, 20, 20, 255));
        u32 col = i == ctl_sel ? UI_YELLOW : UI_WHITE;
        ui_text(20, y, 0.5f, col, "%s", BIND_NAMES[i]);
        if (i == ctl_sel && ctl_waiting) ui_text(180, y, 0.5f, UI_GREEN, "press a button...");
        else ui_text(180, y, 0.5f, col, "%s", button_name(settings.bind[i]));
    }
    ui_text(10, 180, 0.42f, UI_GRAY, "Menus always use A = confirm, B = back.");
    ui_text(10, 212, 0.42f, UI_GRAY, ctl_waiting ? "START: cancel" : "A: change   START: defaults   B: back");
}

static void settings_update(u32 down)
{
    if (set_sel == SET_CONTROLS && (down & KEY_A)) {
        page = PAGE_CONTROLS;
        ctl_sel = 0;
        ctl_waiting = false;
        audio_play(SND_MENU_PRESS);
        return;
    }
    if (down & KEY_DOWN) set_sel = (set_sel + 1) % SET_COUNT;
    if (down & KEY_UP) set_sel = (set_sel + SET_COUNT - 1) % SET_COUNT;
    bool *flag = set_flag(set_sel);
    int *vol = set_sel == SET_MUSIC ? &settings.music_volume : set_sel == SET_SFX ? &settings.sfx_volume : NULL;
    bool changed = false;
    if (flag && (down & (KEY_A | KEY_LEFT | KEY_RIGHT))) {
        *flag = !*flag;
        changed = true;
    }
    if (vol && (down & KEY_LEFT) && *vol > 0) { (*vol)--; changed = true; }
    if (vol && (down & KEY_RIGHT) && *vol < 10) { (*vol)++; changed = true; }
    if (changed) {
        audio_apply_volume(settings.music_volume, settings.sfx_volume);
        audio_play(SND_MENU_SELECT);
    }
    if (down & KEY_B) {
        settings_save();
        page = PAGE_MAIN;
    }
}

// Shop: lobby icons, EXE taunts and pets (obj_menu_icon / obj_menu_taunt / obj_menu_pet)
enum { SHOP_ICONS, SHOP_TAUNTS, SHOP_PETS, SHOP_TABS };
static int shop_tab, shop_sel;
static const char *SHOP_TAB_NAMES[SHOP_TABS] = { "Lobby icons", "EXE taunts", "Pets" };
static const int PET_SPRITES[8] = { SPR_PET_NONE, SPR_PET_FLICKY, SPR_PET_CHAO, SPR_PET_METAL, SPR_PET_DALDOL_B,
                                    SPR_PET_MAJIN, SPR_PET_MKNUX, SPR_PET_EGG };
static const char *PET_NAMES[8] = { "none", "flicky", "chao", "metal", "t. doll", "majong", "m. knux", "eggor" };
static const int TAUNT_SPRITES[8] = { SPR_EXE_EMOTION3, SPR_EXE_EMOTION2, SPR_CHAOS_EMOTION1, SPR_CHAOS_EMOTION2,
                                      SPR_EXETIOR_EMOTION3, SPR_EXETIOR_EMOTION2, SPR_EXELLER_EMOTION3,
                                      SPR_EXELLER_EMOTION2 };

static int shop_count(void) { return shop_tab == SHOP_ICONS ? 25 : shop_tab == SHOP_TAUNTS ? 8 : 8; }

static bool shop_owned(int i)
{
    switch (shop_tab) {
    case SHOP_ICONS: return i == 0 || achiev.icons[i];
    case SHOP_TAUNTS: return achiev.taunts[i];
    default: return i == 0 || achiev.pets[i - 1];
    }
}

static int shop_price(int i)
{
    switch (shop_tab) {
    case SHOP_ICONS: return shop_icon_price(i);
    case SHOP_TAUNTS: return shop_taunt_price(i);
    default: return shop_pet_price(i - 1);
    }
}

static void shop_update(u32 down)
{
    int n = shop_count(), cols = shop_tab == SHOP_ICONS ? 5 : 4;
    if (down & KEY_L) { shop_tab = (shop_tab + SHOP_TABS - 1) % SHOP_TABS; shop_sel = 0; }
    if (down & KEY_R) { shop_tab = (shop_tab + 1) % SHOP_TABS; shop_sel = 0; }
    if (down & KEY_RIGHT) shop_sel = (shop_sel + 1) % n;
    if (down & KEY_LEFT) shop_sel = (shop_sel + n - 1) % n;
    if (down & KEY_DOWN) shop_sel = (shop_sel + cols) % n;
    if (down & KEY_UP) shop_sel = (shop_sel + n - cols) % n;
    if (down & KEY_A) {
        int i = shop_sel;
        if (!shop_owned(i)) {
            int price = shop_price(i);
            if (achiev.mercoins < (u64)price) {
                audio_play(SND_NONO);
                return;
            }
            achiev.mercoins -= price;
            if (shop_tab == SHOP_ICONS) achiev.icons[i] = true;
            else if (shop_tab == SHOP_TAUNTS) achiev.taunts[i] = true;
            else achiev.pets[i - 1] = true;
            achiev_save();
            audio_play(SND_CASH);
            return;
        }
        if (shop_tab == SHOP_ICONS) settings.lobby_icon = i;
        else if (shop_tab == SHOP_PETS) settings.pet = i - 1;
        settings_save();
        audio_play(SND_MENU_PRESS);
    }
    if (down & KEY_B) page = PAGE_MAIN;
}

static void achievements_update(u32 held, u32 down)
{
    const SpriteInfo *s = sprite_info(SPR_ACHIVEMENTS);
    float list_h = s ? s->height : 0;
    if (held & KEY_UP) ach_target += 6;
    if (held & KEY_DOWN) ach_target -= 6;
    if (ach_target > 0) ach_target = 0;
    if (ach_target < -(list_h - TOP_H)) ach_target = -(list_h - TOP_H);
    ach_scroll += (ach_target - ach_scroll) * 0.2f;
    if (down & KEY_B) {
        achiev_clear_new();
        page = PAGE_MAIN;
    }
}

static const char *SURVIVOR_NAMES[7] = { "Exe", "Tails", "Knuckles", "Eggman", "Amy", "Cream", "Sally" };
static const char *EXE_NAMES[4] = { "Exe", "Chaos", "Exetior", "Exeller" };
static const int EXE_ICONS[4] = { SPR_LOBBY_EXEICON, SPR_LOBBY_EXEICON2, SPR_LOBBY_EXEICON3, SPR_LOBBY_EXEICON4 };

static const char *DISCONNECT_TEXT[] = {
    "Failed to connect", "Kicked by the host", "Banned by the host", "Version mismatch",
    "Server timed out", "Packets not received", "The game already started", "Kicked for being AFK",
    "The lobby is full", "Rate limited: wait a few seconds", "The server shut down", "Already connected from this address",
};

static void show_room(int room)
{
    if (level.room_id != room || !level.room.layers) level_load(room, -1);
}

void menu_enter_title(void)
{
    app_mode = APP_TITLE;
    show_room(ROOM_MENU);
}

void menu_connect(const char *address)
{
    app_mode = APP_ONLINE;
    leave_armed = false;
    show_room(ROOM_LOBBY);
    net_connect(address, -1);
}

void menu_start_practice(int room, int choice)
{
    app_mode = APP_OFFLINE;
    level_load(room, -1);
    if (choice < 1 || choice > 10) return;
    const RoomInstance *sp = room_instance_find(&level.room, OBJ_SPAWNPOINT, 0);
    if (!sp) return;
    if (choice <= 6) level_spawn(choice, 0, sp->x, sp->y - 18);
    else level_spawn(CHARACTER_EXE, choice - 7, sp->x, sp->y - 18);
}

static void title_update(u32 held, u32 down)
{
    if (page == PAGE_SETTINGS) {
        settings_update(down);
        return;
    }
    if (page == PAGE_CONTROLS) {
        controls_update(down);
        return;
    }
    if (page == PAGE_ACHIEVEMENTS) {
        achievements_update(held, down);
        return;
    }
    if (page == PAGE_SHOP) {
        shop_update(down);
        return;
    }
    if (title_sel == TITLE_PRACTICE) {
        if (down & KEY_LEFT) practice_choice = practice_choice <= 1 ? 10 : practice_choice - 1;
        if (down & KEY_RIGHT) practice_choice = practice_choice >= 10 ? 1 : practice_choice + 1;
    }
    if (down & KEY_DOWN) title_sel = (title_sel + 1) % TITLE_COUNT;
    if (down & KEY_UP) title_sel = (title_sel + TITLE_COUNT - 1) % TITLE_COUNT;
    if (down & KEY_START) app_mode = APP_QUIT;
    if (!(down & KEY_A)) return;
    switch (title_sel) {
    case TITLE_PLAY:
        if (!settings.server[0] &&
            !ui_keyboard("Server address (host or host:port)", NULL, settings.server, sizeof settings.server, 63))
            break;
        settings_save();
        menu_connect(settings.server);
        break;
    case TITLE_SERVER:
        if (ui_keyboard("Server address (host or host:port)", settings.server, settings.server,
                        sizeof settings.server, 63))
            settings_save();
        break;
    case TITLE_NICK:
        if (ui_keyboard("Nickname (max 29 characters)", settings.nickname, settings.nickname,
                        sizeof settings.nickname, 29))
            settings_save();
        break;
    case TITLE_PRACTICE:
        menu_start_practice(ROOM_GREENHILL, practice_choice);
        break;
    case TITLE_VIEWER:
        app_mode = APP_OFFLINE;
        level_load(ROOM_GREENHILL, -1);
        break;
    case TITLE_ACHIEVEMENTS:
        page = PAGE_ACHIEVEMENTS;
        ach_scroll = ach_target = 0;
        break;
    case TITLE_SETTINGS:
        page = PAGE_SETTINGS;
        break;
    case TITLE_SHOP:
        page = PAGE_SHOP;
        shop_sel = 0;
        break;
    }
}

// Debug 'x': play the menus automatically (ready, vote, pick the first free character).
static void auto_play(void)
{
    static u32 wait;
    if (++wait < 90) return;
    switch (net.state) {
    case NET_LOBBY: {
        // E: ask to be the EXE (operator chat trigger; local clients are operators)
        static bool asked;
        if (dbg_flag('E') && !asked) { net_send_chat("i want big burgr"); asked = true; wait = 0; break; }
        if (net.lobby_ready && !net.my_ready) { net_set_ready(true); wait = 0; }
        break;
    }
    case NET_VOTE:
        if (net.my_vote < 0) { net_vote(0); wait = 0; }
        break;
    case NET_CHARSELECT:
        if (net.exe_id == net.id) {
            if (net.my_exe_character < 0) { net_request_character(1); wait = 0; }
        } else if (net.my_character < 0) {
            for (int c = 1; c <= 6; c++)
                if (net.av_characters[c]) { net_request_character(c); break; }
            wait = 0;
        }
        break;
    default:
        break;
    }
}

// Chat in the menus: the mapped chat button, unless it is A or B (confirm / back here): X
static u32 menu_chat_key(void)
{
    u32 k = settings.bind[BIND_CHAT] & ~(KEY_A | KEY_B);
    return k ? k : KEY_X;
}

static void online_update(u32 down)
{
    if (dbg_flag('x')) auto_play();
    switch (net.state) {
    case NET_OFF:
    case NET_ERROR:
        if (down & (KEY_A | KEY_B)) {
            net_disconnect();
            menu_enter_title();
        }
        return;
    case NET_CONNECTING:
    case NET_WAITING:
        if (down & KEY_B) {
            net_disconnect();
            menu_enter_title();
            return;
        }
        if (net.state == NET_WAITING && (down & menu_chat_key())) {  // chat while a match runs
            char msg[96];
            if (ui_keyboard("Chat message", NULL, msg, sizeof msg, 80)) net_send_chat(msg);
        }
        return;
    case NET_LOBBY:
        if (down & KEY_A) net_set_ready(!net.my_ready);
        break;
    case NET_VOTE:
        if (net.my_vote < 0) {
            if (down & KEY_LEFT) vote_sel = (vote_sel + 2) % 3;
            if (down & KEY_RIGHT) vote_sel = (vote_sel + 1) % 3;
            if (down & KEY_A) net_vote(vote_sel);
        }
        break;
    case NET_CHARSELECT: {
        bool exe = net.exe_id == net.id;
        int count = exe ? 4 : 6;
        if (net.my_character < 0 || (exe && net.my_exe_character < 0)) {
            if (char_sel < 1 || char_sel > count) char_sel = 1;
            if (down & KEY_LEFT) char_sel = char_sel <= 1 ? count : char_sel - 1;
            if (down & KEY_RIGHT) char_sel = char_sel >= count ? 1 : char_sel + 1;
            if (down & KEY_A) net_request_character(char_sel);
        }
        break;
    }
    default:
        break;
    }
    if (down & menu_chat_key()) {
        char msg[96];
        if (ui_keyboard("Chat message", NULL, msg, sizeof msg, 80)) net_send_chat(msg);
    }
    if (down & KEY_B) {
        if (leave_armed) {
            net_disconnect();
            menu_enter_title();
            return;
        }
        leave_armed = true;
    } else if (down) {
        leave_armed = false;
    }
}

// Sounds for net events (obj_lobby / obj_menu_waiting)
static void event_sounds(void)
{
    static u32 join, leave, chat, ready, clock, nono;
    if (net.ev_join != join) audio_play(SND_RING);
    if (net.ev_leave != leave) audio_play(SND_HURT);
    if (net.ev_chat != chat) audio_play(SND_MESSAGE);
    if (net.ev_ready != ready) audio_play(SND_READY);
    if (net.ev_clock != clock) audio_play(SND_CLOCK);
    if (net.ev_nono != nono) audio_play(SND_NONO);
    join = net.ev_join;
    leave = net.ev_leave;
    chat = net.ev_chat;
    ready = net.ev_ready;
    clock = net.ev_clock;
    nono = net.ev_nono;
}

int menu_music(void)
{
    if (app_mode == APP_TITLE) return SND_MUS_MENU;
    switch (net.state) {
    case NET_WAITING: return SND_MUS_WAITING;
    case NET_LOBBY:
    case NET_VOTE: return SND_MUS_LOBBY;
    case NET_CHARSELECT: return SND_MUS_LOBBY_EPIC;
    default: return -2;
    }
}

void menu_update(u32 held, u32 down)
{
    frame++;
    event_sounds();
    if (app_mode == APP_TITLE && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))) audio_play(SND_MENU_SELECT);
    if (app_mode == APP_TITLE && (down & KEY_A)) audio_play(SND_MENU_PRESS);
    if (app_mode == APP_ONLINE && level.room_id != ROOM_LOBBY) show_room(ROOM_LOBBY);
    if (app_mode == APP_TITLE) title_update(held, down);
    else if (app_mode == APP_ONLINE) online_update(down);
    level.time++;
}

// ------------------------------------------------------------------ drawing

static void draw_room_centered(void)
{
    // Menu rooms are 480x270: shown 1:1 (pixel-perfect) around the centre; objects that would
    // be cut by the edges (the mercoin counters, the corner buttons) are pulled inside.
    level.cam_x = floorf(((float)level.room.width - TOP_W) / 2);
    level.cam_y = floorf(((float)level.room.height - TOP_H) / 2);
    if (level.cam_x < 0) level.cam_x = 0;
    if (level.cam_y < 0) level.cam_y = 0;
    level.view_w = TOP_W;
    level.view_h = TOP_H;
    level.gui_clamp = true;
    level_draw();
    level.gui_clamp = false;
}

static void dim_top(float alpha)
{
    C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, alpha));
}

static void draw_icon(int spr, float frame_idx, float x, float y, u32 blend)
{
    // Lobby icons are 24x24 with the origin in the centre.
    sprite_draw(spr, frame_idx, x, y, 1, 1, 0, blend, 1);
}

// obj_lobby_icon: our icon in the middle (240, 115), the others in a row at y 58, 80 px apart
// (positions scaled from the 480x270 room); ready players are tinted lime; names in the game's
// sprite font, cut to the room between icons.
static void draw_player_icon(int id, float x, float y, float room_w)
{
    bool me = id == net.id;
    NetPlayer *p = me ? NULL : net_player(id);
    if (!me && !p) return;
    const char *nick = me ? settings.nickname : p->nickname;
    int character = me ? net.my_character : p->character;
    int exe_char = me ? net.my_exe_character : p->exe_character;
    bool ready = me ? net.my_ready : p->ready;
    u32 blend = net.state == NET_LOBBY && ready ? 0xFF00FF00 : 0xFFFFFFFF;
    if (net.state == NET_CHARSELECT || net.state == NET_GAME) {
        if (id == net.exe_id)
            draw_icon(exe_char >= 0 && exe_char < 4 ? EXE_ICONS[exe_char] : SPR_LOBBY_EXEICON5,
                      exe_char >= 0 ? frame * 0.25f : (me ? settings.lobby_icon : p->icon), x, y, 0xFFFFFFFF);
        else
            draw_icon(SPR_LOBBY_ICON, character > 0 ? character + 1 : 0, x, y, 0xFFFFFFFF);
    } else {
        draw_icon(SPR_LOBBY_DICON, me ? settings.lobby_icon : p->icon, x, y, blend);
    }
    char name[32];
    snprintf(name, sizeof name, "%s", nick);
    for (size_t n = strlen(name); n > 1 && text_spr_width(name) > room_w; n--) name[n - 1] = 0;
    float w = text_spr_width(name);
    text_spr(floorf(x - w / 2), y + 16, name, me ? 0x00FFFF : FONT_WHITE, 1);  // BGR: yellow for us
}

static void draw_player_row(float y)
{
    (void)y;
    float sx = TOP_W / 480.0f, sy = TOP_H / 270.0f;
    draw_player_icon(net.id, floorf(240 * sx), floorf(115 * sy), 120);
    int i = 0;
    for (int k = 0; k < NET_MAX_PLAYERS; k++) {
        if (!net.players[k].used || net.players[k].id == net.id) continue;
        draw_player_icon(net.players[k].id, floorf((40 + i * 80) * sx), floorf(58 * sy), 80 * sx - 4);
        i++;
    }
}

static void menu_draw_top_inner(void);
static void draw_achievements_top(void);

void menu_draw_top(void)
{
    menu_draw_top_inner();
    achiev_draw_popup();
}

static void menu_draw_top_inner(void)
{
    if (app_mode == APP_TITLE && page == PAGE_ACHIEVEMENTS) {
        draw_achievements_top();
        return;
    }
    draw_room_centered();
    if (app_mode == APP_TITLE) {
        ui_text_center(TOP_W / 2, 214, 0.5f, UI_YELLOW, "Port made by PenguinEhis");

        return;
    }
    switch (net.state) {
    case NET_VOTE:
        dim_top(0.5f);
        ui_text_center(TOP_W / 2, 12, 0.6f, UI_WHITE, "Vote for the next map  %u", net.vote_timer);
        for (int i = 0; i < 3; i++) {
            float cx = 70 + i * 130;
            int map = net.vote_maps[i];
            // spr_mapvote: 123x104 preview per map
            sprite_draw(SPR_MAPVOTE, map, cx - 61, 50, 1, 1, 0, 0xFFFFFFFF,
                        net.my_vote < 0 || net.my_vote == i ? 1 : 0.4f);
            if (i == (net.my_vote >= 0 ? net.my_vote : vote_sel))
                C2D_DrawRectSolid(cx - 63, 156, 0, 126, 3, net.my_vote >= 0 ? UI_GREEN : UI_YELLOW);
            ui_text_center(cx, 164, 0.45f, UI_WHITE, "%s", map < LEVEL_COUNT ? LEVELS[map].name : "?");
            ui_text_center(cx, 184, 0.6f, UI_YELLOW, "%u", net.vote_counts[i]);
        }
        break;
    case NET_CHARSELECT:
        dim_top(0.5f);
        ui_text_center(TOP_W / 2, 12, 0.6f, UI_WHITE, "%s  -  %u",
                       net.map >= 0 && net.map < LEVEL_COUNT ? LEVELS[net.map].name : "?", net.char_timer);
        draw_player_row(120);
        break;
    case NET_LOBBY:
    case NET_WAITING:
        draw_player_row(120);
        if (net.state == NET_LOBBY && net.counting)
            ui_text_center(TOP_W / 2, 40, 1.2f, UI_YELLOW, "%u", net.countdown);
        break;
    case NET_ERROR:
        dim_top(0.6f);
        if (net.error_code >= 0 && net.error_code < 12) {
            const SpriteInfo *s = sprite_info(SPR_MENU_ERROR);
            if (s) sprite_draw(SPR_MENU_ERROR, net.error_code, TOP_W / 2 - s->width / 2 + s->xorigin, 110 + s->yorigin,
                               1, 1, 0, 0xFFFFFFFF, 1);
        }
        break;
    default:
        dim_top(0.4f);
        break;
    }
}

// Chat, wrapped to the screen width: the newest messages that fit in max_lines
static int chat_lines(int i, bool draw, float y)
{
    float x = 6;
    char head[48];
    snprintf(head, sizeof head, "%s: ", net.chat[i].sender);
    int n = ui_text_coded_wrap(6, y, 6, BOT_W - 4, 13, 0.42f, UI_GRAY, head, draw, &x);
    float yy = y + (n - 1) * 13;
    return n - 1 + ui_text_coded_wrap(x, yy, 6, BOT_W - 4, 13, 0.42f, UI_WHITE, net.chat[i].text, draw, NULL);
}

static void draw_chat(float y0)
{
    const int max_lines = 8;  // the area of the old 8 one-line messages, bottom-aligned
    int first = net.chat_count, used = 0;
    while (first > 0) {
        int n = chat_lines(first - 1, false, 0);
        if (used + n > max_lines) break;
        used += n;
        first--;
    }
    float y = y0 + (max_lines - used) * 13;
    for (int i = first; i < net.chat_count; i++) y += chat_lines(i, true, y) * 13;
}

static void draw_title_bottom(void)
{
    const char *items[TITLE_COUNT];
    char server[96], nick[64];
    snprintf(server, sizeof server, "Server: %s", settings.server[0] ? settings.server : "(not set)");
    snprintf(nick, sizeof nick, "Nickname: %s", settings.nickname);
    items[TITLE_PLAY] = "Play online";
    items[TITLE_SERVER] = server;
    items[TITLE_NICK] = nick;
    char practice[64];
    snprintf(practice, sizeof practice, "Offline practice: < %s >", PRACTICE_NAMES[practice_choice]);
    items[TITLE_PRACTICE] = practice;
    items[TITLE_VIEWER] = "Level viewer";
    char ach[48];
    int done = 0;
    for (int i = 0; i < ACHIEV_COUNT; i++) done += achiev.achieved[i];
    snprintf(ach, sizeof ach, "Achievements (%d/%d)%s", done, ACHIEV_COUNT, achiev.changed_count ? "  new!" : "");
    items[TITLE_ACHIEVEMENTS] = ach;
    items[TITLE_SETTINGS] = "Settings";
    char shop[48];
    snprintf(shop, sizeof shop, "Shop  (%llu mercoins)", (unsigned long long)achiev.mercoins);
    items[TITLE_SHOP] = shop;
    for (int i = 0; i < TITLE_COUNT; i++) {
        float y = 10 + i * 23;
        if (i == title_sel) C2D_DrawRectSolid(10, y - 3, 0, 300, 20, C2D_Color32(80, 20, 20, 255));
        ui_text(20, y, 0.5f, i == title_sel ? UI_YELLOW : UI_WHITE, "%s", items[i]);
    }
    if (!audio_available()) {
        ui_text(10, 176, 0.42f, UI_YELLOW, "No sound: sd:/3ds/dspfirm.cdc is missing.");
        ui_text(10, 190, 0.42f, UI_YELLOW, "Run the DSP1 homebrew once to create it.");
    }
    ui_text(10, 212, 0.45f, UI_GRAY, "A: select   START: quit");
}

static void draw_settings_bottom(void)
{
    ui_text(10, 6, 0.6f, UI_WHITE, "Settings");
    for (int i = 0; i < SET_COUNT; i++) {
        float y = 26 + i * 17;
        if (i == set_sel) C2D_DrawRectSolid(10, y - 2, 0, 300, 17, C2D_Color32(80, 20, 20, 255));
        u32 col = i == set_sel ? UI_YELLOW : UI_WHITE;
        ui_text(20, y, 0.45f, col, "%s", SET_NAMES[i]);
        bool *flag = set_flag(i);
        if (flag) ui_text(250, y, 0.45f, *flag ? UI_GREEN : UI_RED, "%s", *flag ? "On" : "Off");
        else if (i == SET_MUSIC || i == SET_SFX)
            ui_text(235, y, 0.45f, col, "< %d >", i == SET_MUSIC ? settings.music_volume : settings.sfx_volume);
    }
    ui_text(10, 196, 0.4f, UI_GRAY, "%s", SET_HELP[set_sel]);
    ui_text(10, 212, 0.45f, UI_GRAY, "A / Left / Right: change   B: back");
}

static void draw_shop_bottom(void)
{
    ui_text(10, 4, 0.5f, UI_WHITE, "L < %s > R", SHOP_TAB_NAMES[shop_tab]);
    ui_text(200, 4, 0.5f, UI_YELLOW, "%llu mercoins", (unsigned long long)achiev.mercoins);
    int n = shop_count(), cols = shop_tab == SHOP_ICONS ? 5 : 4;
    float cw = BOT_W / (float)cols, ch = shop_tab == SHOP_ICONS ? 36 : shop_tab == SHOP_TAUNTS ? 76 : 70;
    for (int i = 0; i < n; i++) {
        float x = (i % cols) * cw, y = 24 + (i / cols) * ch;
        bool owned = shop_owned(i);
        bool equipped = (shop_tab == SHOP_ICONS && settings.lobby_icon == i) ||
                        (shop_tab == SHOP_PETS && settings.pet == i - 1);
        if (i == shop_sel) C2D_DrawRectSolid(x + 1, y, 0, cw - 2, ch - 2, C2D_Color32(80, 20, 20, 255));
        if (shop_tab == SHOP_ICONS) {
            sprite_draw(SPR_LOBBY_DICON, i, x + cw / 2, y + 14, 1, 1, 0, 0xFFFFFFFF, owned ? 1 : 0.5f);
        } else {
            int spr = shop_tab == SHOP_TAUNTS ? TAUNT_SPRITES[i] : PET_SPRITES[i];
            const SpriteInfo *s = sprite_info(spr);
            float sc = 1;  // 1:1 (pixel-perfect); the tallest, Exeller's taunt, is 55 px
            if (s) sprite_draw(spr, 0, x + cw / 2 - (s->width / 2 - s->xorigin) * sc, y + 2 + s->yorigin * sc, sc, sc, 0,
                               0xFFFFFFFF, owned ? 1 : 0.5f);
            if (shop_tab == SHOP_PETS) ui_text_center(x + cw / 2, y + ch - 30, 0.38f, UI_WHITE, "%s", PET_NAMES[i]);
        }
        const char *state = equipped ? "on" : owned ? (shop_tab == SHOP_TAUNTS ? "owned" : "equip") : NULL;
        if (state) ui_text_center(x + cw / 2, y + ch - 16, 0.36f, equipped ? UI_GREEN : UI_GRAY, "%s", state);
        else ui_text_center(x + cw / 2, y + ch - 16, 0.36f, UI_YELLOW, "%d", shop_price(i));
    }
    if (shop_tab == SHOP_TAUNTS) ui_text(10, 200, 0.4f, UI_GRAY, "EXE emotions 1 / 2 (L / R in game) once bought.");
    ui_text(10, 222, 0.42f, UI_GRAY, "A: buy / equip   L / R: tab   B: back");
}

// obj_menu_achivements: the 50 rows of spr_achivements (428 x 38 each) on the top screen at 1:1.
// 428 px does not fit 400: the reward column (from x 320) is drawn flush right over the empty
// end of the description boxes.
static void draw_achievements_top(void)
{
    const float row = 38;
    float y0 = floorf(ach_scroll);
    C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32(0, 0, 0, 255));
    sprite_draw_part(SPR_ACHIVEMENTS, 0, 0, y0, 0, 320);
    sprite_draw_part(SPR_ACHIVEMENTS, 0, TOP_W - 428, y0, 320, 432);
    for (int i = 0; i < ACHIEV_COUNT; i++) {
        float y = y0 + i * row;
        if (y < -row || y > TOP_H) continue;
        if (achiev.achieved[i]) C2D_DrawRectSolid(0, y, 0, TOP_W, row - 1, C2D_Color32(15, 255, 57, 60));
    }
    for (int j = 0; j < achiev.changed_count; j++)
        sprite_draw(SPR_ACHIVEMENTS_NEW, 0, 0, y0 + achiev.changed[j] * row, 1, 1, 0, 0xFFFFFFFF, 1);
}

static void draw_achievements_bottom(void)
{
    int earned = 0;
    for (int i = 0; i < ACHIEV_COUNT; i++) earned += achiev.achieved[i];
    ui_text(10, 8, 0.6f, UI_WHITE, "Achievements");
    ui_text(10, 44, 0.5f, UI_GREEN, "%d of %d earned", earned, ACHIEV_COUNT);
    ui_text(10, 64, 0.5f, UI_YELLOW, "%llu mercoins", (unsigned long long)achiev.mercoins);
    if (achiev.changed_count) ui_text(10, 84, 0.5f, UI_WHITE, "%d new", achiev.changed_count);
    ui_text(10, 120, 0.45f, UI_GRAY, "Earned ones are marked green.");
    ui_text(10, 214, 0.45f, UI_GRAY, "Up / Down: scroll   B: back");
}

void menu_draw_bottom(void)
{
    if (app_mode == APP_TITLE && page == PAGE_SETTINGS) {
        draw_settings_bottom();
        return;
    }
    if (app_mode == APP_TITLE && page == PAGE_CONTROLS) {
        draw_controls_bottom();
        return;
    }
    if (app_mode == APP_TITLE && page == PAGE_ACHIEVEMENTS) {
        draw_achievements_bottom();
        return;
    }
    if (app_mode == APP_TITLE && page == PAGE_SHOP) {
        draw_shop_bottom();
        return;
    }
    if (app_mode == APP_TITLE) {
        draw_title_bottom();
        return;
    }
    char hint[48];
    snprintf(hint, sizeof hint, "%s: chat   B twice: leave", button_name(menu_chat_key()));
    switch (net.state) {
    case NET_CONNECTING:
        ui_text(10, 10, 0.55f, UI_WHITE, "Connecting to %s:%u...", net.host, net.port);
        ui_text(10, 212, 0.45f, UI_GRAY, "B: cancel");
        return;
    case NET_WAITING:
        ui_text(10, 10, 0.55f, UI_WHITE, "A match is running. Waiting...");
        if (net.wait_clock)
            ui_text(10, 30, 0.5f, UI_YELLOW, "time left %d:%02d", (net.wait_timer + 59) / 3600,
                    ((net.wait_timer + 59) / 60) % 60);
        draw_chat(60);
        ui_text(10, 212, 0.45f, UI_GRAY, net.state == NET_WAITING ? "%s: chat   B: leave" : "B: leave",
                button_name(menu_chat_key()));
        return;
    case NET_LOBBY:
        ui_text(10, 6, 0.5f, UI_WHITE, "Lobby  %d players  exe chance %u%%", net_player_count() + 1, net.exe_chance);
        ui_text(10, 22, 0.5f, net.my_ready ? UI_GREEN : UI_YELLOW, net.my_ready ? "You are ready (A: not ready)"
                                                                            : "A: ready");
        break;
    case NET_VOTE:
        ui_text(10, 6, 0.5f, UI_WHITE, net.my_vote >= 0 ? "Vote sent" : "Left/Right: choose   A: vote");
        break;
    case NET_CHARSELECT: {
        bool exe = net.exe_id == net.id;
        bool chosen = exe ? net.my_exe_character >= 0 : net.my_character >= 0;
        ui_text(10, 6, 0.5f, exe ? UI_RED : UI_WHITE, exe ? "You are the EXE!" : "You are a survivor");
        if (chosen) {
            ui_text(10, 22, 0.5f, UI_GREEN, "Chosen: %s",
                    exe ? EXE_NAMES[net.my_exe_character & 3] : SURVIVOR_NAMES[net.my_character % 7]);
        } else {
            int count = exe ? 4 : 6;
            for (int i = 1; i <= count; i++) {
                float x = 30 + (i - 1) * 50;
                if (exe) draw_icon(EXE_ICONS[i - 1], frame * 0.25f, x, 50, 0xFFFFFFFF);
                else draw_icon(SPR_LOBBY_ICON, i + 1, x, 50, 0xFFFFFFFF);
                if (!exe && !net.av_characters[i]) draw_icon(SPR_LOBBY_ICON_USED, 0, x, 50, 0xFFFFFFFF);
                if (i == char_sel) C2D_DrawRectSolid(x - 14, 66, 0, 28, 2, UI_YELLOW);
            }
            ui_text(10, 72, 0.5f, UI_YELLOW, "%s  (A: choose)", exe ? EXE_NAMES[(char_sel - 1) & 3]
                                                                  : SURVIVOR_NAMES[char_sel % 7]);
        }
        break;
    }
    case NET_GAME:
        ui_text(10, 10, 0.55f, UI_WHITE, "Match running");
        break;
    case NET_RESULTS:
        ui_text(10, 10, 0.55f, UI_WHITE, "Results");
        break;
    case NET_OFF:
    case NET_ERROR:
        ui_text(10, 10, 0.6f, UI_RED, "Disconnected");
        if (net.error_code >= 0 && net.error_code < 12) ui_text(10, 32, 0.5f, UI_WHITE, "%s", DISCONNECT_TEXT[net.error_code]);
        if (net.error_text[0]) ui_text_coded(10, 50, 0.5f, UI_WHITE, net.error_text);
        ui_text(10, 212, 0.45f, UI_GRAY, "A: back");
        return;
    }
    draw_chat(96);
    ui_text(10, 212, 0.45f, leave_armed ? UI_RED : UI_GRAY, leave_armed ? "Press B again to leave" : "%s", hint);
}

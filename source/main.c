// Sonic.exe: The Disaster 2D Remake for Nintendo 3DS.
// Title menu -> online (lobby, vote, character select, match) or offline practice.
// Offline controls:
//   Circle Pad / D-Pad  move          A  jump / fly          B  tail shot (hold to charge)
//   L / R               previous / next room
//   SELECT              show invisible objects (solids, spawns, triggers) as boxes
//   START               back to the title
#include <math.h>
#include <stdarg.h>

#include "audio.h"
#include "achiev.h"
#include "common.h"
#include "game.h"
#include "level.h"
#include "loader.h"
#include "maps/maps.h"
#include "levels.h"
#include "mapobj.h"
#include "menu.h"
#include "net.h"
#include "player.h"
#include "settings.h"
#include "sprite.h"
#include "ui.h"

// ------------------------------------------------------------------ util

void *read_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(size > 0 ? size : 1);
    if (buf && fread(buf, 1, size, f) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf && size_out) *size_out = size;
    return buf;
}

// Append a line to sdmc:/sonic3ds.log (in Azahar: %APPDATA%\Azahar\sdmc\sonic3ds.log).
// Only in debug mode (sdmc:/sonic3ds.cfg present): every line is a blocking SD card write,
// which freezes the game for a moment on real hardware.
static bool log_enabled;

// Lines are collected in memory and written in one go (dbg_flush) so the timings measured in
// debug mode are not dominated by the log itself.
static char log_buf[16384];
static size_t log_len;

void dbg_flush(void)
{
    if (!log_len) return;
    static bool first = true;
    FILE *f = fopen("sdmc:/sonic3ds.log", first ? "w" : "a");
    first = false;
    if (f) {
        fwrite(log_buf, 1, log_len, f);
        fclose(f);
    }
    log_len = 0;
}

void dbg_log(const char *fmt, ...)
{
    if (!log_enabled) return;
    if (log_len > sizeof log_buf - 512) dbg_flush();
    size_t room = sizeof log_buf - log_len - 1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(log_buf + log_len, room, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    log_len += (size_t)n < room ? (size_t)n : room - 1;
    log_buf[log_len++] = '\n';
}

static bool c2d_ready;

// Debug switches read from sdmc:/sonic3ds.cfg:
//   b skip backgrounds, a skip level art, i skip instances, s skip assets, t skip text
//   p<N> save both screens to sdmc:/sonic3ds_top.ppm / _bottom.ppm at frame N (default 120)
//   d    offline practice with scripted demo input and a position log every 30 frames
//   o    start in offline practice;  v  start in the level viewer
//   r<N> offline start room N (index into ROOM_FILES)
//   k<N> offline character: 1..6 survivors (Tails..Sally), 7..10 EXE, Chaos, Exetior, Exeller
//   c<address>;  connect to a server at startup (e.g. "c192.168.1.5:8606;")
//   n<name>;     nickname override
//   A<N> / B<N> / U<N> / D<N>  press A / B / Up / Down at frame N (automated tests)
//   m<N> hold N MB of linear memory (simulate low memory)
//   x    auto-play the online menus (ready, vote, first free character)
static bool dbg_flags[128];
static int dbg_nums[128];
static bool dbg_has_num[128];
static char dbg_strs[128][64];

static void dbg_parse(const char *cfg)
{
    for (const char *c = cfg; *c; c++) {
        unsigned char k = (unsigned char)*c;
        if (k >= 128 || k <= ' ') continue;
        dbg_flags[k] = true;
        if (k == 'c' || k == 'n') {
            const char *end = strchr(c + 1, ';');
            int len = end ? (int)(end - c - 1) : (int)strlen(c + 1);
            snprintf(dbg_strs[k], sizeof dbg_strs[k], "%.*s", len, c + 1);
            c += len + (end ? 1 : 0);
        } else if (c[1] >= '0' && c[1] <= '9') {
            if (!dbg_has_num[k]) dbg_nums[k] = atoi(c + 1);
            dbg_has_num[k] = true;
            while (c[1] >= '0' && c[1] <= '9') c++;
        }
    }
}

bool dbg_flag(char c) { return (unsigned char)c < 128 && dbg_flags[(unsigned char)c]; }
int dbg_num(char c, int def) { return dbg_flag(c) && dbg_has_num[(unsigned char)c] ? dbg_nums[(unsigned char)c] : def; }
bool dbg_str(char c, char *out, size_t size)
{
    if (!dbg_flag(c) || !dbg_strs[(unsigned char)c][0]) return false;
    const char *src = dbg_strs[(unsigned char)c];
    size_t n = strnlen(src, size - 1);
    memcpy(out, src, n);
    out[n] = 0;
    return true;
}

// Scripted A/B presses from the cfg ("A300B420A500"): all numbers after each letter.
static char dbg_raw[256];
static bool dbg_press(char key, u32 frame)
{
    for (const char *c = dbg_raw; *c; c++) {
        if (*c == 'c' || *c == 'n') { const char *e = strchr(c, ';'); if (!e) break; c = e; continue; }
        if (*c == key && (u32)atoi(c + 1) == frame && c[1] >= '0' && c[1] <= '9') return true;
    }
    return false;
}

// Demo input: idle 1 s, then run right; jump at 2 s, fly (A in the air) at 2.3 s,
// look down at 6 s, run left from 7 s.
static void demo_keys(u32 frame, u32 *held, u32 *down)
{
    *held = *down = 0;
    if (dbg_flag('l')) {  // walk left only
        if (frame >= 60) *held |= KEY_LEFT;
        return;
    }
    if (frame >= 60 && frame < 360) *held |= KEY_RIGHT;
    if (frame >= 420) *held |= KEY_LEFT;
    if (frame == 120 || frame == 138) *down |= KEY_A;
    if (frame >= 120 && frame < 200) *held |= KEY_A;
    if (frame >= 360 && frame < 420) *held |= KEY_DOWN;
}

// Show the error on the top screen and wait for START. The system error applet is
// avoided on purpose: in emulators without system applets it can hang.
void fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    dbg_log("FATAL: %s", msg);
    dbg_flush();
    if (c2d_ready) {
        C2D_Fini();
        C3D_Fini();
    }
    consoleInit(GFX_TOP, NULL);
    printf("\x1b[31mFatal error\x1b[0m\n\n%s\n\nPress START to exit.\n", msg);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    exit(1);
}

// Dump a screen's displayed framebuffer (BGR8, stored rotated 90 degrees) as a PPM.
static void save_screen(gfxScreen_t screen, const char *path)
{
    u16 fb_w, fb_h;  // fb_w = 240 (screen height), fb_h = screen width
    u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &fb_w, &fb_h);
    FILE *f = fopen(path, "wb");
    if (!f || !fb) { if (f) fclose(f); return; }
    int w = fb_h, h = fb_w;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    u8 *row = malloc(w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const u8 *px = fb + ((x * h) + (h - 1 - y)) * 3;
            row[x * 3 + 0] = px[2];
            row[x * 3 + 1] = px[1];
            row[x * 3 + 2] = px[0];
        }
        fwrite(row, 1, w * 3, f);
    }
    free(row);
    fclose(f);
}

static bool is_new3ds;
static float fps = 60;
// Profiling (ms, smoothed): game update, top / bottom scene building on the CPU, GPU time
static float prof_update, prof_top, prof_bottom, prof_gpu;

static float tick_ms(u64 t) { return t / (float)CPU_TICKS_PER_MSEC; }
static void prof_add(float *acc, float ms) { *acc = *acc * 0.9f + ms * 0.1f; }

void main_draw_perf(float x, float y)
{
    ui_text(x, y, 0.42f, UI_WHITE, "upd %.1f  top %.1f  bot %.1f  gpu %.1f ms  quads %d  tex %d", prof_update, prof_top,
            prof_bottom, prof_gpu, sprite_stat_quads, sprite_stat_switches);
}

// The level's track in practice (rooms outside global.levels are silent).
static int offline_music(void)
{
    for (int i = 0; i < LEVEL_COUNT; i++)
        if (LEVELS[i].room == level.room_id) return maps_music(level.room_id, LEVELS[i].music);
    return -1;
}

static void offline_update(u32 held, u32 down, u32 frame_no)
{
    if (down & KEY_START) {
        menu_enter_title();
        return;
    }
    // SELECT + Y: hidden objects; SELECT + L / R: previous / next room (L / R alone are emotions)
    if ((held & KEY_SELECT) && (down & KEY_Y)) level.show_hidden = !level.show_hidden;
    int spawn = level.has_player ? level.player.character : -1;
    int spawn_exe = level.has_player ? level.player.exe_character : 0;
    int next = (held & KEY_SELECT) && (down & KEY_R) ? 1 : (held & KEY_SELECT) && (down & KEY_L) ? -1 : 0;
    // test: Z<n> goes to the next room every n frames, once round all of them
    int zp = dbg_num('Z', 0);
    if (zp > 0 && frame_no > 0 && frame_no % zp == 0 && frame_no <= (u32)(zp * RM_COUNT)) next = 1;
    if (next) {
        level_load((level.room_id + RM_COUNT + next) % RM_COUNT, -1);
        const RoomInstance *sp = room_instance_find(&level.room, OBJ_SPAWNPOINT, 0);
        if (spawn >= 0 && sp) level_spawn(spawn, spawn_exe, sp->x, sp->y - 18);
        held = down = 0;
    }
    Keys k = level_keys(held, down);
    level_update(&k, held);
    if (dbg_flag('d') && !dbg_flag('q') && level.has_player && frame_no % 30 == 0) {
        const Player *p = &level.player;
        dbg_log("demo f%lu x %.1f y %.1f gspd %.2f xspd %.2f yspd %.2f state %d %s angle %.0f", frame_no, p->x, p->y,
                p->gspd, p->xspd, p->yspd, p->state, p->isGrounded ? "G" : "A", p->angle * 57.2958f);
    }
}

static void offline_draw_bottom(void)
{
    const Player *p = &level.player;
    ui_text(8, 8, 0.6f, UI_WHITE, "%s  (%d/%d)", room_name(level.room_id), level.room_id + 1, RM_COUNT);
    ui_text(8, 28, 0.5f, UI_WHITE, "room %lux%lu  layers %d  instances %d", level.room.width, level.room.height,
            level.room.layer_count, level_count_instances());
    ui_text(8, 44, 0.5f, UI_WHITE, "camera %.0f, %.0f", level.cam_x, level.cam_y);
    ui_text(8, 60, 0.5f, UI_WHITE, "%.0f fps  sheets %d  linear free %.1f MB  %s", fps, sprites_loaded_sheets(),
            sprites_linear_free() / 1048576.0f, is_new3ds ? "New3DS" : "Old3DS");
    main_draw_perf(8, 140);
    if (level.has_player) {
        ui_text(8, 84, 0.5f, UI_WHITE, "x %.1f  y %.1f  state %d", p->x, p->y, p->state);
        ui_text(8, 100, 0.5f, UI_WHITE, "gspd %.2f  xspd %.2f  yspd %.2f", p->gspd, p->xspd, p->yspd);
        ui_text(8, 116, 0.5f, UI_WHITE, "angle %.0f  %s%s%s", p->angle * 57.2958f, p->isGrounded ? "ground " : "air ",
                p->isFlying ? "fly " : "", p->isHiding ? "hidden" : "");
    }
    ui_text(8, 164, 0.45f, UI_WHITE, "%s", level.has_player ? "Move: Circle Pad/D-Pad   A: jump   B: special"
                                                               : "Circle Pad/D-Pad: move camera   B: fast");
    ui_text(8, 180, 0.45f, UI_WHITE, "L / R / ZL: emotions   Y: C");
    ui_text(8, 196, 0.45f, UI_WHITE, "SELECT+L/R: room   SELECT+Y: hidden objects %s", level.show_hidden ? "ON" : "off");
    ui_text(8, 212, 0.45f, UI_WHITE, "START: back to the title");
}

int main(void)
{
    gfxInitDefault();
    // The bottom screen is redrawn only every few frames in game (text is costly on Old 3DS);
    // single-buffered so the last picture stays up in between.
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    romfsInit();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();
    c2d_ready = true;
    {
        size_t n;
        char *cfg = read_file("sdmc:/sonic3ds.cfg", &n);
        if (cfg) {
            log_enabled = true;
            snprintf(dbg_raw, sizeof dbg_raw, "%.*s", (int)n, cfg);
            free(cfg);
            dbg_parse(dbg_raw);
        }
    }
    dbg_log("start: gfx ok, cfg='%s'", dbg_raw);
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    ui_init();

    APT_CheckNew3DS(&is_new3ds);
    if (is_new3ds) osSetSpeedupEnable(true);

    settings_load();
    achiev_init();
    dbg_str('n', settings.nickname, sizeof settings.nickname);
    loader_init();
    audio_init();
    audio_apply_volume(settings.music_volume, settings.sfx_volume);
    if (!net_init()) dbg_log("net_init failed: online play unavailable");
    game_init();
    // m<N>: hold N MB of linear memory to simulate a console with less of it
    if (dbg_flag('m')) {
        void *ballast = linearAlloc((u32)dbg_num('m', 16) * 1024 * 1024);
        dbg_log("ballast %d MB: %s", dbg_num('m', 16), ballast ? "ok" : "failed");
    }
    sprites_init();
    dbg_log("sprites ok (linear free %lu)", linearSpaceFree());
    objects_init();

    char address[64];
    if (dbg_flag('d') || dbg_flag('o') || dbg_flag('v')) {
        int room_id = dbg_num('r', ROOM_GREENHILL);
        if (room_id < 0 || room_id >= RM_COUNT) room_id = ROOM_GREENHILL;
        menu_start_practice(room_id, dbg_flag('v') ? -1 : dbg_num('k', 1));
        if (dbg_flag('w') && level.has_player) level.player.x = dbg_num('w', 0);  // test position
        if (dbg_flag('h') && level.has_player) level.player.y = dbg_num('h', 0);
    } else if (dbg_str('c', address, sizeof address)) {
        menu_connect(address);
    } else {
        menu_enter_title();
    }
    u32 frame_no = 0;
    u64 last_tick = osGetTime();
    NetState last_net = NET_OFF;

    while (aptMainLoop() && app_mode != APP_QUIT) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        if (dbg_flag('d') && app_mode == APP_OFFLINE) {
            u32 quit = down & KEY_START;
            demo_keys(frame_no, &held, &down);
            down |= quit;
        }
        if (dbg_press('A', frame_no)) down |= KEY_A;
        if (dbg_press('B', frame_no)) down |= KEY_B;
        if (dbg_press('U', frame_no)) down |= KEY_DUP;
        if (dbg_press('D', frame_no)) down |= KEY_DDOWN;

        net_poll();
        if (net.state != last_net) {
            dbg_log("net state %d -> %d (players %d)", last_net, net.state, net_player_count());
            last_net = net.state;
        }
        // The match runs while the server is in the game, and through the results screen.
        bool in_match = app_mode == APP_ONLINE &&
                        (net.state == NET_GAME || (net.state == NET_RESULTS && game_active()));
        if (in_match && !game_active()) game_begin();
        if (!in_match && game_active()) game_end();

        u64 t_upd = svcGetSystemTick();
        if (app_mode == APP_OFFLINE) offline_update(held, down, frame_no);
        else if (in_match) game_update(held, down);
        else menu_update(held, down);

        // scr_play_music: each screen / room names its track
        int music = app_mode == APP_OFFLINE ? offline_music() : in_match ? game_music() : menu_music();
        audio_music_request(music);

        u64 t_top = svcGetSystemTick();
        prof_add(&prof_update, tick_ms(t_top - t_upd));
        sprites_frame_begin();
        ui_frame_begin();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        t_top = svcGetSystemTick();

        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);
        sprites_scene(TOP_W, TOP_H);
        if (app_mode == APP_OFFLINE) {
            level_draw();
            mapobj_draw_gui();
        }
        else if (in_match) game_draw_top();
        else menu_draw_top();

        C2D_Flush();
        u64 t_bot = svcGetSystemTick();
        prof_add(&prof_top, tick_ms(t_bot - t_top));
        // Menus are interactive: every frame. In game / practice: every 4th frame.
        bool draw_bottom = !(app_mode == APP_OFFLINE || in_match) || frame_no % 4 == 0 || frame_no < 8;
        if (draw_bottom) {
            C2D_TargetClear(bottom, C2D_Color32(16, 16, 24, 255));
            C2D_SceneBegin(bottom);
            sprites_scene(BOT_W, 240);
            if (app_mode == APP_OFFLINE) offline_draw_bottom();
            else if (in_match) game_draw_bottom();
            else menu_draw_bottom();
            C2D_Flush();
            prof_add(&prof_bottom, tick_ms(svcGetSystemTick() - t_bot));
        }
        C3D_FrameEnd(0);
        prof_add(&prof_gpu, C3D_GetDrawingTime());
        static float worst;
        float frame_ms = tick_ms(svcGetSystemTick() - t_upd);
        if (frame_ms > worst) worst = frame_ms;
        if ((dbg_flag('d') || dbg_flag('P')) && frame_no % 120 == 0)
        {
            dbg_log("perf upd %.2f top %.2f bot %.2f gpu %.2f quads %d tex %d worst %.1f", prof_update, prof_top,
                    prof_bottom, prof_gpu, sprite_stat_quads, sprite_stat_switches, worst);
            worst = 0;
        }
        if (frame_no % 120 == 0) dbg_flush();
        if (dbg_flag('p') && frame_no == (u32)dbg_num('p', 120)) {
            save_screen(GFX_TOP, "sdmc:/sonic3ds_top.ppm");
            save_screen(GFX_BOTTOM, "sdmc:/sonic3ds_bottom.ppm");
            dbg_log("screens saved");
        }
        if (++frame_no <= 3 || frame_no % 600 == 0)
            dbg_log("frame %lu ok, sheets %d, linear free %lu", frame_no, sprites_loaded_sheets(), linearSpaceFree());

        u64 now = osGetTime();
        if (now > last_tick) fps = fps * 0.9f + (1000.0f / (now - last_tick)) * 0.1f;
        last_tick = now;
    }

    net_exit();
    audio_exit();
    loader_exit();
    dbg_flush();
    level_unload();
    sprites_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}

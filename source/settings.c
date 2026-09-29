#include "settings.h"

#include <sys/stat.h>

#define SETTINGS_DIR "sdmc:/3ds/sonic3ds"
#define SETTINGS_PATH SETTINGS_DIR "/settings.ini"

Settings settings;

void settings_load(void)
{
    memset(&settings, 0, sizeof settings);
    snprintf(settings.nickname, sizeof settings.nickname, "3ds player");
    settings.pet = -1;
    settings.gfx_backgrounds = settings.gfx_parallax = settings.gfx_effects = true;
    settings.gfx_weather = settings.gfx_overlays = true;
    // Old 3DS: parallax and weather start off (the costliest layers; 60 fps instead of 30)
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    if (!n3ds) settings.gfx_parallax = settings.gfx_weather = false;
    settings.music_volume = settings.sfx_volume = 10;

    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *key = line, *val = eq + 1;
        if (!strcmp(key, "nickname") && *val) snprintf(settings.nickname, sizeof settings.nickname, "%s", val);
        else if (!strcmp(key, "server")) snprintf(settings.server, sizeof settings.server, "%s", val);
        else if (!strcmp(key, "lobby_icon")) settings.lobby_icon = atoi(val);
        else if (!strcmp(key, "pet")) settings.pet = atoi(val);
        else if (!strcmp(key, "gfx_backgrounds")) settings.gfx_backgrounds = atoi(val);
        else if (!strcmp(key, "gfx_parallax")) settings.gfx_parallax = atoi(val);
        else if (!strcmp(key, "gfx_effects")) settings.gfx_effects = atoi(val);
        else if (!strcmp(key, "gfx_weather")) settings.gfx_weather = atoi(val);
        else if (!strcmp(key, "gfx_overlays")) settings.gfx_overlays = atoi(val);
        else if (!strcmp(key, "show_perf")) settings.show_perf = atoi(val);
        else if (!strcmp(key, "music_volume")) settings.music_volume = atoi(val);
        else if (!strcmp(key, "sfx_volume")) settings.sfx_volume = atoi(val);
    }
    fclose(f);
}

void settings_save(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(SETTINGS_DIR, 0777);
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    fprintf(f, "nickname=%s\nserver=%s\nlobby_icon=%d\npet=%d\n", settings.nickname, settings.server,
            settings.lobby_icon, settings.pet);
    fprintf(f, "gfx_backgrounds=%d\ngfx_parallax=%d\ngfx_effects=%d\ngfx_weather=%d\ngfx_overlays=%d\n",
            settings.gfx_backgrounds, settings.gfx_parallax, settings.gfx_effects, settings.gfx_weather,
            settings.gfx_overlays);
    fprintf(f, "music_volume=%d\nsfx_volume=%d\nshow_perf=%d\n", settings.music_volume, settings.sfx_volume,
            settings.show_perf);
    fclose(f);
}

u64 settings_console_id(void)
{
    static u64 id;
    if (id) return id;
    if (R_SUCCEEDED(cfguInit())) {
        CFGU_GenHashConsoleUnique(0x5D2D, &id);
        cfguExit();
    }
    if (!id) id = 0x3D5D15A57E4ULL;
    return id;
}

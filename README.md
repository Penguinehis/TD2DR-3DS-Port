# Sonic.exe The Disaster 2D Remake — 3DS

Port made by **PenguinEhis**.

Homebrew port of the GameMaker project in `../disaster2d-oss-main`. It plays online on the same
servers as the PC game (the protocol is identical). See `PLAN.md` for the roadmap and design
notes.

## Build

Needs Python 3 with Pillow (`pip install Pillow`) and Docker Desktop. No devkitPro install is
needed; the build runs in the official `devkitpro/devkitarm` image.

```
build.bat            export sprites/rooms/tables + build
build.bat noexport   build only (after changing C code)
```

Audio is converted separately (it needs ffmpeg, provided by a small Docker image), and only
when the sounds change:

```
docker build -t sonic3ds-tools tools/docker
docker run --rm -v "%cd%:/port" -v "%cd%\..\disaster2d-oss-main:/gm" -e GM_PROJECT=/gm -w /port sonic3ds-tools python3 tools/export_audio.py
```

Output: `sonic3ds.3dsx` (assets are inside it).

CIA (installable to the HOME menu; also gives the game 80 MB of memory on Old 3DS):

```
docker build -t sonic3ds-cia tools/docker/cia
docker run --rm -v "%cd%:/project" -w /project sonic3ds-cia sh tools/cia/build_cia.sh
```

Output: `sonic3ds.cia` (install with FBI).

## Run

- **3DS:** copy `sonic3ds.3dsx` to `sd:/3ds/` and start it from the Homebrew Launcher. For
  sound, the console needs `sd:/3ds/dspfirm.cdc` (run the DSP1 homebrew once to dump it).
- **Emulator:** open `sonic3ds.3dsx` in [Azahar](https://azahar-emu.org/) with the **Vulkan**
  renderer (Emulation > Configure > Graphics); on this PC's AMD driver the OpenGL renderer
  hangs. Azahar also wants a `3ds/dspfirm.cdc` file in its SD folder for sound (any content).

Settings (nickname, server, lobby icon, pet, graphics, volumes) are saved in
`sd:/3ds/sonic3ds/settings.ini`; achievements, mercoins and shop unlocks in
`sd:/3ds/sonic3ds/achievements.bin`.

## Controls

| Button | In game | Menus |
|---|---|---|
| Circle Pad / D-Pad | move, look up/down | choose |
| A | jump (and the character's air ability) | confirm / ready |
| B | special attack | back (lobby: twice to leave) |
| Y | the C ability (EXE invisibility, Exeller clones, ...) | |
| X | chat | chat |
| L / R / ZL-ZR | emotions 1 / 2 / 3 | |
| START | | title: quit |

**Title menu:** Play online (asks for the server address the first time), change server or
nickname, offline practice with any of the ten characters (Left/Right picks one),
achievements (the 50 from the PC game, earned online), shop (spend mercoins on lobby icons,
EXE taunts and pets; L/R switch tabs), settings, level viewer.

**Settings:** turn off backgrounds, parallax, effects, weather or screen overlays to keep 60 fps
in a full lobby (mostly needed on Old 3DS), and set the music / effects volume.

**Offline practice:** SELECT + L/R changes the room, SELECT + Y shows invisible objects,
START returns to the title. The bottom screen shows position, speed, FPS and memory.

## Debugging

The game reads `sdmc:/sonic3ds.cfg` (in Azahar: `<user dir>\sdmc\sonic3ds.cfg`):

| Switch | Effect |
|---|---|
| `b` `a` `i` `s` `t` | skip backgrounds / level art / instances / assets / text |
| `p<N>` | at frame N (default 120) save both screens to `sdmc:/sonic3ds_top.ppm` / `_bottom.ppm` |
| `o` / `v` | start in offline practice / the level viewer |
| `k<N>` | practice character: 1-6 Tails, Knuckles, Eggman, Amy, Cream, Sally; 7-10 Exe, Chaos, Exetior, Exeller |
| `r<N>` | practice room N |
| `d` | practice with scripted input and a position log every 30 frames |
| `c<address>;` | connect to a server at startup, e.g. `c192.168.1.5:8606;` |
| `n<name>;` | nickname |
| `x` | auto-play the online menus (ready, vote, first free character) |
| `A<N>` `B<N>` | press A / B at frame N |

It writes a log to `sdmc:/sonic3ds.log`. `python tools/emu_shot.py <sdmc dir> out.png` turns
the screen dumps into one PNG. Errors are shown on the top screen (not the system error
applet, which hangs in emulators without system files).

For online tests without a second console, `tools/netbot/bot.c` is a headless client that
joins, readies, votes, picks a character and walks around (see PLAN.md, "Testing online").

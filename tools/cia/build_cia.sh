#!/bin/sh
# Package sonic3ds.elf + romfs as sonic3ds.cia. Run after the normal build, in the CIA tools image:
#   docker build -t sonic3ds-cia tools/docker/cia
#   docker run --rm -v "<sonic3ds>:/project" -w /project sonic3ds-cia sh tools/cia/build_cia.sh
set -e
mkdir -p build/cia
bannertool makebanner -i tools/cia/banner.png -a tools/cia/banner.wav -o build/cia/banner.bnr
bannertool makesmdh -s "Sonic.exe The Disaster 2D" -l "Sonic.exe: The Disaster 2D Remake (3DS port)" \
    -p "PenguinEhis" -i icon.png -o build/cia/icon.smdh
makerom -f cia -o sonic3ds.cia -elf sonic3ds.elf -rsf tools/cia/sonic3ds.rsf \
    -icon build/cia/icon.smdh -banner build/cia/banner.bnr -target t -exefslogo
echo "built sonic3ds.cia"

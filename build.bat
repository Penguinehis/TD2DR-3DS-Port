@echo off
rem Export assets from the GameMaker project, then build sonic3ds.3dsx in the devkitPro Docker image.
rem Requires Python (with Pillow) and Docker Desktop. Pass "noexport" to skip the asset step.
setlocal
cd /d "%~dp0"

if /i not "%1"=="noexport" (
    python tools\export_sprites.py || exit /b 1
    python tools\export_rooms.py || exit /b 1
    python tools\export_tables.py || exit /b 1
)

docker run --rm -v "%cd%:/project" -w /project devkitpro/devkitarm make -j8 || exit /b 1
echo.
echo Built %cd%\sonic3ds.3dsx

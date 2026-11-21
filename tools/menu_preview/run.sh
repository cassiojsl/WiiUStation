#!/bin/sh
# Renders WiiUStation's menu screens on a PC (SDL2 dummy driver) and saves
# PNG screenshots, using scripted input. Needs libsdl2-dev, libsdl2-ttf-dev,
# libsdl2-image-dev and the Inter font. Put test files under ./sd.
set -e
D=$(cd "$(dirname "$0")" && pwd)
S=$D/../../source
mkdir -p "$D/sd/wiiu/apps/WiiUStation/games" "$D/out"
gcc -O1 -o "$D/host" "$D/host.c" -I"$S" -DSDROOT="\"$D/sd\"" \
    -DCONTENT="\"$D/../../content\"" $(pkg-config --cflags --libs sdl2 SDL2_ttf SDL2_image) -lm
for m in main ingame grid list settings gamesettings controls slots cheats resume credits loading; do
  SDL_VIDEODRIVER=dummy "$D/host" $m "$D/out/$m.png"
done
echo "screenshots in $D/out"

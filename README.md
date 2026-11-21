# WiiUStation

Standalone PlayStation emulator for the Wii U (Aroma `.wuhb`). It uses
[PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) as its core. The core
is linked into the app through the libretro API, so RetroArch is not needed.
The menu, video, controllers and saves are WiiUStation's own.

## Install

1. Copy `WiiUStation.wuhb` to `sd:/wiiu/apps/`.
2. Put your games in `sd:/wiisxrx/isos/`. Supported formats: `.cue/.bin`,
   `.chd`, `.pbp`, `.iso`, `.img`, `.m3u` (multi-disc) and more.
3. Optional: put a BIOS (`scph1001.bin`, `scph5501.bin`, ...) in
   `sd:/wiisxrx/bios/`. Without one, the built-in HLE BIOS is used.

Memory cards are stored in `sd:/wiisxrx/saves/<game>.srm`, save states in
`sd:/wiisxrx/savestates/`, and settings in `sd:/wiisxrx/wiiustation.cfg`.

## Controls

GamePad, Pro Controller, Classic Controller and Wii Remote (sideways).
Players are assigned in this order: GamePad first, then the other controllers.

| Wii U | PlayStation |
|---|---|
| A / B / X / Y | ○ / ✕ / △ / □ |
| L / R / ZL / ZR | L1 / R1 / L2 / R2 |
| Sticks, stick clicks | DualShock analog, L3 / R3 |
| + / − | Start / Select |

In a game, **HOME**, **L3 + R3** or **touching the GamePad** opens the menu.
In the menu, HOME opens the Wii U HOME Menu.

## Use of the Wii U hardware

- **CPU core 1** runs the PlayStation CPU with **Lightrec**, which recompiles
  MIPS code into PowerPC code. The generated code lives in Cafe OS's codegen
  (JIT) area, which is either writable or executable (W^X). Every write by
  Lightrec is wrapped in RW→RX switches (see `patches/`).
- **Core 2** runs the GPU renderer (gpulib + gpu_unai, threaded).
- **Core 0** runs SPU mixing and CD-ROM prefetch.
- **GX2** scales the picture to the TV (720p/1080p) and the GamePad at the
  same time, with bilinear or sharp filtering, 4:3, stretch or integer
  scaling, and optional scanlines.

If the hosting title has no codegen area, Lightrec is disabled and the
interpreter is used. Settings → CPU core shows which one is active.

## Building

```bash
git submodule update --init --recursive
make -C WiiUStation
```

This needs devkitPro with `wiiu-dev`, `wiiu-sdl2` and `wiiu-sdl2_ttf`. The
Makefile applies `patches/*.patch` to `vendor/pcsx_rearmed` and builds the
core (`platform=wiiu`, Lightrec, threaded GPU/SPU, gpu_unai), then links the
frontend.

The patches do three things:
- point Lightrec's code buffer at the codegen area and add the W^X hooks;
- enable the native Wii U threads in libretro-common, pinned to cores 0 and 2;
- fix a big-endian build error in gpulib's threaded renderer.

## Licence

GPL-2.0-or-later, the same as PCSX-ReARMed.

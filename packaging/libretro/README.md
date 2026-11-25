# NeoST for RetroArch

NeoST emulates the Atari ST, STE, Mega ST and Mega STE with a cycle-exact 68000
and chips modelled against the silicon. This core is **experimental**: it is
built and tested by NeoST's own CI, not yet distributed by libretro.

## Build and install

```sh
cmake -B build-libretro -DCMAKE_BUILD_TYPE=Release -DNEOST_PIC=ON -DNEOST_BUILD_LIBRETRO=ON
cmake --build build-libretro --target neost_libretro -j
```

Copy `neost_libretro.dylib` (macOS), `.so` (Linux) or `.dll` (Windows) into
RetroArch's **Cores** directory and `neost_libretro.info` into **Core Info**
(**Settings → Directory** lists both).

## TOS

In RetroArch's **System/BIOS** directory, create a folder named `neost` and put
your TOS images there. EmuTOS is free: `etos192us.img` or `etos192uk.img` for
an ST, `etos256us.img` for an STE. Any Atari TOS image you own works too. With
**Core Options → System → TOS** on *Automatic*, NeoST picks the TOS the machine
shipped with when you have it (PAL before NTSC), otherwise EmuTOS. A real TOS
runs more games than EmuTOS: Stunt Car Racer, for one, needs it.

The TOS also sets the video standard: a US TOS runs at 60 Hz, a European one at
50 Hz. Most ST games were written for 50 Hz.

## Drive sounds

Copy NeoST's `roms/drivesound/epson_smd480l` folder to `system/neost/drivesound`
to hear the drive: motor, head steps and seeks, recorded from a real Epson
SMD-480L drive by Stefan jL (from STeem SSE, freeware; keep this credit when
you pass the samples on). **Media → Drive sounds** turns them off and
**Drive sound volume** sets their level. The drive stays silent while it is
empty, and with run-ahead each step is heard once.

## Content

| File | What happens |
|---|---|
| `.st`, `.msa`, `.dim`, `.stx` (Pasti), `.ipf` | Inserted in drive A. Other disks of the same game in the same folder (`Disk 1 of 3`, `_a`/`_b`) join a playlist. |
| `.m3u` | A playlist, one disk per line; `disk.st|Label` names it. |
| `.prg`, `.tos`, `.app`, `.gtp`, `.ttp` | Its folder becomes drive C:, and the program starts by itself. |
| `.hd`, `.vhd` | A hard disk image on ACSI target 0, copied once to `saves/neost/harddisks/`. EmuTOS mounts its partitions by itself; an Atari TOS needs a driver on the disk. |
| `.stc` | A cartridge. |
| `.zip`, `.gz` | Unpacked once into `saves/neost/archives/`. Its own `.m3u`, else its floppies as a playlist, else its program (a zipped GEMDOS game) on C:. Files already unpacked are never overwritten, so game saves survive. |

IPF images need no extra library: NeoST has its own decoder.

**Your disks are never written.** Each floppy is copied once to
`saves/neost/disks/` in RetroArch's save directory, and the game saves there.
Delete that copy to start afresh.

## What NeoST does for you

- **Adaptive framing.** The picture shows the active screen, without the
  borders a game never draws in, and opens to the borders as soon as a demo
  draws there. The aspect ratio stays the same when the resolution changes.
  **Video → Framing** also offers the active area alone, or the full frame.
- **The right machine.** With **System → Machine** on *Automatic*, content
  tagged `(STE)` starts on an STE. A program that touches STE or Mega STE
  hardware on an ST without expecting it to be missing restarts on the machine
  it needs, and a notification says so.
- **Programs on non-booting floppies start by themselves.** Many game disks
  do not boot: you double-click the game on the desktop. When such a disk
  holds a single game program, NeoST starts it for you
  (**Media → Start the program**).
- **Fast floppy loading.** While the floppy controller is busy with a disk,
  and no key or button is held, NeoST runs up to eight Atari frames per host
  frame. The emulation stays exact; only the waiting is shorter (a 330 KB
  program loads about twice as fast).
- **Cheats and memory tools** see the ST-RAM at address 0, big-endian as on the
  68000, even after an automatic restart on another machine.
- **Exact states.** A save state weighs 2 to 4 MB with its floppies, takes
  under a millisecond on a recent computer, and replays exactly: run-ahead,
  rewind and netplay work. It also carries what a game wrote on a protected
  (STX or IPF) floppy, so a high score saved on the disk rewinds with the game.

## Controls

| RetroPad | Atari |
|---|---|
| D-pad, left stick | Joystick (port 1) |
| B | Fire |
| A | Joystick up, to jump (**Input → Button A**: up, Space or Fire) |
| Y, Start | Space |
| X | Return |
| Select | Esc |
| L, R | F1, F2 |
| Right stick | Mouse |
| L2, R2 | Mouse buttons |
| R3 | Next disk of the playlist |

The second RetroPad is joystick port 0. The keyboard and the host mouse work
too; turn on **Game Focus** (Scroll Lock) if RetroArch's hotkeys take your keys.
Help is F11 or Print Screen, Undo is F12 or End. Keys follow their position, as on a real ST:
pick the TOS of your keyboard's country (EmuTOS FR for an AZERTY keyboard) and the
letters and digits type what they show.

**Netplay.** RetroArch's netplay sends the state of controller ports, never
typed keys. Set **Port 3 → Keyboard (for netplay)** for games played at the
keyboard and **Port 4 → Mouse (for netplay)** for the mouse; both players need
the same content and the same core options.

A player who joins takes the host's state and stays in step, even though each
keeps the floppies in their own save folder: NeoST finds its own copy of a disk
by its name. Both need the same disk image under the same file name.

**Touch screens.** With **Input → Touch moves the GEM pointer** (on by default
on Android), a tap on the GEM desktop or in a GEM program puts the mouse pointer
there and clicks; a double tap opens. Games keep the relative mouse.

**Language.** The core options appear in French when RetroArch is in French.

## Shaders

The `shaders` folder carries the CRT pass of the NeoST desktop as RetroArch
slang shaders: scanlines, slight curvature, phosphor persistence, shadow mask,
glow and analog RGB cable.

1. Copy `shaders` into RetroArch's **Shaders** directory. This adds
   `shaders_slang/neost/` and `presets/NeoST/NeoST.slangp`.
2. Turn on **Settings → Video → Shaders → Video Shaders** once.

RetroArch then loads `presets/NeoST/NeoST.slangp` by itself for NeoST. It
follows the picture: the SC1224 colour monitor for colour modes, the SM124 for
monochrome. Other presets in `shaders_slang/neost`:

| Preset | Look |
|---|---|
| `neost-auto.slangp` | The monitor follows the picture (the default) |
| `neost-sc1224.slangp` | SC1224 colour monitor, dot mask and scanlines |
| `neost-sm124.slangp` | SM124 monochrome monitor, white text with a slight glow |
| `neost-peritel.slangp` | A colour TV fed RGB through a Péritel (SCART) lead |

Every setting can be changed in **Quick Menu → Shaders → Shader Parameters**.
The shaders need the Vulkan, Metal, Direct3D 11/12 or GL core video driver.
On macOS, install RetroArch's Metal build (`brew install --cask retroarch-metal`:
the plain `retroarch` cask is Intel-only and OpenGL-only, without slang shaders)
and keep its default **Vulkan** video driver: with the **Metal** driver, RetroArch
1.22.2 runs very slowly unless the trackpad button is held, whatever the core.
The persistence counts presented frames: for a 50 Hz game on a 60 Hz screen,
turn on **Sync to Exact Content Framerate** or use a variable refresh display.

The module uses the MIT-licensed `libretro.h` from libretro-common.

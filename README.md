<div align="center">

<img src=".github/assets/icon.png" width="132" alt="D.Smile icon" />

# D.Smile

**VTech V.Smile emulator - for Nintendo Switch and Android.**

<img src="https://img.shields.io/badge/platform-Nintendo%20Switch-e60012?style=flat-square" alt="Nintendo Switch" />
<img src="https://img.shields.io/badge/platform-Android%208.0%2B-3ddc84?style=flat-square" alt="Android 8.0+" />
<img src="https://img.shields.io/badge/renderer-OpenGL%20%2F%20OpenGL%20ES-blue?style=flat-square" alt="OpenGL / OpenGL ES" />
<img src="https://img.shields.io/badge/controllers-gamepad%20%2B%20touch-orange?style=flat-square" alt="gamepad and touch" />

</div>

This is a fork of [derik-dot-digital/D.Smile](https://github.com/derik-dot-digital/D.Smile),
the original Android-only D.Smile. All credit for the Android app and the
V.Smile emulation core itself belongs there; this fork's own work is the
native Nintendo Switch homebrew port added on top (`switch/`), plus whatever
else changes here from now on - and that Switch port is this repo's main
focus going forward, so it's what the rest of this README leads with. Both
frontends share the same C++ V.Smile emulation core
(`app/src/main/cpp/core`) - feature parity isn't the goal, each frontend
leans into what its platform does well, but the underlying hardware
emulation is identical code either way.

## Switch (NX) port

A native Nintendo Switch homebrew build lives in [`switch/`](switch/), built
against devkitPro/libnx and packaged as a single `dsmile_switch.nro` - no
Android runtime, no APK, just the same V.Smile core running under its own
menu chrome built for a gamepad and a TV instead of a touchscreen.

**Play**
- Same core, same cartridge compatibility - runs the same `.bin` dumps as
  the Android app, BIOS optional.
- Save states (3 slots per game) and Rewind.
- Cart-side NVRAM saves for the titles that use onboard save memory.

**Look**
- Pixel, sharp, and CRT display modes (curvature, glow, scanlines, mask,
  vignette, each independently adjustable), 4:3/stretch/integer aspect,
  themed letterbox backgrounds and TV bezels - the same options as
  Android's Look features, in a menu built around a controller instead of
  touch.
- Six menu themes, including a default theme built around the V.Smile
  console/controller's own purple-and-orange color scheme.

**Control**
- Full gamepad remapping and hotkeys (save/load state, rewind, menu) via
  Settings > Controller.
- Two-player support - see [switch/README.md](switch/README.md) for the
  current status and a known limitation around controller crosstalk.

### Getting started (Switch)

1. Copy `dsmile_switch.nro` to `sdmc:/switch/dsmile/dsmile_switch.nro` on
   your SD card.
2. Drop `.bin` cartridge dumps into `sdmc:/switch/dsmile/games/`
   (optionally with a same-named `.png` next to any of them for cover art).
3. Launch it from hbmenu or your homebrew loader of choice.

A BIOS is optional here too: drop `.bin` BIOS dumps into
`sdmc:/switch/dsmile/bios/` and pick one, plus the region/language, from
Settings > Library & Storage.

See [switch/README.md](switch/README.md) for full build instructions,
current status, and everything that's actually been confirmed on real
hardware so far.

## Android app

The Android app this was forked from is still here (`app/`) and still
builds and works the same as it always did - see
[derik-dot-digital/D.Smile](https://github.com/derik-dot-digital/D.Smile)
for its own history. Kept brief here since the Switch port above is this
repo's focus now.

**Features**
- Runs V.Smile cartridge dumps straight away, no BIOS required (optional
  system BIOS import for extra compatibility).
- Save states with thumbnails and timestamps, per-game slot memory, Rewind
  and fast forward.
- CRT shader, pixel/sharp/CRT display modes, 4:3/stretch/integer aspect,
  themed backgrounds and TV bezels, two renderer options.
- An on-screen controller modelled on the real thing (with a full layout
  editor) plus gamepad support with remappable buttons and hotkeys.
- Launches directly into a game from front ends like iiSU, Daijisho and
  ES-DE - see [docs/iisu-integration.md](docs/iisu-integration.md).

### Getting started (Android)

1. Grab the latest `D.Smile-x.y.z.apk` from [Releases](../../releases) and install it (allow installs from your browser if prompted).
2. Open D.Smile, tap **ROM folder**, and pick the folder with your `.bin` dumps.
3. Tap a game. The menu button in the top corner opens save states, video options, the layout editor and more.

A BIOS is **not required**. Games boot and play fine without one. Importing a V.Smile system ROM (tap **BIOS**) is purely optional and only adds a bit of extra compatibility that can help a handful of games. Skip it unless you run into a title that misbehaves.

## Acknowledgements

This project wouldn't exist without the V.Smile reverse engineering work that came before it:

- **[derik-dot-digital/D.Smile](https://github.com/derik-dot-digital/D.Smile)**
  — the original Android D.Smile this repo is forked from. The Android app
  and the entire V.Smile emulation core are their work; this fork's own
  contribution starts at the Switch port in `switch/`.
- **[veesem](https://github.com/sp1187/veesem)** by sp1187 (ISC license) — the
  primary behavioral reference for this emulator. D.Smile's core was verified
  against veesem instruction-by-instruction during development, and its
  hardware model (unSP CPU, PPU, SPU, controller handshake) taught this
  project most of what it knows.
- **[MAME](https://github.com/mamedev/mame)** — the SPG2xx and V.Smile drivers
  (Ryan Holtz, Vas Crabb, and contributors) documented the hardware registers,
  timings and controller protocol that both veesem and D.Smile build on. The
  accurate-render fade/saturation formulas follow MAME's implementation.
- **[V.Frown](https://github.com/Schnert0/VFrown)** by Schnert0 — a further
  reference for controller timing and hardware behavior.
- **[Oboe](https://github.com/google/oboe)** by Google (Apache-2.0) — the
  low-latency Android audio library D.Smile ships with.
- **[NaGaa95](https://github.com/NaGaa95)**, author of
  [DrasticDS_nx](https://github.com/NaGaa95/DrasticDS_nx) and
  [NetherSX2_nx](https://github.com/NaGaa95/NetherSX2_nx) — the Switch port's
  emulator frontend and libnx-side handling (input/controller pairing,
  audio, rendering glue, settings and menu UI patterns) were built studying
  those two projects as working references for wrapping an existing
  emulator core in a native Switch homebrew shell. At least one concrete fix
  (dropping a broken controller-pairing applet for automatic detection) came
  directly from checking how NetherSX2_nx handled the same problem.
- The V.Smile research and homebrew community, whose compatibility testing and
  hardware documentation made a project like this possible.

Where D.Smile's behavior diverges from these projects (controller reporting
modes, held-input handling, sprite DMA edge cases), the changes are original
findings — documented in the commit history so they can flow back upstream.

## License

Personal project, forked from [derik-dot-digital/D.Smile](https://github.com/derik-dot-digital/D.Smile).
Switch-port code written for this fork, informed by the references above.
Not affiliated with or endorsed by VTech.

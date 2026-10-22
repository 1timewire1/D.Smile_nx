# D.Smile - Switch port (bring-up)

**1.2.0** (`Makefile` `APP_VERSION`). Everything since 1.1.0, each detailed
in its own section below:
- **Performance:** the emulator core is 25-60% faster per frame, with output
  bit-identical to before. At stock clocks Toy Story 3, Alphabet Park, Cars 2
  and Shrek hold 60 fps (Action Mania ~59), up from ~40-43. See "Performance
  pass (1.2.0)".
- **Audio:** the "audio dies until reset" bug is fixed, and the output rate
  now tracks the queue, so steady games no longer click. See "Audio: the
  dying bug, and rate control (1.2.0)".
- **Frame skip:** Auto now reacts to the cost of shaders and the Accurate
  renderer too, and skipped frames are paced (Manual skip used to speed fast
  games up). See "Frame skip: Auto counts drawing, skipped frames are paced
  (1.2.0)".
- **Performance overlay:** Settings > Graphics > Performance shows timings,
  and can log every frame to a CSV for bug reports.
- **Controllers:** mashing buttons and the stick no longer stalls the
  joystick link; Motion controllers keep 1.1.0's link handling. See
  "Controller link: the lost RTS edge".
- **Carts:** German and French Shrek Forever After and Cars 2, and German and
  Spanish Toy Story 3, now boot. See "Carts with swapped ROM halves".
- **BIOS:** the Spanish Motion BIOS's test screen (v7.05) works, its region
  codes are labelled from that test screen, and it shows as "Motion (Spanish
  unit, v7.05)". See "BIOS test screen on the Spanish Motion BIOS".

**1.1.0**. Everything since 1.0.1, each detailed in its own section below:
- **Peripherals:** the Smart Keyboard (USB keyboard), the V.Smile Motion
  (tilt/swing/shake from the Switch controller's sensors, one or two
  players), and the Jammin' Gym Class dance mat.
- **Joystick:** analog stick levels.
- **Power:** real standby, with Console ON/OFF/RESTART bindings, Reset Game
  as RESTART, and Quit as a power-off that's kept for the next launch.
- **Console options:** PAL/NTSC, the VTech-logo jumper, a controller LED
  overlay, BIOS-only boot and the BIOS test screen, and the corrected
  Motion console type.
- **Save states:** they now carry the console's configuration.

**Prepping for a 1.0.0 release** - not code-untested, still needs the
two-player work above to get a hardware round, but the theme/branding
work in "1.0.0 release prep" below is done. Build's `.nro` metadata now
reads `D.Smile NX` by `SenseiFromClubPenguin`, version `1.0.0`. Also did a
documentation-only pass on several other V.Smile peripherals this port
doesn't support (microphone, Smartbook, PC Pal's keyboard/mouse/
digitizer, V.Link, plus a Jammin' Gym Class dance mat finding, since
corrected and implemented) - see "Documented for later: other V.Smile
peripherals" below; nothing here changes any actual behavior.

**This pass, part 7** (hardware-tested fix + save-state change): turning the
console off with the OFF binding and then choosing Quit woke it back up
(intro and all) before quitting. Fixed, and save states now keep the console
configuration they were made on. See "Power buttons and standby" > "Fixes
after hardware testing".

**This pass, part 6**: power buttons and real standby. Console ON / OFF /
RESTART are bindable. Reset Game presses RESTART. Quit turns the console off
(the goodbye plays) and keeps its standby state, so the next launch resumes
as if the cartridge never left. See "Power buttons and standby" below.
Checked on the harness, **hardware-untested**.

**This pass, part 5**: the console-type straps and the US Motion BIOS. See
"Console type straps, and why the US Motion BIOS shows the V.Smile intro"
below. Checked on the harness, **hardware-untested**.

**This pass, part 4**: veesem's remaining console options. Settings >
Library & Storage gets **Video Timing** (NTSC 60 Hz / PAL 50 Hz) and **VTech
Logo** (the jumper that skips it). Settings > Graphics gets **Controller LEDs**.
Checked on the harness, **hardware-untested**. See "New this pass: veesem's
options (PAL, logo jumper, LEDs)" below.

**This pass, part 3**: three quick items, each checked on the desktop harness,
**hardware-untested**: the Jammin' Gym Class dance mat as a controller type,
analog stick levels, and booting the BIOS on its own, including its hidden
test screen. See "New this pass: dance mat, stick levels, BIOS test screen"
below.

**This pass, part 2**: V.Smile Motion controller support. Motion carts now
offer their Standard/Motion choice, and tilting the Switch controller steers,
tested on PC with Action Mania. See "New this pass: V.Smile Motion controller
(tilt)" below. **Hardware-untested.**

**This pass**: V.Smile Smart Keyboard support using a USB keyboard
plugged into the Switch (Settings > Controller > Controller Type).
**Hardware-confirmed**: keyboard detected, login-screen typing and the
Enter/Esc/F1 keys all work. Follow-up in the same pass (hardware-untested):
the keyboard's colour buttons and ABC (F5-F9), a Spanish layout, the
language setting renamed "Keyboard Layout", and the French/German/Spanish
carts checked on PC. See "New this pass: Smart Keyboard (USB keyboard)"
below.

**Previous pass**: separate button bindings for player 2 and BIOS version
detection (see "New in the previous pass: player 2 bindings and BIOS
version detection" below). Hardware-confirmed working.

Status: the original bring-up, the ROM browser/BIOS pass, the menu redesign,
Graphics settings, Controller settings, the settings-screen scrolling fix,
the in-game pause menu, save states, the -O3/LTO/frame-skip performance
pass, Rewind, the BIOS picker, and the Region setting have all been run on
real hardware and are working (frame skip specifically confirmed to
meaningfully help stock, non-overclocked performance; Rewind confirmed to
work perfectly; Region's language labels confirmed to match what each code
actually boots to, against the real dumps in `reference/bios/` - see
"Region mapping" below). Fast Forward was also built and hardware-tested,
found to barely do anything, and has been removed outright rather than
kept as a feature that doesn't actually deliver - see "Fast Forward: tried
and removed" below for why.

The BIOS/Region main-menu lock and the audio self-heal (previous pass) have
both been accepted after hardware testing - the self-heal specifically
confirmed working as intended: a dropout now recovers on its own after a
brief silence instead of staying dead until a full relaunch.

**This pass**: investigated MAME's V.Smile Motion driver looking for wand/
motion-controller support to port over, the way Region's mapping was found
in MAME's source last pass - found MAME doesn't have it either (see
"V.Smile Motion investigation" below). That same investigation turned up
two other gaps against MAME that *were* concretely portable: two-player
controller support, and cart-side NVRAM saves for the one V.Smile title
(and its regional variants) that has onboard save memory. A third gap -
ABC/Smart Keyboard accessory support - is deliberately left as a
documented idea only for now; see its own note below for why.

**Two-player controller support, four hardware rounds later**: single-
player input, connection detection, and cart-side NVRAM are all confirmed
working. Along the way, three real bugs were found and fixed - an
in-game-input regression (`UartRxDone()`), a controller-pairing applet
that never actually displayed (`LibnxError_LibAppletBadExit`, replaced
with an automatic-detection approach with no applet at all, found by
checking `reference/NetherSX2_nx-main`, a second homebrew emulator with
working multiplayer, at the user's suggestion), and a Joy-Con-pair-
splitting regression from the first attempt at fixing what looked like a
fourth bug. **That fourth "bug" - either controller driving both players -
turned out not to be a bug at all**: the game used to test it is a
turn-taking title that deliberately accepts input from either controller.
Retested with games that have proper simultaneous two-player, both
controllers stay cleanly separated, and with only one controller
connected those two-player modes are correctly gated out. Two-player
support works as intended; see "Two-player controller support" below for
the full history. Testing also confirmed that V.Smile Art Studio is fully
playable with the standard controller - the drawing pad itself isn't
emulated yet (see its own section below).

## Performance pass (1.2.0)

**Measured first.** Settings > Graphics > Performance got a "Shown + log" mode that writes every
frame's timings to a CSV (below), and two builds were compared on hardware at stock clocks
(1020 MHz CPU, handheld). The baseline spent 19-21 ms per frame in `VSmile::RunFrame()` alone,
over the 16.7 ms budget: 40-43 fps.

**Where the time went** (desktop profile, the same code): the CPU interpreter about half; the
per-instruction upkeep of the other devices (PPU, SPU, timers, UART, ADC, controller ports, each
checked after every instruction) about a fifth; PPU line drawing and SPU sample generation the
rest. Every game runs ~100,000 instructions a frame - when a game's work is done it spins in a
wait loop until the next interrupt, so the cost per frame barely depends on the game.

**Changes, all in the shared core (`app/src/main/cpp/core`), all bit-exact:**
1. **Inline fast paths** for the per-instruction device ticks (`Ppu::RunCycles`,
   `Spu::RunCycles`, `TickTimers`, `TickUart`, `TickAdc`): the "nothing due" case is inline, the
   rest out of line.
2. **Next-event scheduling.** The controller ports first (`MachineIo::NextEvent`,
   `Spg200::TickIo`), then every device (`Spg200::RunDevices`): cycles are banked and the devices
   run only when the nearest deadline is reached, as an event-free lump plus the last instruction
   on its own. That split is what keeps it exact: a counter running out can change what the rest
   of the same call charges (the controller's RTS re-arm restarts its retry counter, for one).
   Any read or write of a device register runs the banked cycles first.
3. **Read maps:** a pointer per 1K-word page (RAM, cart, BIOS, Art Studio NVRAM), one map per CS2
   bank, so a bus read is one lookup instead of a chain of address decodes.
4. **Fetch:** PC increments without rewriting SR every instruction; the interrupt check is
   skipped when no line is raised.
5. **Wait-loop replay** (`Spg200::IdleLoop`): after a short backward jump, one pass of the loop
   is recorded (the CPU state before each instruction, and its cycles). If the pass changed no
   memory (rewriting a value already there is allowed), read no device register, took no
   interrupt and came back to its starting state, later passes are identical until an interrupt
   (devices never write RAM). So they're replayed without running the CPU, the cycles fed to the
   devices as usual, and the CPU gets the exact boundary state back when an interrupt is due.
   Alphabet Park spent 80% of its instructions in such a loop; Toy Story 3 about 10%.
6. Tried and dropped: decoding whole tile rows at once in the PPU (no measurable gain).

**Verification.** `test/core_digest.cpp` hashes every frame's picture, every audio sample and
the final save state of a run. The optimized core matched the original on every ROM in
`reference/roms` (122), and on a suite covering the joystick, Motion (including random full-range
tilt every frame), two players, the Smart Keyboard, PAL and standby. The checker caught two
scheduling bugs along the way (a deadline not shortened after a sync, and the event-free-lump rule
above), both fixed before any hardware build.

**Results on hardware** (stock clocks, from the logs):

| Game | Before | After | `RunFrame()` before / after |
|---|---|---|---|
| Action Mania | 39.7 fps | 58.4-59.8 fps | 20.6 / 14.5 ms |
| Alphabet Park Adventure | 43.2 fps | 60.0 fps | 19.3 / 7.0 ms |
| Toy Story 3 | 42.4 fps | 60.0 fps | 19.5 / 13.3-14.1 ms |

Cars 2 held 60 fps and Shrek: Dragon's Tale 59-60 fps even with the Accurate renderer and SABR or
5xBR. Desktop: 25-35% less time per frame for busy games, 45-50% for wait-heavy ones.

**Front end:**
- The performance overlay first cost 2 ms a frame itself (`switch_ui` draws each glyph as its
  own quad). Its text is now drawn into a texture only when it changes: 0.14 ms.
- The game picture uploads into one of three textures in turn, so the CPU never writes one the
  GPU may still be drawing from.

**Performance overlay and log** (Settings > Graphics > Performance: Hidden / Shown / Shown +
log): `Emu` is `RunFrame()` (average, and the worst frame), `Snap` the rewind snapshot, `Draw` the
GL work, then frames shown per second (and, while frame skip is active, the skip level and the
emulated rate). "Shown + log" writes `sdmc:/switch/dsmile/perf/<game>_<time>.csv`:
- a header with the build (NRO path), game, BIOS, graphics settings and CPU/GPU/memory clocks;
- every 5 s, a line with the clocks, the audio counters and the frame-skip level;
- one row per frame: `emu, snap, upload, render, overlay, swap, loop`, in ms.

A second overlay line shows the log's file name, or why it couldn't be opened.

**Deferred: PGO.** Profile-guided optimization would likely gain another 10-20% for the
interpreter, but a profile goes stale as the core changes, and the accuracy work next will change
it. Better done right before a release that's about speed again.

## Audio: the dying bug, and rate control (1.2.0)

**The bug.** Each frame's audio went into one of four wavebufs in turn, after checking that the
next one had finished playing. Their states only change on `audrvUpdate()`, and the only call came
after queueing - so when a slot was still playing, `switch_audio_push()` returned without
updating, the slot looked busy forever, and after 3 s of silence the self-heal tore down and
re-created the whole audio driver. It took a full queue to trigger: rare while the emulator ran
below 60 fps, routine once it kept up. Now the states are refreshed before every check, so a full
queue costs one dropped frame of audio.

**Rate control.** The console makes 60.05 frames of audio a second; the Switch shows 60 and plays
48,000 samples, so a fixed resampling ratio let the queue drift empty (a click), and frame jitter
swung it further. Now:
- The resampling ratio is nudged, at most 0.5% (RetroArch's own limit; too little to hear as
  pitch), to hold the queue near 1,600 output frames (about 33 ms). On hardware it settles at
  about -0.1%, the frame-rate mismatch.
- When the queue has run dry (start, after the pause menu, an underrun), one frame of silence goes
  in ahead of the next audio, restoring headroom at once.
- Eight wavebufs instead of four.

It can't hide a game running a few percent slow (Action Mania at 58-59 fps still clicks now and
then). Simulated over 2 minutes, steady 60 fps went from 228 underruns to 1. The perf log's
5-second lines show `dropped`, `underruns`, `reinits`, the smoothed `level` and the `adjust`.

## Frame skip: Auto counts drawing, skipped frames are paced (1.2.0)

**Auto** used to look only at `RunFrame()`'s time, so a heavy shader or the Accurate renderer
could slow a game without it reacting. Drawing time can't simply be added: when the emulator is
ahead, the wait for vsync lands inside the GL calls. So Auto now also watches emulated frames per
real second over each half-second window (`switch_frameskip.cpp`):
- Below 95% speed, one more level is skipped (a game at 58 fps is better shown in full).
- Coming back down is a probe, after 3 s at full speed; if it fails, the wait doubles, up to
  48 s, so a needed level isn't flipped back every few seconds.
- It never goes above what `RunFrame()`'s own cost calls for when emulation alone is too slow:
  skipping drawing can't make that up.

**Pacing.** A drawn frame is paced by the swap's wait for the display, but a skipped one wasn't:
the next frame started at once. With Manual skip a fast game sped up, and Auto's skipping
overfilled the audio queue (the log showed dropped audio exactly while skipping). An undrawn
frame that finishes early now waits until one frame period after the previous frame.

## BIOS test screen on the Spanish Motion BIOS

The test screen row used to be hidden for the Spanish Motion BIOS, which ignores RESTART+OFF+ON.
Disassembly found its own entry, and the harness confirmed it:
1. Hold OFF at power-on.
2. Press OFF about 5 more times: 10 changes, 9–15 frames apart, within about 2 s.

It shows "Main Unit Test Program" **Ver 7.05**, checksums `42C5` / `4519`.

`main.cpp` now plays a per-frame button script for the test-screen launch
(`ScriptTestScreenEntry()`): RESTART+OFF+ON for the v1.0x BIOSes, the OFF sequence for the
Spanish Motion one. The row shows for every BIOS. Details in `docs/console-type-and-bios.md`.
Also found there: v1.03 accepts IOC5 grounded (test point TP5) in place of RESTART. **Checked on
the harness, hardware-untested.**

### Motion region labels, read from the test screen

The test screen prints a "Language:" line, so every region code on the Spanish Motion BIOS was
read directly. The labels come from that line plus the logo and the insert-cartridge prompt.
They replace MAME's guesses in `RegionLabel()`:
- 0: English (UK), no boot screen.
- 1: English (US), no boot screen or prompt.
- 2: Italian.
- 3: English (US), no subtitle or prompt.
- 4: English (US), no subtitle.
- 5: English (US).
- 6, A, E: English (UK).
- 7: Chinese.
- 8: **Portuguese**. MAME guessed Mexican Spanish from the "Sistema Educativo" subtitle.
- 9: **Dutch**. MAME had a "(?)".
- B: German.
- C: Spanish.
- D: French.
- F: English (US, "Smartridge" prompt, ™ mark).

Codes 5/6/A/E produce identical frames; only the test screen's tag tells them apart. The full
per-code table is in `docs/console-type-and-bios.md`. The harness has no audio, so voice clips
are unchecked.

## Carts with swapped ROM halves

*Für immer Shrek* (Germany) and *Shrek 4 - Il était une fin* (France) showed a garbled screen
from the first frame. On the harness, the CPU ran a copy routine and then crashed into data. The
code it was executing didn't match the ROM file, which pointed at the cartridge mapping.

MAME's `vsmilem_cart.xml` explains it. These two carts are two 8 MB epoxy blobs wired to the
opposite chip selects from the US cart, so MAME loads blob1 at `0x800000` and blob2 at 0. The
16 MB dumps store blob1 first, so our core booted them from the wrong half.

`VSmile::LoadCart()` now swaps the halves when a 16 MB dump's first half CRC is blob1 of either
cart (`3f1c79e2` German, `bb3d3e09` French). A dump already in MAME's order doesn't match and
loads as-is. They're the only two such carts in MAME's V.Smile lists.

Checked on the harness: both reach their title screens and menus ("Lernabenteuer → Neues Spiel",
"Jouer l'aventure → Nouvelle partie"). The US cart is unchanged. **Hardware-untested.**

**Cars 2 (Germany and France) have the same fault.** Both stayed black. MAME loads these in dump
order (`cars2mge`, `cars2mfr`), but swapping the halves boots them, so they're in the swap list
too (first-half CRCs `8d5eeb71` German, `e9ce0620` French). Both now reach the Disney logo and the
player/level menu ("Spieler / Spielstufe", "Nombre de joueurs / Niveau"). **Hardware-untested.**

The rest of the multi-chip dumps boot as they are on the harness: Cars 2 USA and Pocket, Super
WHY!, Toy Story 3 USA, Alphabet Park Adventure (6 MB USA Alt 2/Alt 3 and UK), Care Bears, Lion
King, Mickey UK, Pooh USA/UK and Spider-Man UK.

**Toy Story 3 (Germany, Spain) too.** Same black screen, same fix (first-half CRCs `845bbb46`,
`a26c152a`); both match MAME, which loads them unswapped. The common French dump (`1eebefa2`)
already boots, but MAME's French file (`2cb9d987`) is exactly that dump with the halves swapped, so
its first half (`f1596894`) is in the list too. All reach the Learning Zone in their language.
*Toy Story 3 (Italy) [b]* is an 8 MB single chip; with the USA upper chip it boots, in English,
so the other Italian chip is undumped and the file can't work alone.

## Controller link: the lost RTS edge

**Symptom (hardware, reported):** BIOS test screen (v1.03, region F, joystick). Mashing all seven
buttons and the analog stick froze the screen and the colour LEDs within 30–60 s.

**Reproduction:** an input fuzzer on the desktop harness (`hash` event: framebuffer hash, CPU PC,
LEDs once a second). Booting the BIOS alone into the test screen and mashing random buttons and
stick levels for 3 minutes, 8 seeds, froze every session for 25–58 s at a time. The CPU kept
looping in the BIOS's `wait_blank` (`0x16866`), so the program ran but no input arrived.

**Cause:**
- The console notices a controller on the *falling edge* of RTS. The BIOS's EXT1 handler
  (`0xA9CD`, KanedaFr's disassembly) grants CTS only if no transfer is marked busy and the last
  UART activity was at least about 8 ms ago. Otherwise it returns, without a retry.
- Our controller raised RTS once and held it. When that edge fell in the "too early" window
  (right after an LED byte or probe, which mashing colour buttons makes constant), nothing more
  happened until the 0.5 s RTS timeout flushed the queue and started over.

**Fix (`vsmile.cpp`, `VSmileJoy::RearmRts()`):**
- With no CTS within 12 ms, release RTS for 1 ms and assert it again: a fresh edge.
- Do the same when the console drops CTS while bytes are still queued.
- The 0.5 s timeout now counts time without progress (it restarts on every byte sent) and is
  only a last resort.

**Results:**
- The 8 fuzz sessions: longest frozen stretch 1–2 s (inputs repeating), down from 25–58 s.
- 60 s traced: 3,665 bytes sent and read, 0 timeouts, 0 dropped, 252 re-requests.

**Motion controllers keep 1.1.0's handling (after hardware testing).** Releasing RTS between
packets broke Toy Story 3: on the harness it ignored OK on its main menu, and on hardware it kept
saying "Please reconnect your controller". Disabling the three re-arm triggers one at a time found
the culprit - the re-arm when CTS goes while a byte is in flight, for a Motion controller usually
mid-report - but skipping only that still showed the disconnect icon now and then. So a Motion
controller doesn't re-arm or retry at all, and its timeout doesn't restart per byte: exactly
1.1.0. A 12,000-frame Toy Story 3 Motion session then matched 1.1.0 screenshot for screenshot. The
joystick keeps the fix. Two Motion controllers are back at 1.1.0's 80 ms report gap (below).
Cars 2 still showed a brief disconnect notice on its main screen on hardware: a test case for
later.

**What the re-arm did for two-player Motion (since reverted, above).** This was also why two
Motion controllers lost presses and needed an 80 ms report gap. In *Shrek Forever After*'s two-player character select, both
ports streaming:

| Build | Timeouts | Reports per second, per port |
|---|---|---|
| Old core, 80 ms gap | after every menu press | ~8 |
| Old core, 40 ms gap | after every menu press | 7–9 (fewer) |
| Fixed core, 40 ms gap | only at boot | ~15 |

Both players' A presses registered on the same frame. With the revert above,
`kMotionPeriodShared` is back to 80 ms. The two-controller ID resend stays at 1 s; 250 ms cost about 2.5 reports a
second per port.

**Regression check:** all 74 carts in `reference/` cold-boot and take input with no dropped
bytes. The only timeouts are the usual ones right after reset. The German and French *Shrek
Forever After* dumps didn't boot on either core; that was a cartridge layout issue, now fixed (see
"Carts with swapped ROM halves").

## Noted for later (skipped as low value)

- ~~**FPS counter overlay**: veesem's `-fps`.~~ Done in 1.2.0 as the
  Performance overlay (see "Performance pass (1.2.0)").
- **IOC6 audio-output enable**: the wiki's "IOC6: audio output enable"
  (port C bit 6 shunts the whole audio output). The core ignores port C bit 6
  writes. Honouring it would only matter if a game deliberately mutes
  output, and none is known to.

## Power buttons and standby

**How the real console turns off.** Both power buttons are just inputs to the
SoC (port B: IOB6 ON, IOB7 OFF, both active low).
- On OFF, the game plays its goodbye clip, then copies a few instructions into
  RAM and runs them. They set the wakeup source (`3D29 = 0x80`, key change),
  the wakeup time (`3D2A = 0xC8`), and write `0xAA55` to `3D28`.
- That write **halts the CPU**: standby. RAM stays powered, which is how games
  "remember your place" (Smart Keyboard manual).
- On wakeup the CPU carries on after that write. The RAM code jumps to the
  cart's wakeup vector (`0xFFF6`), which re-initialises the hardware and
  resumes from RAM.
- The v1.03 BIOS disassembly (`reference/vsmile_disassembly`) shows the same
  sequence in the BIOS itself (`sleepmode_microcode`, `WAKEUP_handler`).

Our core used to ignore `3D28`, so a game "woke" the instant it slept and
simply rebooted. veesem does the same. Now `Spg200` really sleeps:
- The CPU is halted and the screen goes black.
- A change on the console-button pins wakes it (key-change source), and the
  2 Hz source is honoured too.
- The state is saved in save states (version 9).

**RESTART is a hardware reset.** The manuals:
- The original V.Smile has separate ON, OFF and RESTART buttons: "RESTART:
  press this button when the unit is ON, to start the game over from the
  beginning".
- The Writing Pad model, the Motion and the PC Pal have one **ON/RESTART**
  button: "If you press the ON/RESTART Button while playing a game, the game
  will start over from the beginning".

The Motion schematic (rendered with PyMuPDF) shows how:
- The front-panel connector (CON5 "ON-OFF") carries only OFF, GND and START.
- The old ON-driving circuit is marked "REMOVE 'ON' CIRCUIT", with every part
  "NC".
- The "new reset circuit" wires START straight onto `/RST` (D4 "short").
- IOB3 "RESTART" goes only to test point TP3.

Little Einsteins confirms games don't rely on IOB3: its button handler only
acts on it on a Motion-type board. So RESTART is emulated as a **hardware
reset that keeps RAM** (`VSmile::Restart()`), not a power-on.
- Pressed mid-game, the game starts over.
- Pressed in standby, it wakes the console, as the Motion's single button does.
- On the harness, Smart Keyboard offers **Continue Game** after either.

**What the Switch port does:**
- **Settings > Controller: Console ON / Console OFF / Console RESTART.**
  Bindable, unbound by default. ON and OFF are held while the button is;
  RESTART fires once per press.
- **Pause menu > Reset Game** presses RESTART, in place of the old cold reset.
- **Pause menu > Quit** turns the console off:
  - The game keeps running with OFF pressed (pads released) until it enters
    standby, so the goodbye plays. On the harness this takes 0.25-2.5 s:
    Scooby-Doo 15 frames, Thomas 55, Smart Keyboard 60, Little Einsteins 80,
    Up 150.
  - The whole console state is then saved to
    `sdmc:/switch/dsmile/standby/<game>.dss` and you're back at the grid.
  - If a game never sleeps within 10 s, Quit gives up and no standby is kept.
- **Next launch of that game** loads the standby state (still asleep), presses
  ON, and deletes the file, since the state is used once like the RAM it
  stands for.
  - Quitting any other way (L+R+Plus, HOME) boots cold next time, like pulling
    the cartridge without pressing OFF.
  - A standby from an older build fails the version check and boots cold.
  - Booting the BIOS alone never keeps one.
- **Turned off mid-game with the OFF binding:** the screen says "The V.Smile is
  off". ON wakes it, or Enter if ON isn't bound, or Reset Game.

**Fixes after hardware testing.**
- *OFF binding, then Quit, turned the console back on.* Quit pressed OFF,
  and standby woke on any console-button change. Now standby wakes only on
  a fresh press of ON (IOB6), and Quit on a console that's already off just
  keeps that standby and leaves.
- *Save states and changed settings.* A save state and a standby file are
  the same thing: one full core state. Neither held the console's
  configuration, which comes from settings at launch. On the harness,
  Smart Keyboard states made on v1.03 were loaded under v1.00, v1.02, v1.03,
  both Motion BIOSes, the French BIOS and no BIOS:
  - Standby states resumed with Continue Game under all seven.
  - Mid-game states worked under every standard BIOS and with no BIOS, but
    restarted the game under either Motion BIOS. The cause is the console
    type flipping to Motion under a running game, not the BIOS: v1.03 with
    the Motion type also restarts, and a Motion BIOS with the standard type
    doesn't.
  - States taken during the BIOS intro carry on into whichever BIOS is now
    loaded, and stay black with none.

  So states are now **v10**: they store the console type, Region code and
  VTech-logo jumper, and loading restores them. `ApplyPortDevices()` now
  applies the console type only at launch or when the Controller Type
  setting actually changes, instead of every frame. Loading a state also
  sets the PAL/NTSC frame pacing from the state. After this, mid-game v10
  states carry on under both Motion BIOSes. The BIOS image itself isn't
  stored (it's a 2 MB file the user supplies). Swapping it only matters for
  a state saved mid-intro, and removing it entirely leaves such a state
  black until Reset Game.
- *Android.* The core now really sleeps, and games also turn themselves off
  after a long idle spell (the BIOS has an "Auto Shut-off" message). The
  Android app has no ON button, so in standby any input presses ON
  (`jni_bridge.cpp`). This was compile-checked for the core with the NDK
  only; Oboe's headers come from the Gradle build.

**Why a full state rather than just RAM.** The console's RAM is only 10K words,
but resuming also needs the sleeping CPU and the code it's halted in, the
cart's bank/extmem setup, and the controller link. A save state already holds
all of that exactly, and taking it while asleep means nothing is mid-flight.
Waking still goes through the game's own wakeup code, as on hardware.

**Checked on the harness:**
- **Standby round trip:** Smart Keyboard, Cyber Adventure started, OFF, then
  standby saved. In a fresh process the state loads asleep, ON is pressed,
  and Adventure Play offers **New Game / Continue Game**. A cold boot goes
  straight to the level settings.
- **Cold boots:** all 41 carts in `reference/roms` still boot normally,
  without falling asleep.

## Console type straps, and why the US Motion BIOS shows the V.Smile intro

**The US Motion BIOS is v1.03.** `vmotionbios.bin` (CRC `427087ea`, "Motion
(US unit)") matches the standard v1.03 BIOS word for word. The only
difference is a `078800VER0` tag in its first 10 words, which the CPU can
never read (internal RAM shadows them). `0788` is that console's FCC ID and
PCB code (MAME: FCC ID 62R-0788, PCB 35-078800-001). MAME's own note says the
dump "doesn't show the 'Motion' logo at all, but was dumped from a Motion
unit". So the standard V.Smile intro is authentic for that console, and no
jumper changes it: the Motion animation isn't in that BIOS. Only the Spanish
dump (`vsmilemotion.bin`) has it. On the harness the US BIOS gives the V.Smile
intro under all 16 region codes and all console types.

It's now identified as "Motion (US unit, v1.03)":
- Region shows v1.03's labels rather than the Motion list.
- The BIOS test screen row appears for it; its test screen works.
- It still selects the Motion console, since it came from one.

**Console type (port A IOA14/IOA15).** The wiki's `io` page, from the Motion
schematics:

| Console | IOA14 | IOA15 |
|---|---|---|
| V.Smile, V.Smile Pocket | 0 | x |
| V.Smile Motion | 1 | 0 |
| Cyber Pocket | 1 | 1 |

We returned MAME's `0xC000` for the Motion console, which by this table is the
Cyber Pocket. The core now has `VSmile::SetConsoleType()` (Standard / Motion /
CyberPocket), and Motion returns `0x4000`. This is harmless either way, from
disassembling every Motion cart's port A reads:
- Most (Action Mania, Pooh, Up, Little Einsteins, Scooby-Doo, Thomas,
  Spider-Man) only test IOA14.
- A shared routine (at `0xB9C8` in most carts, `0x41101F` in Shrek) sorts
  `0x4000`/`0x8000` as the Motion console and `0` as the original. `0xC000`
  becomes a third type (presumably the Cyber Pocket's handheld mode) when
  **IOC5** (`3D0B` bit 5, port C) is high. Our core drives IOC5 high, so
  with the old value Shrek (and any cart using this routine) took that third
  path. It now takes the Motion path, as a real Motion console would.
  *(Corrected: this first said IOB5 and "harmless"; `3D0B` is port C.)*

Neither BIOS changes its intro with the console type.

**Other jumpers.** Tried on the harness with no visible effect: IOC5 low (the
wiki's "maybe LCD/TV detection") and the Cyber Pocket console type. So nothing
else is worth exposing. The two useful ones, Region and the VTech logo, are
already settings.

## New this pass: veesem's options (PAL, logo jumper, LEDs)

veesem (github.com/sp1187/veesem, archived) has three console options this
port didn't expose. The core already supported all three; they just weren't
reachable from the Switch UI.

**Video Timing (Library & Storage).** NTSC (default) or PAL. PAL is the
core's 312-line, 50 Hz mode (`VSmile::Reset(pal)`; the PPU reports it at
`0x2B`).
- The Switch display is 60 Hz, so `main.cpp` runs 5 emulated frames per 6
  refreshes. Each refresh adds 5 credits and each shown frame costs 6; a
  refresh with no credit shows the previous frame again.
- Frames that frame skip doesn't render aren't charged, since they don't
  wait for vsync. Frame skip's per-frame budget becomes 20 ms.
- Audio needs nothing: the resampler works at a fixed 281250 to 48000 Hz
  ratio, so 50 longer frames a second come out at the right pitch.
- On the harness, Little Einsteins boots and reaches its menu under PAL.

This is mostly useful with a PAL BIOS and European carts; US carts run too.
Applies at launch, like Region.

**VTech Logo (Library & Storage).** The Port C bit 4 jumper. Hidden skips the
BIOS intro's blue VTech logo and starts straight at the V.Smile animation
(checked on the harness). The cart's own intro is unaffected. Applies at
launch.

**Controller LEDs (Graphics).** Draws the four colour-button lights as small
squares in the bottom-left corner, dim when off. There's a second row when
player 2 is connected.
- The console sets the lights with `6x` bytes (bit 0 green, 1 blue, 2 yellow,
  3 red, as veesem has them).
- Games light a button when it's pressed, as feedback. Not every game does:
  on the harness Up and Little Einsteins lit Red and Blue on those presses,
  while Scooby-Doo and Thomas never touched the LEDs.

**Everything else in veesem was checked against our core.** Its v0.2 changelog
fixes (MULS FIR_MOV, blending over transparent pixels, blended sprites in
depth-layer order, the SPU's wave-in mix, the watchdog) are all already in
`app/src/main/cpp/core`. So are its Port C/ADC details (full battery `3FF`,
UART lines idle high) and region notes. Its remaining features are desktop
debug UI (a memory editor, SPU visualizer), an FPS counter, and
F1/F2/F3 = console ON/OFF/RESTART, which belongs to the power-off work.

## New this pass: dance mat, stick levels, BIOS test screen

**Dance mat (Jammin' Gym Class).** Settings > Controller > Controller Type >
**Dance Mat (Gym Class)**, for player 1 and player 2 (the game's two-player
mode needs a mat on each port). The mat keeps the joystick's protocol and its
`55` ID but sends different bytes: its Right arrow is the pad's "left,
level 3" byte, Yellow is "left, level 1", and so on (table in
`reference/findings.md` §0; MAME `mat.cpp`). So a normal pad can't play
mat games. `VSmileJoy::QueueMatUpdates()` sends the mat's bytes, with the
pad's inputs mapped one to one onto the mat's 3x3 grid plus its buttons:

| Mat | Pad input |
|---|---|
| Up / Down / Left / Right arrows | stick or D-Pad |
| Red / Yellow / Blue / Green footprints (corners) | the Red / Yellow / Blue / Green buttons |
| Centre panel | ABC |
| Enter / Exit / Help | Enter / Exit / Help |

Checked with the US cart on the harness:
- "Pick Your Athlete": the mat's Right arrow changes the athlete. A joystick's
  right does nothing, which confirms the game needs the mat.
- Main menu: the Red/Yellow/Blue footprints open Gym Games, Brain Games and
  Music Box. Green highlights Options, and Enter opens it. The Options screen
  shows the manual's layout (red 1 player, yellow 2 players, blue easy, green
  difficult).
- The BIOS test screen (below) shows each panel arriving as the byte in the
  MAME/wiki table.

**Analog stick levels.** The V.Smile joystick reports 5 levels per direction.
The left stick now maps its deflection onto them: nothing inside 20%, full
tilt from 85% out (so a diagonal still reaches level 4), evenly spaced in
between. The D-Pad is always full tilt. The BIOS test screen reads 3..7 for
levels 1..5, and on the harness levels 1, 3 and 4 read back as 3, 5 and 6.
Games that only check for "any tilt" behave as before; games that use the
level (e.g. for movement speed) now get partial tilt.

**Booting the BIOS alone and its test screen.** Settings > Library & Storage
now has **Boot BIOS (no cartridge)** and **BIOS test screen** under the BIOS
row. They only appear when a BIOS is present, and the test-screen row is
hidden for the Motion BIOS, which has none.
- An empty slot makes the BIOS answer at every bank (MAME's `banked_map`), so
  the BIOS is loaded 4x mirrored as the "cart". It then shows its own
  insert-a-cartridge screen.
- The test screen holds ON + OFF + RESET (all active low) for the first second,
  then releases them. KanedaFr's procedure: the BIOS checks them in its first
  ticks.
- It shows "Main Unit Test Program": BIOS version, language, the two ROM
  checksums, the power (battery ADC) reading, and a live test of controller A's
  stick and buttons. On the harness v1.03 reads checksums DA64/DA64 and v1.00
  reads 8F05/2F61.
- Earlier, holding the buttons with a cart inserted did nothing: the cart's own
  code runs instead, which is why it has to be the BIOS alone.
- The test screen's "ON key" switches it to stick B. ON/OFF aren't bound to any
  Switch button yet (that's the power-off work).

## New this pass: V.Smile Motion controller (tilt)

**Using it.** Settings > Controller, Player 1, **Controller Type** now cycles
Joystick → Smart Keyboard (USB) → **Motion Controller (tilt)**. With it
selected, Motion carts offer their Standard/Motion choice (the "1P" screen
with the Motion controller and joystick pictures), and in Motion mode tilting
the Switch controller left/right is read by the game. The pad's stick and
buttons keep working as usual. It uses the accelerometer of player 1's
controller: the console itself in handheld mode, a Pro Controller, or the left
Joy-Con of a pair.

**Why it never worked before - two separate gates, both now handled:**
1. **The controller has to identify itself.** Each controller's keep-alive
   byte is its device ID: `55` joystick, `52` Smart Keyboard hello, **`56`
   Motion controller**. Games map the ID to a "device type" (Action Mania's
   library function at `0x43EA5`; `56` → type 7) and only show the
   Standard/Motion screen for type 7.
2. **The game also checks the console.** Choosing Motion silently fell back
   to Standard until the core reported the **V.Smile Motion board's port A
   pins**: IOA14/IOA15 high (`0xC000`, same as MAME's `vsmilem` driver). The
   check is Action Mania's `0x5608C`. `VSmile::SetMotionConsole()` models it;
   the Switch build turns it on when a Motion BIOS is loaded or the Motion
   controller is selected. The in-game tell: the icon at the start of Rapid
   Race shows the Motion controller held upright (as in real footage)
   instead of the joystick.

**Protocol** (from Action Mania's controller library; `PortDevice::Motion`
in `vsmile.*`):
- The console sends `05` when a Motion controller appears.
- `8n` sets the report mode (0 = off). The controller must answer `Dn`, or
  the game resends it forever. Menus ask for mode 0; Rapid Race asks for
  mode 2.
- `9n` sends a 4-bit setting (default `F`; meaning unknown, no reply needed).
- While a mode is on, the controller streams reports. Modes 1/2 send `43 X Y Z`,
  one raw byte per axis. Modes 3/4 send `43` plus four 6-bit bytes packing
  the same 24 bits, with X and Z inverted. Modes 3/4 are implemented from the
  code but untested.
- Axis bytes: `80` = 0g, about `40` per g, so level and still is
  `80 80 C0`. **X is left/right tilt**, higher = right. Rapid Race steers on
  it, with a dead zone of about ±`10` (roughly 15°).
- The console withholds CTS for long spells while it sends its own commands.
  Our 0.5 s no-grant timeout drops a half-sent report rather than
  interleaving a `56`, and the next report realigns the game's parser.

**Steadiness filter: a still controller now reads still.** Hardware testing
of Shrek Forever After's Bowling Bonanza: holding the controller still threw
the ball; only setting it down stopped it. On the desktop harness, steady
readings never throw, nor does a single step change (even of 8). But a change
of **1** that returns a quarter second later throws, and so does a drift of
+1 per second. So the game treats any small "movement" as a throw, and a real
controller held still must report constant values. A hand-held Switch
controller never does: sensor noise, tremor, and a few degrees of slow
wander. `switch_input.cpp` now smooths each axis and only moves the reported
value once the smoothed reading is 8 units (about 7°) away from the last one.
In simulated tremor plus a ±4-unit drift, a deadband of 6 still threw; 8 held
steady; deliberate swings and tilts still threw. Rapid Race's own steering
dead zone is 16, so steering is unaffected.

**Steadiness filter, v2: gyro-gated.** Hardware testing showed the deadband
fixed Shrek but made Spider-Man & Friends: Secret Missions need extreme
motions in every mode. On the harness, Safari Showdown ignores held tilts
entirely (even ±64, i.e. 90°) and reacts to shakes. A sideways shake of ±32
does nothing, while ±48 makes Spider-Man run. The old filter's smoothing and
deadband cut a real ±0.75 g shake below that threshold. The filter now:
- Decides whether the controller is **moving**. That is true when the gyro
  (smoothed so 10 Hz hand tremor averages out) reads over 20°/s, or when the
  lightly smoothed accelerometer departs quickly from its slow average (over
  6 units), which catches sideways shakes that barely rotate.
- While moving, and for 8 frames after, passes the raw readings through
  untouched.
- While still, holds the last value. A slow tilt still gets through once the
  slow average is 8 units away.

Checked on the harness with a simulated hand (roll/pitch tremor at about
10 Hz plus a ±3° wander, accelerometer and gyro noise), fed through a Python copy
of the filter:
- In Shrek's Bowling Bonanza, a still hand, even with 1° of tremor, doesn't
  throw, and neither does the old filter. A quick swing throws. Raw
  unfiltered input throws, which shows the test is sensitive.
- In Safari Showdown, a ±0.75 g shake makes Spider-Man run through the new
  filter but not through the old one, matching the hardware report.

One assumption: libnx doesn't document the gyro's units. The code assumes
the SDK convention of 1.0 = 360°/s, one revolution per second. If that's
wrong, the 20°/s threshold is off by the same factor.

**Axis fix: forward/back tilt was inverted.** Hardware testing in
Spider-Man & Friends: Secret Missions (Safari Showdown's tree: "tilt forward
to climb up") needed the opposite tilt. The real controller behaves as a
gravity sensor with x right, y toward the player, z down. That fits both
known facts: tilting right raises x, and level reads +1g on z. So tipping the
top away lowers y. The Switch's y axis points the other way, so
`switch_input.cpp` now negates it; x and z were already right.

**Two Motion controllers (player 2).** Settings > Controller, switch Player
to **Player 2**: a **Controller Type** row (Joystick / Motion Controller
(tilt)) now appears there too. The Smart Keyboard stays player 1 only. On
the real console each wireless controller has an OFF/P1/P2 switch, and its
single receiver presents them to the SoC as ordinary ports 1 and 2. So a
second Motion controller is just a Motion device on port 2
(`VSmile::SetPort2Device()`/`SetMotion2()`), with its own ID, modes and
reports. The game's library already handles both ports (Action Mania's port-2
path runs its own mode acks and reports, confirmed with watch hooks).
Player 2's tilt comes from controller slot 2's sensor (Pro Controller, or the
left Joy-Con of a pair).

Found while testing two players with Shrek Forever After (its 2-player
option shows a "switch controller 2 to P2" screen until a second controller
answers). With two streaming Motion controllers, player 1's menu presses got
lost:
- **Queued input was flushed on timeouts.** When the console pauses
  servicing (screen loads, its own commands), a controller gives up after
  0.5 s. That flushed queued button events, and then assumed "nothing
  pressed". Now a Motion controller drops only its stale report and ID
  bytes and keeps button events queued in order. Any controller that does
  flush re-sends its full current state next time, so a lost release can no
  longer leave a button stuck (a latent joystick bug too).
- **Too much traffic for the console's controller servicing.** With two
  Motion controllers, each now reports every 80 ms instead of 40 ms, and
  re-sends its ID every 1 s instead of 250 ms. Every `56` makes the console
  repeat its Motion setup.

Result: Shrek's two-player flow accepts both players (Character Selection
shows P1 and P2, and P2's OK registers). Player 1's menu input with a second
Motion controller matches the joystick-on-port-2 baseline: selecting "2
players" worked in 4 of 6 runs at different timings for both, and every run
accepted the OK. The misses are the test script racing the screen. Single
controller behaviour is unchanged. A "turn-taking" scheduler between the two
ports was tried and removed; it cut each port to about 2.6 reports a second.

**Fixed after the first hardware round: garbage input and false
disconnects.** The first build sent reports every 33 ms regardless of line
activity. On hardware, after a while, input went strange, the controller
dropped out, and Toy Story 3 switched to the writing-pad controller. Found and
reproduced on PC with long random-tilt sessions. The cause is a chain:
1. The console can only send its own commands while our RTS is idle.
   Reports back to back starved it.
2. Separately, the game's disconnect check has a race (Action Mania
   `0x88F3`: "now − last seen", read across an interrupt that stamps a newer
   time and so goes negative). A report arriving at just the wrong moment
   triggers a false disconnect.
3. After a disconnect the game resets the port, and every report byte is
   parsed as an ordinary pad byte. `5x` values become a new device ID (`50`/
   `51` = the writing-pad controller, `52` = keyboard). `Cx`/`8x` look like
   the stick, `9x`/`Ax` like buttons. Nothing re-identified the controller,
   because the `56` keep-alive never fires while streaming.

Now:
- Reports wait for 40 ms of idle line, about 19 per second.
- The `56` ID is re-sent every 250 ms while streaming.
- A report still waiting to go out is dropped when an `8n` arrives.
- Reports pause for 30 ms after a console `05`.

Over eight random-tilt sessions of about 3 minutes each: before, 16 wrong
IDs in one session and they never stopped. Now, 3 false disconnects and 1
wrong ID in total. Each recovered on its own after about 2 seconds (the
console's own cool-down before it serves the port again). A 20 ms gap gave 12
disconnects, so the rate stays at 40 ms. The race is in the game code, so it
presumably exists on real hardware too.

**Tested on PC** (`test/keyboard_test.cpp`, `mot` device and
`<frame>:tilt<x>,<y>,<z>` events), Action Mania (USA) with the US Motion BIOS:
- The Standard/Motion screen appears, and picking Motion gives the Motion icon.
- Steering: X `98` → car moves right, `60` → left, `80` → keeps its lane.
  It holds up across a minute of racing with direction changes.
- Up (Motion) and a non-Motion Scooby-Doo still boot and take input normally
  with the Motion controller.

**Not verified yet:**
- On Switch hardware: which way is "right" on the Switch accelerometer. If
  steering is mirrored, the X sign needs flipping (one line in
  `switch_input_poll_motion()`).
- Which physical motions the other minigames want (forward/back tilt,
  swings), and whether Y/Z map to them.
- Report modes 3/4.
- The Motion controller on port 2.

## New this pass: Smart Keyboard (USB keyboard)

**Using it.** Settings > Controller, Player 1, **Controller Type** row:
Left/Right or A toggles between **Joystick** and **Smart Keyboard (USB)**.
With the keyboard selected, a **Keyboard Layout** row appears: English
(US), English (UK), French (AZERTY), German (QWERTZ), Spanish. **Set it to
match your USB keyboard**, whichever cart you play. It's the language byte
the keyboard sends at power-up, and every Smart Keyboard cart tested (US,
French, German, Spanish) switches its whole typing layout to follow it.
So with it matching your keyboard, the character on screen is the one
printed on the key you pressed. Plug a USB keyboard into the dock or a
USB-C adapter. The setting takes effect immediately, including from the
pause menu mid-game (port 1 is "re-plugged"; tested working), but choosing
it before launching the game is the natural way.

With the keyboard selected, port 1 is the Smart Keyboard, not a joystick,
same as real hardware where the keyboard plugs in instead of the pad. The
Switch pad still works as the keyboard's own joystick ball (D-Pad/stick)
and its buttons (your normal bindings). The real keyboard has all of the
pad's buttons: four colour buttons, and Help / Exit / ABC buttons at the
top right. Player 2 is unaffected.

**Key layout** (by physical position - USB keyboards report positions,
not characters, so an AZERTY or QWERTZ keyboard lines up with the French or
German Smart Keyboard the same way QWERTY does with the US one):

| Smart Keyboard | USB keyboard |
|---|---|
| Letters, digits, Space, Shift, Caps, Backspace, arrows | the same keys |
| OK (Enter) / Exit / Help | Enter / Esc / F1 |
| Player 1 / Player 2 / Symbol | F2 / F3 / F4 (or keypad 1 / 2 / +) |
| Red / Yellow / Blue / Green | F5 / F6 / F7 / F8 |
| ABC | F9 |
| Typing Time | Tab |
| Erase | Delete (or `]`) |
| the key right of P (US: `/ ?`) | `[` (or `/`) |
| the key right of 0 (US: `- +`) | `-` |

The arrow keys navigate the carts' menus, so a USB keyboard alone is
enough; the pad isn't required.

**Carts.** In `reference/roms/`, identified against MAME's
`hash/vsmile_cart.xml`:
- **Smart Keyboard carts:** Smart Keyboard (USA), Clavier Tip Tap (France),
  Schreibspaß (Germany), Teclado Interactivo (Spain).
- **PC Pal carts, not Smart Keyboard** (despite the file names): PC Pal
  Island (USA), Mes premiers clics (France), Mein erster Mausklick
  (Germany), "V.Smile - PC" (Spain). They need PC Pal's infrared
  keyboard/mouse, which isn't emulated.
- MAME marks the Spanish Smart Keyboard cart unsupported, since it has no
  Spanish keyboard.

**How it works.** Core side (`app/src/main/cpp/core/vsmile.*`), port 1's
`VSmileJoy` gains a `PortDevice::SmartKeyboard` mode reusing all of the
existing UART/RTS/CTS plumbing. Protocol from the vtech.pulkomandy.tk
wiki's logic-analyzer captures and MAME's `bus/vsmile/keyboard.cpp`, which
agree:
- Power-up: every 300 ms raise RTS for 12.2 ms; if the console selects the
  port, send `52 52 52`. The console answers `02 02 E6 D6 60`, the keyboard
  replies with its language byte (`40` US), then it's the usual `55`
  keep-alives and `7x`/`Bx` probe replies.
- Keys send their code on press and `code | C0` on release; Shift is
  `A9`/`AA`. OK/Quit/Help/ABC are `A1`/`A2`/`A3`/`A4`, `A0` when released.
  The keyboard's joystick uses `87`/`8F`/`80` (up/down/centre) and
  `7F`/`77`/`70` (left/right/centre).
- Colour buttons send the joystick's `9x` byte. Neither source documents
  them (MAME's keyboard has none), but the wiki keeps key codes out of the
  `90`-`AF` range for the shared buttons, and the cart accepts them: red
  skips Cyber Adventure's intro exactly as it does from a joystick.
- Language bytes: `40` US, `41` UK, `42` French, `44` German (wiki), and
  **`43` Spanish** (not on the wiki; found by testing the Spanish cart).
- The console's UART output now reaches both ports and each device decides
  whether it cares (`VSmileJoy::Rx(byte, selected)`), like MAME: the
  keyboard must hear the handshake bytes even when not selected. The
  joystick still ignores anything it isn't selected for, so its behaviour
  is unchanged.
- Save states bumped to version 7 (the port's device and keyboard state).
  Older states still load, as a joystick.

Switch side: `switch_input_poll_keyboard()` (`hidInitializeKeyboard()` +
`hidGetKeyboardStates()`) diffs the USB keyboard each frame into key
events, sending Shift presses first and Shift releases last so a fast
Shift+letter still arrives as a capital. `main.cpp` feeds the events to
`VSmile::KeyboardKey()` and calls `SetPort1Device()` every frame (a no-op
unless the setting changed).

**Tested on PC** with `test/keyboard_test.cpp` (scriptable: key presses,
pad input, device swaps and screenshots at given frames), the real
`Smart Keyboard (USA)` cart and the v1.03 BIOS:
- The handshake trace matches the wiki's capture byte for byte, and the
  console keeps the keyboard alive (probe replies checked against MAME's
  formula).
- Title screen and main menu reached; the keyboard's joystick moves the
  menu cursor; OK enters Computer Zone's login screen.
- Typing into the NAME field: "abcde", "yellow", doubled letters ("lla",
  "aab", "kka"), Backspace, Shift+H gives "H", Caps Lock gives capitals.
- Swapping joystick → keyboard mid-game and then typing works.
- Typing "hello" shows "helo": every byte reaches the console (checked in
  the trace), and "yellow" and other doubled letters work, so this is the
  game refusing the word "hell" (a kids'-game word filter), not a dropped
  key.
- Arrow keys move the menu cursor; the colour buttons from the keyboard
  work (see above).
- Layouts, typing the same eight key positions (Q, W, Y, Z, `;`, `-`, `[`,
  A) into each cart's login field:
  - French cart, `42`: **azywm)^q** (AZERTY).
  - German cart, `44`: **qwzyöß**; "üab" typed separately. That field
    holds only 6 characters.
  - Spanish cart, `43`: **qwyzñ-á** (the `[` position is the accent dead
    key).
  - French, German and Spanish carts with `40`: **qwyz;-/a**, the US
    layout.
  - US cart, `41` vs `40`: Shift+2 / Shift+3 give **" £** vs **@ #**.
- The US key right of P is `/` (Shift `?`), matching a photo of the real
  keyboard. MAME's key table labels it `'`, which is wrong.

Hardware-confirmed: USB keyboard detection, typing and the Enter/Esc/F1
keys. Not yet hardware-tested: F5-F9 (colours/ABC) and the non-US layouts.

## New in the previous pass: player 2 bindings and BIOS version detection

**Player 2 bindings.** Settings > Controller now starts with an
"Player" row - press A or Left/Right on it to switch between Player 1 and
Player 2. Player 2's list is only the 8 V.Smile buttons (Enter, Exit,
Help, ABC and the four colours); the hotkeys (save/load state, rewind,
menu) stay player 1's, since `main.cpp` only ever reads them from player
1's controller. Player 2's layout starts out identical to player 1's
defaults, is saved under its own `bind2_*` keys in `settings.ini`, and
keeps the same "one button, one action" rule within its own list. The
"Press a button to bind" prompt accepts the press from either controller,
so one person can set up both without handing controllers back and forth.
Nothing changes when only one controller is connected.

**BIOS version detection.** New `core/switch_bios.{h,cpp}` identifies a
BIOS file by CRC-32 against the five known dumps (MAME's v1.00, v1.02,
v1.03, and the two Motion dumps - from a Spanish and a US unit) *and*
against byte-swapped copies of each. It's used in three places:
- The Library & Storage BIOS row and the BIOS picker show the version,
  e.g. `vsmile_v103.bin (v1.03)`, `vsmilebios.bin (v1.02, byte-swapped)`,
  or `(unrecognized)`.
- The Region row's label follows the detected version (see "Region labels
  per BIOS version" below). The code is now shown in hex (`A`-`F` rather
  than `10`-`15`) to match that table.
- `main.cpp` un-swaps a known byte-swapped dump before handing it to the
  core, so `vsmilebios.bin` from `reference/bios/` now boots as v1.02
  instead of feeding the CPU scrambled instructions.

None of the dumps contain a readable version string, so the CRC is the
only reliable signal; an unrecognized dump gets version-neutral labels.

### Region labels per BIOS version

Source: the community wiki's hardware-tested table (vtech.pulkomandy.tk,
page `io`, "Configuration" - documented by veesem's author in 2025; see
`reference/findings.md` §9). It supersedes the standard-BIOS half of the
MAME-derived table in "Region mapping" below. The Spanish Motion BIOS's
labels now come from its own test screen (see "Motion region labels, read
from the test screen" above).

| Code | v1.00 | v1.02 | v1.03 |
|---|---|---|---|
| 0, 1 | English (US) | No boot screen | No boot screen |
| 2 | English (US) | English (UK, no subtitle) | **Italian** |
| 3 | English (US) | English (US, no subtitle) | same |
| 4 | English (US) | English (US, alt. subtitle "TV Learning System") | same |
| 5 | English (US) | English (US, no "V.Smile!" clip) | same |
| 6 | English (US) | English (UK, no "V.Smile!" clip) | same |
| 7 / 8 / 9 | Chinese / Portuguese / Dutch (?) | same | same |
| A | Italian | Italian | **English (UK, no subtitle)** |
| B / C / D / E / F | German / Spanish / French / English (UK) / English (US) | same | same |

On v1.00, codes 0-5 also show a blinking blue VTech logo when no cartridge
is inserted - never visible here, since this port always has a cart
loaded. An unrecognized standard BIOS (or none) shows "Italian or English
(UK)" for 2 and A, and the v1.02+ labels for 0-6.

## New in the previous pass: more shader ports (libretro/common-shaders)

Settings > Graphics > Shader gained 4 more options this pass - Scale2x,
Scale3x, Super2xSaI, and zfast-crt - bringing the total to 14. Same idea
as the previous pass (porting known-good shaders from the wider
RetroArch/libretro ecosystem rather than writing new ones from scratch),
but sourced directly from
[libretro/common-shaders](https://github.com/libretro/common-shaders) this
time instead of through DraStic's own curated subset of it.

**The real difference from last time: this repo is Cg, not GLSL.** The
DraStic bundle's `.dsd` files turned out to already be GLSL ES 1.00 in a
text wrapper, so that port was a mechanical rename. `common-shaders` is
written in Cg (`float4`/`float3` instead of `vec4`/`vec3`, `tex2D`-style
sampling via `COMPAT_Sample*` macros, semantic-bound struct fields like
`float2 texCoord : TEXCOORD0`) - a close cousin of GLSL with a direct
equivalent for everything used here, but porting it is a real hand
translation, not a rename. Each of the 4 below was checked line-by-line
against its downloaded `.cg` source (`libretro/common-shaders`'s `crt/`,
`scalenx/`, and `xsai/` directories) rather than paraphrased from memory -
see the comments directly above each shader's source in
`switch_render.cpp` for the specific things that needed real translation
(mainly: Cg's boolean-vector comparisons like `all(A==B)` become GLSL's
`all(equal(A,B))`/`any(notEqual(A,B))`; Cg's `frac()` is GLSL's
`fract()`).

**Why these 4 out of a genuinely huge ecosystem** (`common-shaders` has
~40 top-level categories - CRT variants alone number in the dozens):
picked for being both single-pass (this renderer still has no
render-to-texture/FBO chaining - most of that ecosystem, `scalefx` and the
`crt-royale`/`crt-geom`/NTSC families included, is genuine multi-pass and
stayed out of scope for the same reason the DraStic bundle's `+` combo
presets did last pass) and for actually adding something new rather than
overlapping what's already here:
- **Scale2x / Scale3x** (Andrea Mazzoleni, GPL) - a much cheaper
  alternative to 5xBR/SABR for anyone who finds those two too heavy: a
  classic sharp, non-blurry edge-preserving upscaler, a few texture reads
  and comparisons instead of a page of edge-detection math.
- **Super2xSaI** (Derek Liauw Kie Fa's original algorithm, ported into
  DOSBox by The DOSBox Team, adapted for RetroArch by "guest(r)" - all
  GPL) - a softer, smoothing scaler, a different look from every sharp/
  edge-preserving option already here.
- **zfast-crt** (Greg Hogan/SoltanGris42, same author as zFast LCD above)
  - a second, meaningfully different CRT look from D.Smile CRT's own
    (barrel distortion + glow + RGB triad mask): this one leans on a
    "weighted linear" resample plus a monochrome per-column aperture mask,
    and its own header specifically advertises being cheap enough for a
    Raspberry Pi 3 - worth having as a lighter CRT option if D.Smile CRT
    turns out to be the wrong performance tradeoff for someone's setup.

**Filtering**: Scale2x, Scale3x, and Super2xSaI all read precise neighbor
texels the same way xBR/SABR do, so they need `GL_NEAREST` for the same
reason (see the point_sample comment in `switch_render.cpp`). zfast-crt
explicitly resamples through hardware bilinear (its own header calls it "a
weighted linear filter") the same way D.Smile Sharp/CRT and Sharp Bilinear
already do, so it uses `GL_LINEAR` like those.

**Parameters baked in as fixed constants**, same treatment as the DraStic
shaders' `#define`s: zfast-crt ships several `#pragma parameter`-tunable
values upstream (blur amount, scanline darkness, brightness boost, mask
strength) - this port uses their documented defaults rather than exposing
new per-shader sliders, matching how 5xBR/SABR's own tunables were handled
last pass.

**Not attempted, same reasoning as before**: anything genuinely
multi-pass (ScaleFX, crt-royale/crt-geom, NTSC shaders needing frame
history, hqx's lookup-texture-based variants), and per-shader tunable
parameters beyond the baked-in defaults above.

**Update: hardware-confirmed working**, translation included. Original
note: **Hardware-untested** - all 4 need a real run, and this batch specifically
also needs confirmation that the Cg→GLSL translation itself is correct;
unlike the previous batch, nothing here was a mechanical rename of
already-working GLSL, so there's more surface area for a translation
mistake even after a careful line-by-line check. `switch_render_init()`
still isolates a failure the same way as the previous batch - a bad
translation that fails to *compile* would show up as that one menu entry
falling back to D.Smile Pixel (logged), not a crash - but a translation
that compiles fine while computing something subtly wrong wouldn't be
caught by that safety net at all, only by actually looking at it running.

## New in the previous pass: shader ports

Settings > Graphics > Shader now has 14 options total (10 as of this
section, 4 more added later - see "New in the previous pass: more shader
ports (libretro/common-shaders)" above): the original
Pixel/Sharp/CRT (unchanged, just relabeled "D.Smile Pixel/Sharp/CRT" in
the menu to read as a distinct family) plus 7 new ones - LCD1x, Sharp
Bilinear, Linear, zFast LCD, Natural Vision, 5xBR, and SABR - ported from
`reference/DrasticDS_nx-main/third_party/drastic-ds-shaders`.

**NDS Color was dropped after the initial port** (which had 8 new ones,
including it). It's a color-correction shader that simulates a Nintendo
DS Phat's specific display characteristics - not something a V.Smile
emulator has any reason to reproduce, since V.Smile hardware never had
that display. It shipped in the same bundle purely because the bundle is
DraStic's (a DS emulator), not because it's relevant here; everything
else in the bundle is a general-purpose upscaler/overlay effect that
doesn't assume DS hardware.

**These aren't DraStic's own shaders, and the menu doesn't call them
that.** They're classic shaders from the broader RetroArch/libretro
ecosystem; jdgleaver (and, for 5xBR/SABR specifically, a second porter
credited as "Try791023" in their own source - see below) adapted them
*into* DraStic DS's own shader format, the same way this project just
adapted them again into its own. The first attempt at this labeled them
"DraStic <name>" in the menu to distinguish them from D.Smile's own 3 -
inaccurate once the actual origin turned up, so they're shown under their
original names instead. **This did change their `settings.ini` keys**
(`drastic_lcd1x` → `lcd1x`, etc. - see `switch_menu.cpp`'s `kShaderKeys`)
- a save made against the very first build with these 8 will silently
reset to D.Smile Pixel on next load rather than error, since the old key
just won't match anything anymore.

**Why this turned out feasible rather than a rewrite**: that `.dsd` format
looked proprietary from the file extension alone, but it's actually just
plain GLSL ES 1.00 (`attribute`/`varying`/`gl_FragColor`, `texture2D()`) in
a small text wrapper - the exact same dialect `switch_render.cpp` already
writes its own shaders in. Every shader that's actually a single `<vertex>`/
`<fragment>` pair (all of them, individually - only the bundle's `+`-joined
combo presets like `lcd1x+natural_vision.dfx` chain multiple passes) drops
into this project's existing one-quad-one-GL-program pipeline with a
mechanical rename (`u_texture`→`uTex`, `a_vertex_coordinate`→`aPos`,
`u_texture_size.zw`→`uTexSize`, etc. - see the comment above
`kFragDrasticLcd1xSrc` in `switch_render.cpp`) rather than new rendering
architecture. The `+` combo presets are **not** included here - they're
genuine multi-pass chains, and this renderer has no render-to-texture/FBO
chaining to run a second pass through, so porting those would be a real
architecture change, not a shader port.

**Why only 7 of the ~24 base shaders**: most of the bundle is the same
xBR/SABR edge-detection algorithm at different quality/performance presets
(Low/High/Very Low/Very High Configuration, spanning v3.5 through v4.0) -
maintaining, and eventually hardware-testing, ~20 near-duplicates wasn't
worth it. Picked one of each: the newest xBR (5xBR v4.0) and SABR's
"Optimized" variant (explicitly built for a weaker mobile GPU, which a
stock Switch's Tegra X1 also qualifies as, over the plain/lq variants)
alongside the shaders that only exist once each and are actually relevant
to a non-DS console - LCD1x, Sharp Bilinear, Linear, zFast LCD, and
Natural Vision (NDS Color was in this group too, but dropped - see above).

**Performance - hardware-confirmed fine**: 5xBR and SABR are both
genuinely heavy per-pixel shaders (5xBR samples 15 neighbor texels and
runs a page of edge-detection logic per output pixel; SABR is similar), so
this was the real open question going in. Both run at a playable frame
rate on real, stock (non-overclocked) hardware, paired with the frame
skip setting already recommended for stock clocks - exactly the pairing
expected to be necessary, and it was.

**Failure isolation, since these are unverified on this GPU**:
`switch_render_init()` still hard-fails (as before) if D.Smile's own
Pixel/Sharp/CRT don't compile - that would mean something is fundamentally
broken. The 7 new ones are allowed to fail individually instead (logged via
the existing `CompileShader`/`LinkProgram` error prints); `switch_render_
frame()` falls back to D.Smile Pixel for any shader whose program failed to
link, rather than calling `glUseProgram(0)` and drawing nothing. So a
driver rejecting, say, SABR's shader source would mean that one menu entry
renders as plain nearest-neighbor pixels instead of what it's labeled -
noticeable, but not "nothing boots."

**Filtering**: 5xBR and SABR read specific neighbor texels via their own
precise `fract()`/`floor()` math and blend them themselves - hardware
bilinear filtering would pre-blend those samples and break the algorithm,
so (like D.Smile Pixel) they force `GL_NEAREST`. Every other shader here,
including Linear and Sharp Bilinear (whose whole design, per its own
name, leans on hardware bilinear for the final blend), uses
`GL_LINEAR` same as D.Smile Sharp/CRT already did.

**Two of these needed a platform-specific tweak, not just a rename**:
LCD1x and zFast LCD normalize their scanline-grid math against a
`NDS_SCREEN_HEIGHT` constant compensating for DraStic's optional "high-
resolution 3D rendering" mode (where its internal framebuffer can be a
multiple of the DS's native size). V.Smile has no such concept - this
core's framebuffer is always exactly `kFbW`x`kFbH` - so that compensation
term simplifies away entirely rather than needing a DS-specific constant
carried over; see the comments in `switch_render.cpp` right above those
two shaders for the derivation.

**Not attempted**: BIOS-side or per-shader tunable parameters (the
originals' `#define`-configurable constants are baked in as fixed `const`
values here, matching whichever preset - e.g. SABR's "Optimized" over
plain v3.0 - was picked above) and the `+` combo multi-pass presets
mentioned above.

**Full credit** (GPL-2-or-later; see
`reference/DrasticDS_nx-main/third_party/drastic-ds-shaders/COPYING` and
`NOTICE.md`, and the main README's Acknowledgements for the summarized
version): jdgleaver ported LCD1x (original by Gigaherz), Sharp Bilinear
(rsn8887 & TheMaister), zFast LCD (Greg Hogan/SoltanGris42), and Natural
Vision (ShadX, modified by Hyllian and Sp00kyFox) to DraStic's format.
5xBR v4.0 (Hyllian/Jararaca)
and SABR v3.0 (Joshua Street, "Optimized" variant) were ported to DraStic's
format separately - credited in their own source files as "Try791023"
rather than jdgleaver - and Linear is DraSticDS-nx's own small MIT-licensed
pass-through stub, not a ported third-party shader.

## New in the previous pass: zip game support

Games can now be `.zip` archives (one `.bin` inside each) as well as plain
`.bin` files, dropped into the same game folders. This started from a
partial attempt already sitting in
`reference/D.Smile_nx-feature-zip-game-support` (`switch/source/core/
switch_zip.cpp`): it listed `.zip` files in the browser fine, but launching
one silently did nothing.

**The actual bug**: that version's ZIP reader only handled two cases -
method 0 ("stored", i.e. uncompressed) and method 8 ("deflate") *but only
when the compressed and uncompressed sizes happened to match* - with a
`// TODO: Link zlib for proper DEFLATE decompression` where real inflation
should have been. Every mainstream zip tool (Windows' own "Compress to zip
file", 7-Zip's defaults, macOS Archive Utility) actually deflates its
contents, so compressed size never equals uncompressed size and that
branch always hit the TODO and returned false - `LoadGame()` in main.cpp
got an empty buffer, bailed out, and the ROM list screen just... sat there,
exactly what was reported.

**The fix**: real raw-DEFLATE decompression via zlib's `inflate()`
(`windowBits=-15` for the header-less raw stream a ZIP entry actually
contains), plus a CRC-32 check against the entry's own stored checksum so
a bad extraction fails loudly instead of feeding the core garbage. No new
dependency - `switch/Makefile` already links `-lz` for libpng's sake, and
zlib's headers come along with the same PORTLIBS include path. Rewritten
as `switch/source/core/switch_zip.{h,cpp}`, wired into
`switch_library.cpp` (list `.zip` files, but only ones that actually start
with a valid ZIP local file header - `switch_zip_is_valid()`) and
`main.cpp`'s `LoadGame()` (extract to memory before handing to
`VSmile::LoadCart()` - the emulated cart is never read from a still-
compressed file). Cover art, save states, rewind, and cart-side NVRAM all
key off the game's path with its extension stripped, same as `.bin`
always did, so none of that needed to change.

**Deliberately still unsupported**: a zip whose entry uses a streamed data
descriptor instead of writing real sizes into the local file header
(general-purpose bit 3 - some non-seekable/streaming zip writers do this).
Detected and rejected rather than misread; in practice this only shows up
from unusual zip producers, not from a person just right-clicking a `.bin`
and compressing it, so it wasn't worth the extra central-directory parsing
this would take to fix properly. Also unchanged: BIOS selection stays
`.bin`-only, and a zip's first `.bin` entry (by archive order) is the one
that gets loaded if more than one is present - name a zip after the game
you want it to contain one `.bin` in, same as before.

**Not yet hardware-tested** - this needs a real run to confirm the zlib
raw-inflate path actually behaves the same on-device as it does in theory
(no host-side test harness covers switch/-only code like this).

## New in the previous pass: header icon

The game grid/list screen's header now shows a small rounded-corner copy
of the mascot icon just to the left of the "D.Smile" title (the other
screens' headers - Settings, Paused, etc. - are unchanged). Generated once
from `switch/icon.jpg` (already the corrected full-bleed-square version
from the 1.0.0 icon fix below) via a rounded-rect alpha mask, so it reads
as the familiar rounded Android-launcher icon shape rather than the
squared-off `.nro` icon. Baked in as a small embedded PNG byte array
(`switch/source/render/switch_header_icon.h`) rather than a file on the SD
card or a Makefile/romfs asset pipeline change - decoded once at
`switch_menu_init()` with the same `stb_image` this project already links
for cover art, so it costs nothing per frame and nothing new to ship or
go missing. Purely cosmetic; no behavior changed.

## 1.0.0 release prep

- **A 6th theme, V.Smile, added and made the default** - see "Themes"
  under "What's here (files)" below for the palette and where it comes
  from.
- **`.nro` metadata** (`Makefile`'s `APP_TITLE`/`APP_AUTHOR`/`APP_VERSION`):
  title `D.Smile NX`, author `SenseiFromClubPenguin`, version `1.0.0`.
- **`icon.jpg` fixed** - the source app icon is an Android adaptive icon
  (`app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`: a solid
  `#F7941D` orange background plus a white mascot-face foreground,
  `ic_launcher_fg.xml`), which Android composites through a per-launcher
  mask shape (circle, squircle, rounded square, whatever that launcher
  wants) - full-bleed square by design, since the mask is applied at
  display time, not baked into the asset. Whatever originally exported
  `icon.jpg` for this Switch build baked in a *specific* rounded-square
  mask instead, with plain black (JPEG has no alpha) filling the four
  true corners outside it - fine for a homebrew icon shown as its own
  small rounded square in some contexts, wrong wherever it's shown as a
  plain square instead (dark, unfinished-looking corners). Fixed by
  flood-filling every pixel connected to the four corners that wasn't
  clearly part of the orange background or the white face (catching both
  the solid black and the JPEG ringing right at the old mask edge) with
  the exact `#F7941D` background color, then a light median-filter pass
  to clean up any remaining compression speckle at the seam. The
  mascot's face itself (including its black eyes, which are *not*
  connected to the border) is untouched.

## V.Smile Motion investigation: no motion controls in MAME either

Went looking for what MAME's `vsmilem` (V.Smile Motion) driver does for
the wand/motion controller, the same way Region's mapping was found in
MAME's source. Checked every place that support could live:
`vsmilem_state` (`src/mame/vtech/vsmile.h`) adds nothing over the standard
console except two empty stub callbacks (`porta_r()`/`porta_w()`, one
commented-out debug `printf`, never implemented); the controller slot list
(`vsmile_controllers`, `src/devices/bus/vsmile/vsmile_ctrl.cpp`) only
offers `joy` (the standard digital pad), `mat` (a dance-mat variant of the
same pad), and three regional ABC keyboard accessories - no wand device
at all; and the machine config wires the *same* default `"joy"` controller
for both the standard console and Motion. Searched every controller-device
source file for `motion`/`accel`/`wand`/`gyro`/`tilt` - zero matches
outside of ROM-loading comments describing what a boot logo says. So
"V.Smile Motion" support in MAME means Motion-branded BIOS/games boot and
run with the plain digital pad, no wand input modeled at all - there's no
reference implementation here to port, unlike Region. Real wand support
would be new engineering with nothing to check it against (most plausibly
mapping Joy-Con gyro/accelerometer data onto whatever the wand's IR-and-
tilt sensor reported, which nobody seems to have reverse-engineered and
published) - not attempted this pass; see "Two-player controller support"
and "Cart-side NVRAM saves" below for the two gaps this same investigation
found that *were* worth doing.

Worth reading alongside "Documented for later: other V.Smile peripherals"
below, specifically its note on V.Smile Motion/standard cross-
compatibility: MAME defaulting *both* the standard console and Motion to
the same plain `"joy"` controller (above) isn't just MAME punting on
unimplemented hardware - Motion cartridges are apparently genuinely
playable on standard V.Smile hardware with standard controls, motion
sensing aside. That reframes the wand gap a bit: it may not be blocking
as much actual gameplay as first assumed, since standard-controller
support alone could already make a real number of Motion titles playable.

## New this pass: two-player controller support

MAME's `vsmile_state` wires up two independent controller ports
(`m_ctrl[0]`/`m_ctrl[1]`, `src/mame/vtech/vsmile.cpp`), each its own
device with its own 4-color LED state - real V.Smile hardware supports two
players. This core's `VSmile` only ever had one `VSmileJoy joy_` - and,
digging in, the console-side GPIO/UART plumbing for a *second* port
already existed as unused stubs: `cts0_`/`cts1_` were both tracked, but
`GpioOut()` only ever forwarded `cts0_` to a controller object, and
`GpioIn()`'s Port C read hardcoded controller 2's RTS bit permanently
"idle" (`// controller 2 RTS always idle`) instead of asking a second
controller object. `Reset()` even already raised *both* controller-request
IRQ lines ("Real hardware boots with both controller-request lines
pending"). All of which suggests this was scaffolded for two controllers
from early on and just never finished - finishing it turned out to be
mostly wiring, not new protocol work:

- `VSmileJoy` now takes which of the console's two controller-IRQ lines
  its RTS transitions raise (`ext_irq_line_`), since each physical port
  has its own line (`Spg200::RaiseExtIrq(0)` = EXT1, `(1)` = EXT2).
  `VSmile` has a `joy2_` alongside `joy_`, wired into `GpioIn()`/
  `GpioOut()`/`UartTx()`/`UartRxDone()`/`RunCycles()` exactly the way
  `joy_` already was, plus a `SetInput2()` to feed it. Save states gained
  a `joy2_` block (state version bumped 5 -> 6; older saves migrate by
  just leaving player 2 in its Reset() state, matching how v5's own
  fields already migrate older saves). This is core code
  (`app/src/main/cpp/core/vsmile.{h,cpp}`), so Android would get the same
  capability for free if it ever wires up a `SetInput2()` caller of its
  own - nothing about this pass is Switch-specific at the core level.
- Fully automatic, no setting - see "Two-player controller support: the
  `UartRxDone()` regression, and dropping the pairing applet" below for
  the full story, but in short: `switch_input_player2_ready()`
  (`padIsConnected()` on a second `PadState` initialized against
  `HidNpadIdType_No2`) is checked every frame during gameplay, and its
  result both decides whether to poll/feed player 2's input that frame
  and is passed straight to a new `VSmile::SetPlayer2Connected()` (see
  below) - a real Switch controller being connected *is* what "2 players"
  means here, nothing to configure. Both players share the same
  action-binding table; the two physical V.Smile pads are identical, so
  there's no separate "player 2 controls" screen to build either.
- `VSmile::SetPlayer2Connected(bool)` gates whether `joy2_` participates
  in the UART/RTS protocol *at all* (`RunCycles()` only steps it while
  connected). Without this, `joy2_` would keep sending its periodic
  keepalive/probe traffic purely on its own internal timing regardless of
  whether a real second controller exists, which would make a game think
  "something's in port 2" even with nothing connected - not a real
  concern with a fixed one-or-two-players setting, but a real one once
  that's driven by live, possibly-flickering connection state every
  frame.

## Two-player controller support: the `UartRxDone()` regression, and dropping the pairing applet

First hardware test broke in-game controller input completely - menu
navigation and Rewind kept working (neither touches the code below), but
no V.Smile button or joystick press reached a running game. Root cause was
in `VSmile::UartRxDone()` (`vsmile.h`), the callback that fires when the
console's UART finishes receiving a byte a controller sent - it acks that
controller by clearing its `tx_busy_` flag, which is what lets it send the
*next* byte (`StartTx()` refuses to run again while `tx_busy_` is still
true). This pass changed it from unconditionally acking `joy_` to acking
whichever of `joy_`/`joy2_` matched the *current* value of `cts0_`/`cts1_`
at the moment the callback fired - which is wrong, because those flags can
change between a controller starting a transmission and this callback
arriving for it. When that happened, neither controller got acked,
`tx_busy_` stayed stuck true forever, and that controller could never send
another byte again - game input silently dead after the first stall,
exactly matching what showed up on hardware.

Fixed by going back to calling both unconditionally
(`joy_.TxDone(); joy2_.TxDone();`) - safe to do because `TxDone()` already
guards itself (`if (!tx_busy_) return;`), so acking a controller that
isn't actually mid-transmission is a harmless no-op. No cts-based
filtering was ever necessary; the original single-controller code didn't
have any either, for the same reason. **Confirmed fixed on a second
hardware round** - player 1 input works again.

**The controller-pairing applet was removed, not fixed.** It never
visibly appeared when switching Players to 2 - screen dimmed briefly, as
if starting the transition, then returned without showing anything - and
the diagnostic added to chase it (`rc=00005d59 players=0`) decoded
cleanly: module 345 is `Module_Libnx`, description 46 is
`LibnxError_LibAppletBadExit` (`nx/include/switch/result.h` in
[switchbrew/libnx](https://github.com/switchbrew/libnx)). Checking
libnx's own `hid_la.c` source
([`nx/source/applets/hid_la.c`](https://github.com/switchbrew/libnx/blob/master/nx/source/applets/hid_la.c#L75)),
that specific error means the applet *did* launch and *did* exit, but
reported a non-zero internal result on the way out (`res.res != 0`) -
consistent with a handheld-mode restriction
`HidLaControllerSupportArgHeader::enable_single_mode` documents ("Using
handheld-mode is not allowed if this is not enabled") but not something
worth patching blind a second time, especially once a better option
turned up.

At the user's suggestion, checked `reference/NetherSX2_nx-main` - another
homebrew emulator with real, working multiplayer support - for how it
handles this. It doesn't call any pairing applet at all:
`padConfigureInput(MAX_CONTROLLERS, ...)` once, `padInitialize(&pads[1],
HidNpadIdType_No2)` once, then just polls both every frame regardless of
whether anything's actually connected to the second slot - identical to
what this port already had built for its own two `PadState`s. It relies
entirely on the player pairing a second controller through the system's
own means (most simply, the HOME-menu overlay) outside the app, with no
in-app prompt at all.

Adopted the same approach: `switch_input_show_pairing_applet()` and its
whole call site are gone, and so is the manual **Players** setting - once
pairing itself was no longer blocked, connection detection worked and the
setting turned out to be redundant on top of it. `switch_input_player2_ready()`
is polled live every frame during gameplay instead, and whatever it
returns *is* the two-player state for that frame - connect a second
controller (paired however, most simply the system's own HOME-menu
overlay) and it's automatically in as player 2; disconnect it and the port
goes back to empty. No applet, no setting, no `LibAppletBadExit` risk, one
less screen to navigate.

### "Crosstalk": either controller could drive both players - resolved, not a bug

**Resolution (after 1.0.1):** this was the test game, not the emulator.
The title used for these rounds only has players taking turns, and it
intentionally accepts input from either controller port. Games with real
simultaneous two-player keep the two controllers cleanly separated, and
with only one Switch controller connected their two-player modes are
correctly unavailable. No code change needed; the history below is kept
because the three genuine fixes it produced (the `UartRxDone()`
regression, the pairing applet, and Single-vs-Dual assignment mode) are
all still real.

Third hardware round, with detection itself finally working: both
controllers connected produced full crosstalk - pressing a button on
*either* physical controller moved *both* V.Smile players. Worth being
precise about what this rules out: `VSmile::SetInput()`/`SetInput2()`
route to two fully separate `VSmileJoy` instances (`joy_`/`joy2_`) with
entirely independent state (`vsmile.h`), so this isn't a core-level mixup;
and the Switch-side `PadState` setup - `padInitializeDefault(&s_pad)` for
player 1, `padInitialize(&s_pad2, HidNpadIdType_No2)` for player 2 - is
*exactly* the same code that existed under the old manual-toggle version.
That's the important part: reverting to that older version would not have
fixed this, since the bug (if it's where this reasoning points) lives in
how the two `PadState`s get *populated* by the system, not in anything
this port's own code decides to do with them.

Lead found in libnx's own HID API: neither this port nor, as far as could
be checked, `reference/NetherSX2_nx-main` (whose read pattern this port
already matched) ever explicitly pins each player slot to one specific
controller - both just read `HidNpadIdType_No1`/`No2` directly and trust
the system's own default pairing behavior to keep them cleanly separated.
First attempt at this,
`hidSetNpadJoyAssignmentModeSingleByDefault(HidNpadIdType)`, was wrong and
caused a real regression, confirmed on hardware: it explicitly requests
*single* (sideways, split) assignment mode, so every connected Joy-Con
*pair* got split into two independent one-Joy-Con controllers instead of
staying combined - player 1's pair became two "controllers" (one landing
on player 1, the other on player 2, each in sideways mode), same for
player 2's pair. Exactly backwards from what's wanted: this port only
means to support combined Joy-Con pairs, Pro Controllers, and comparable
USB controllers - never single/sideways Joy-Con mode.

Fixed by calling `hidSetNpadJoyAssignmentModeDual(HidNpadIdType)` instead
- pins each slot to a *combined pair* rather than forcing a split. Also
tightened `padConfigureInput()`'s requested style set from
`HidNpadStyleSet_NpadStandard` (`{FullKey, Handheld, JoyDual, JoyLeft,
JoyRight}`) to `HidNpadStyleSet_NpadFullCtrl` (`{FullKey, Handheld,
JoyDual}`) - dropping `JoyLeft`/`JoyRight` means a single sideways
Joy-Con shouldn't be offered as a valid controller for this app at all,
not just discouraged by the assignment-mode call. Both changes are
best-effort/no-ops for anything that isn't a bare Joy-Con pair (Pro
Controllers and similar have no left/right pairing to begin with).

**Fourth hardware round: Dual mode fixed the splitting (Joy-Con pairs
stay combined again), but the original crosstalk is still there.**
Confirmed with two combined Joy-Con pairs specifically - either
controller still drives both players.

One real (but ultimately unrelated) bug found while looking further:
`switch_input_player2_ready()` and `switch_input_poll_p2()` were each
calling `padUpdate(&s_pad2)` independently, meaning it ran twice a frame -
harmless for the `held`-button reads this port actually uses (gameplay
input, recomputed fresh either way), but it would silently eat single-
frame `down`/`up` edges on the second call (the first call already moves
any edge into "old" before the second one reads it), a real problem if
anything ever needed player 2's edge-triggered input later. Fixed by
making `switch_input_poll_p2()` the only place that updates `s_pad2`, and
having `switch_input_player2_ready()` just read whatever that last saw -
`main.cpp` now calls them in that order every frame. Confirmed not the
cause of the crosstalk itself, since gameplay input never depended on
edges to begin with, but worth having fixed regardless.

On the theory that "the old manual-toggle version separated inputs
cleanly" - re-checked, and it doesn't hold up structurally: the
`PadState` setup that actually determines which physical controller
lands in which slot (`padInitializeDefault()`/`padInitialize(...,
HidNpadIdType_No2)`, now also the `hidSetNpadJoyAssignmentModeDual()`
calls above) is identical in both versions, and - worth being honest
about - two-player *gameplay* was never actually reachable to test under
the old version either, since its pairing applet never worked. There's
no direct evidence the old version was ever crosstalk-free; it's more
likely the crosstalk was already present and simply never got far enough
to be seen.

**Original decision (at 1.0.0): ship with this as a known limitation.**
The root cause couldn't be found - and in hindsight that's because there
wasn't one in this port: see the resolution note at the top of this
section. The other community-documented detail that fits this is that the
V.Smile has a single UART shared by both controller ports, and each game's
own code decides which port to listen to (see
`reference/findings.md` §3) - so a turn-taking game reading both ports is
ordinary game behavior, not a hardware or emulation fault.

## New this pass: cart-side NVRAM saves

MAME's `hash/vsmile_cart.xml` software list has a `vsmile_nvram` cart type
for cartridges with their own battery-backed SRAM - as far as that list
shows, exactly one real title uses it: **V.Smile Art Studio**, a drawing
game whose saved pictures live on the cartridge itself, plus its four
regional re-releases (Germany, France, Sweden, Spain). This core already
had a *stub* for this - `Spg200::SetCart()` takes an `art_nvram` pointer,
read/written at the cart's bank-2 address window (`spg200.cpp`'s
`ExtRead()`/`ExtWrite()`) - but `VSmile::LoadCart()` always passed
`nullptr`, so the hook existed and was never connected to anything.

- `VSmile::LoadCart()` now CRC-32-hashes the raw cart dump and checks it
  against the 5 known `vsmile_nvram` titles' hashes from MAME's software
  list (`kNvramCartCrc` in `vsmile.cpp`) - matching the exact same
  "identify a specific dump by hash" approach the BIOS/Region investigation
  already used. A match allocates a 128K-word (`kNvramWords`, matching the
  existing `& 0x1FFFF` mask `Spg200::ExtRead()`/`ExtWrite()` already had -
  MAME's own softlist declares the *real* SRAM chip as 128KB/65536 words,
  smaller than this core's addressable window; a real PCB likely aliases
  the difference away in a way this core doesn't model, since it isn't
  observable from software behavior) buffer and attaches it; every other
  cart gets `nullptr`, unaffected, exactly as before. `HasNvram()`,
  `SetNvram()` (load), and `GetNvram()` (read back for saving) are the new
  public surface - again, core code, so this benefits Android too if it
  ever calls them.
- Included in save states/rewind too (only for a cart that actually has
  NVRAM - the `if (!nvram_.empty())` in `SaveState()`/`LoadState()` means
  every other cart's state size is completely unaffected). Worth knowing:
  for Art Studio specifically, this makes each snapshot ~256KB bigger than
  usual, so Rewind's 900-snapshot ring buffer costs meaningfully more
  memory on that one title (~250MB worst case vs. ~20MB for a normal cart)
  - still comfortably within a Switch's RAM budget, not a real concern,
  just not the same tiny footprint every other game gets.
- On the Switch side: a new `sdmc:/switch/dsmile/nvram/` folder, separate
  from `states/` - deliberately, since this mirrors what the physical
  cartridge's own SRAM does on real hardware (survives regardless of
  anything this port's own save-state system does), not another slot-based
  save mechanism. One file per cart, named after it like state files are,
  but with no slot number (`<cart-name>.nvram` in `main.cpp`'s
  `NvramFilePath()`) - there's only ever one "current drawing" on a real
  cartridge, no multi-slot concept to mirror. Loaded right after a fresh
  `LoadCart()` succeeds (a no-op for every other cart); written back via a
  new `FlushNvram()` right before every place the running `VSmile` gets
  torn down - the pause menu's Quit, the L+R+Plus hard-quit, and app exit
  (including quitting via HOME/power mid-game, not just the two in-app
  paths) - so nothing's lost between "still playing" and "cart's been
  swapped out."

## Noted, not implemented: ABC/Smart Keyboard accessory

**Superseded** - implemented with a physical USB keyboard instead of the
`swkbd` idea below; see "New this pass: Smart Keyboard (USB keyboard)"
above. Kept for history.

MAME's controller slot list also offers three regional ABC/Smart Keyboard
accessories (`smartkb_us`/`_fr`/`_ge`, `src/devices/bus/vsmile/
keyboard.cpp`) for the handful of games bundled with one. One idea floated
for this port: a bindable action that summons the Switch's own on-screen
`swkbd` keyboard applet whenever a game wants text input. Left as an idea
rather than attempted this pass - the open question is whether `swkbd`'s
output (a UTF-8 string, entered all at once and submitted) can actually be
translated back into the V.Smile keyboard accessory's real protocol
(individual keystrokes, one at a time, matching whatever
`vsmile_keyboard_device` in MAME expects) closely enough for a keyboard-
accessory game to actually work, or whether the mismatch between "type a
whole string, then submit" and "press one key" input models makes this a
poor fit regardless of effort spent. Lowest priority of the three gaps
found this pass - very few titles use this accessory at all.

## Noted, not implemented: V.Smile Art Studio's digitizer/pen

Confirmed cart-side NVRAM works on real hardware with a real Art Studio
cartridge - but that same testing surfaced that the game itself expects a
digitizer/stylus input for actually drawing, on top of the standard
joystick/buttons this port already handles. **Two physically distinct
digitizer controllers exist, confirmed by the user against real units in
hand, not just photos of one**:

- A smaller digitizer area combined with a joystick, on the standard
  purple/orange controller shape - used across **multiple** games, not
  just Art Studio.
- A dedicated tablet-style digitizer, orange, with Art Studio's own
  drawing HUD (color palette, tool icons) printed directly onto the
  controller surface next to the touch area - specific to Art Studio.

These are two different peripherals with two different scopes (general-
purpose vs. single-game), not two versions of the same thing - worth
keeping distinct if this is ever implemented, rather than assuming one
implementation covers both.

Checked MAME's controller slot list again (the same one consulted for the
ABC/Smart Keyboard note above) - no digitizer/tablet device type exists
there either, only `joy`/`mat`/`smartkb_*`. Same situation as the Motion
wand: no reference implementation anywhere to check against for either
variant, so this would be new protocol reverse-engineering, not a port.
Noting it here so it isn't lost, per request - not investigated further
this pass.

**Update:** Art Studio is fully playable with the standard controller -
drawing included - both in this port and on real hardware (the chat
research in `reference/findings.md` §2 has an owner confirming the same on
a real console). So the drawing pad is a "nicer input" feature, not a
blocker. The goal if this is ever picked up: drive either pad from the
Switch touch screen. What's still missing is the protocol - nobody has
captured the tablet's or writing pad's traffic (the chat's closest attempt
saw console→tablet bytes but never tablet→console).

## Documented for later: other V.Smile peripherals

Web research pass, at the user's request, to at least document what's
publicly known about several other V.Smile accessories this port doesn't
support - not an implementation attempt, and sourced from general web
searches (VTech's own manuals, Wikipedia, fan wikis) rather than anything
as authoritative as MAME's source, so treat specifics here as a starting
point for later investigation rather than verified fact.

**Microphone (V.Mic)** - introduced with the V.Smile Pocket, used for
"sing-along" modes in specific games; some consoles connected it via a
plain 3.5mm aux cable (meaning any compatible mic could stand in for the
official one). No source found describing exactly how a game reacts to
it, but this core's `Spg200` already has a generic 4-channel ADC
peripheral (`adc_ctrl_`/`TickAdc()`, `spg200.cpp`) with a 2-bit channel
select - `VSmile::AdcIn()` only ever handles channel 1 (hardcoded battery
level), returning 0 for every other channel. A microphone reading its
input level (volume, not full digitized audio - consistent with a "make
noise to trigger something" sing-along mechanic rather than real pitch
detection or audio pass-through) through one of the *other* three ADC
channels would be a very plausible, cheap hardware design for this era,
and would be a real, checkable hook if this is ever pursued - but this is
inference from the chip's own capabilities, not a confirmed pinout.

**Smartbook** - a book-holder peripheral with its own touch-sensitive
surface and a "Smart pen"; VTech's own manuals describe the book surface
as knowing which page is open, with the pen directing play on that page.
The right side of the unit has the same buttons as a standard controller,
and the pen can be inserted into a holder to act as an ordinary joystick
when not being used on the book. Structurally this sounds like the same
kind of touch-surface-plus-pen mechanism as the Art Studio tablet
digitizer above - if either is ever reverse-engineered, the other is
worth revisiting with whatever was learned.

**Jammin' Gym Class (dance mat)** - *superseded: the conclusion below was
wrong (see `reference/findings.md` §0); the mat sends different bytes and is
now its own controller type, see "New this pass: dance mat, stick levels,
BIOS test screen".* Original note:
Checked MAME's `bus/vsmile/mat.h`/`mat.cpp`: `vsmile_mat_device` is
built on the *exact* same protocol as the standard pad
(`vsmile_pad_device`, the one this port already implements), just with
its 12 floor panels wired to the same button set a handheld pad already
has - Yellow/Right, Red/Left (the mat's "joystick" zone), Center/Up/Down/
Green (colors), and OK/Quit/Help/Blue (the function buttons). Confirms
the hypothesis exactly: this needs no new protocol work at all if
someone wants to treat a mat as just another controller under this
port's existing button-remapping system - it already speaks the same
language as everything this port supports today.

**V.Smile PC Pal** - a console revision bundling a wireless keyboard
with its own built-in joystick, two Enter/OK buttons, and a retractable
digitizer pad/pen (all one unit - the "Smart Keyboard" idea expanded),
plus a *separate* puck-shaped trackball mouse (one click button) that
connects over infrared. Whether the keyboard/joystick/digitizer unit
also uses infrared, or a direct wired controller-port connection like
the plain Smart Keyboard, wasn't confirmed either way in what was found -
worth pinning down specifically before assuming IR covers the whole
peripheral rather than just the mouse.

**V.Link** - as expected, information is extremely limited. Confirmed to
exist (a flash-drive-like device, proprietary connector on one end and
USB on the other, used for tracking progress and unlocking online bonus
content) and confirmed to only work with a handful of specific consoles
(Cyber Pocket, PC Pal, V.Motion, and a few newer V.Smile revisions) - but
no protocol documentation of any kind turned up. Given the underlying
online service this depended on is almost certainly long defunct, this
one seems genuinely unlikely to be practically supportable regardless of
reverse-engineering effort.

**V.Smile Motion / standard V.Smile cross-compatibility** - the user's
own hardware knowledge, cross-checked against what's publicly documented:
V.Smile Motion cartridges are also playable on a *standard* V.Smile
console (or the Motion console in its non-motion mode), just without the
motion-sensing features - meaning every V.Smile Motion game has a
standard-controls mode by design, not just in theory. This matches what
general web sources describe (with some source disagreement on the
Motion-console-playing-standard-carts direction, but consistent agreement
that Motion carts run on standard hardware minus motion input). Relevant
if the Motion wand is ever revisited: a Motion game isn't necessarily
*only* playable with wand input the way this port previously assumed
might be the case - it may be that standard-controller support alone
already makes a good number of Motion titles playable today, without
needing the wand at all. Worth testing against an actual Motion cart
whenever convenient.

## New in the previous pass: BIOS and Region locked to the main menu

Changing BIOS or Region only ever takes effect on the *next*
`VSmile::LoadGame()` - a fresh instance's `LoadSysrom()`/`SetRegion()` call,
at launch - so being able to change either mid-game (from the in-game pause
menu's Settings) would silently do nothing to the game actually running,
which looks like the setting is broken rather than "takes effect next
launch." Settings > Library & Storage's BIOS and Region rows are now hidden
- not shown-but-disabled, same convention `VisibleLauncherRows()`/
`VisibleGraphicsRows()` already use for rows that don't currently apply -
whenever this screen was reached from the pause menu instead of the main
grid (`g_settings_return_to == Screen::InGamePause`, the same flag that
already tracks where Settings should go back to). `switch_menu.cpp`'s new
`VisibleLibraryStorageRows()` builds the row list dynamically now instead
of a fixed 5, mirroring the Launcher/Graphics screens' own pattern. Game
Folders, Storage, and Rescan library are unaffected - they don't have this
same "looks broken until next launch" problem.

## Audio dropouts: investigated, mitigated, not conclusively root-caused

Reported symptom: sound sometimes goes silent on launching a game -
sometimes after switching between a few, occasionally on the very first
game after booting the app - and stays silent until the whole app is
relaunched (not just the game). A second variant: audio plays for the
VTech logo, then goes silent on the V.Smile logo animation, same "only a
full relaunch fixes it" recovery.

What was actually established, versus what's still a working theory:

- **Read libnx's actual `audrv` source** (`nx/source/audio/driver.c` and
  `voice.c`, fetched from
  [`github.com/switchbrew/libnx`](https://github.com/switchbrew/libnx) -
  not available as source locally, only as a precompiled library, so this
  had to come from upstream) rather than guessing at how the wavebuf queue
  behaves. Confirmed: `AudioDriverWaveBuf.state` transitions (Queued ->
  Playing -> Done -> Free) only ever happen inside `audrvUpdate()`, which
  this port only calls from inside `switch_audio_push()` - so any wavebuf
  slot that manages to get stuck in a non-`Free`/non-`Done` state forever
  would make this port's "drop this frame's audio if the slot we'd reuse is
  still in flight" backpressure check (`switch_audio.cpp`) permanently
  drop every future push on that slot, since it never advances past a
  stuck slot - total, silent, permanent dropout, matching the report
  closely. This is a solid mechanism that *would* explain it.
- **What's not established**: *why* a slot would get stuck in the first
  place. Tracing every call site (`switch_audio_init`/`_push`/`_shutdown`,
  and every place `main.cpp` calls into them) didn't turn up a bug in how
  this port drives that queue for ordinary continuous playback - the logic
  reads as correct for the steady-state case on paper. No host C++ compiler
  or hardware access was available in this environment to actually
  reproduce it or instrument `audrvUpdate()`'s return code live, so the
  exact trigger (a transient failure from the audio renderer service
  itself? something specific to the moment of a game reload/reset?)
  remains unconfirmed.
- Ruled out as the cause: `Spu::Reset()` does clear its internal sample
  ring (`audio_pos_ = 0`) on every reset, so there's no stale backlog
  bug there; and the resampler's output cap (`kMaxOutFrames`) has enough
  headroom over the SPU's actual per-frame output that its theoretical
  edge case (truncating mid-buffer) can't be reached with this port's
  buffer sizes - both looked like plausible leads and both checked out
  clean on inspection.

Given the mechanism (a stuck wavebuf slot, or a session that never
initialized in the first place) was solid even without a confirmed trigger,
`switch_audio.cpp` now **self-heals** instead of needing a manual relaunch:

- If the slot `switch_audio_push()` is about to reuse has been stuck
  (neither `Free` nor `Done`) for more than ~3 seconds of frames in a row -
  far beyond any normal one-or-two-frame backpressure drop - it tears down
  and recreates the entire audio driver (`switch_audio_shutdown()` +
  `switch_audio_init()`), exactly what a full app relaunch was already
  doing to fix it, just automatically.
- If audio never initialized successfully in the first place (or a self-
  heal attempt itself fails), it retries on a ~5-second cooldown instead of
  giving up for the rest of the process's life - covers the "no sound even
  on the first game after boot" variant of the report, which a stuck-slot
  check alone wouldn't catch (that path never even reaches the stuck-slot
  logic if the voice was never up to begin with).
- `main.cpp` no longer gates pushing audio on a one-time `switch_audio_init()`
  result captured at startup - it always calls `switch_audio_push()`, which
  now manages its own up/down state internally, so a later self-heal can
  actually take effect instead of being permanently skipped by a stale
  startup-time flag.

Worth being direct about what this is and isn't: it's a mitigation for the
*symptom* (unbounded silence), verified by careful reading of the actual
driver source rather than guesswork, not a confirmed fix for a specific
root cause - that part honestly couldn't be nailed down without hardware
access to reproduce and instrument it live. If the dropout still happens
after this, it should now recover within a few seconds on its own (a brief
audio hiccup) instead of staying silent until a full relaunch - worth
reporting back either way, since "it still happens but now recovers" vs.
"it still never recovers" would mean very different things about whether
the stuck-wavebuf theory is actually the right one.

## New in the previous pass: BIOS picker

Settings > Library & Storage > **BIOS** now opens a picker listing every
`.bin` file directly inside `sdmc:/switch/dsmile/bios/`, instead of only
ever loading a file hardcoded as `sysrom.bin`. Selecting one sets
`g_settings.bios_file` (just the filename, not a full path) and saves
immediately; the Library & Storage row itself shows whichever BIOS is
actually in effect, not just "Found"/"Not found" as before. If nothing's
been explicitly picked yet - or the picked file has since been deleted from
the card - it falls back to whichever `.bin` sorts first alphabetically, so
the original zero-configuration case (drop in one file, any name, it just
works) still holds without a trip into the picker. An empty bios folder
shows a single non-selectable "No .bin files" row rather than an empty list.

## BIOS picker: language not changing, and the new Region setting

First hardware test of the picker: switching from an English to a German
BIOS dump (tried both re-launching just the game and relaunching the whole
app) didn't change the displayed language at all. The picker itself is
doing exactly what it's coded to do - confirmed by re-reading every line
between the picker's selection and `LoadGame()`'s `LoadSysrom()` call, and
nothing there is wrong. The actual cause is one level deeper, in the shared
core: `VSmile::GpioIn()` (`app/src/main/cpp/core/vsmile.cpp`) returns
`region_ & 0xF` on Port C - a real V.Smile's region/language jumper pins,
per this core's MAME SPG2xx heritage (see the main README's
acknowledgements) - and the BIOS reads *that* to decide which language to
display, on real hardware and here alike. Real VTech hardware apparently
reuses one BIOS datamask across regions and selects the language with
these pins rather than shipping region-specific firmware, which is also
why swapping BIOS *files* alone doesn't change anything: `region_` defaults
to `0xF` (US English) in `vsmile.h` and - checked across the entire
codebase - `VSmile::SetRegion()` was never called from anywhere, not here
and not in Android's `jni_bridge.cpp` either. This isn't a bug introduced
by the picker; it's a pre-existing gap in what either platform ever exposed.

Added Settings > Library & Storage > **Region** (Left/Right to adjust) to
actually expose it - a raw 0-15 value. The user tried cycling it by hand
and confirmed it does change the displayed result, and asked for the
mapping to be figured out properly instead of documenting it by manual
trial and error across every BIOS dump. See the next section for how that
was actually found, and what it turned out to be.

## Region mapping: found via MAME's own driver source, not guesswork

Rather than guess at labels, or reverse-engineer the eight real BIOS dumps
dropped into `reference/bios/` by hand, the authoritative answer turned out
to already be public: this core's `region_`/Port C read
(`VSmile::GpioIn()`) is a line-for-line match for MAME's own
`vsmile_state::portc_r()` (`src/mame/vtech/vsmile.cpp`) - same bit
positions for the controller RTS lines and the always-set IOC5 test point,
same low nibble for region - and MAME's driver defines that nibble as a
named `PORT_CONFNAME("Language")`/`PORT_CONFSETTING` machine configuration,
which for real hardware is normally set via MAME's UI rather than picked
by the software at all. Fetched straight from
[`github.com/mamedev/mame/.../vsmile.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/vtech/vsmile.cpp):

**Standard V.Smile** (`v100`/`v102`/`v103`, and anything else that isn't a
Motion BIOS):

| Code | Language       |
|------|-----------------|
| 0x02 | Italian         |
| 0x07 | Chinese         |
| 0x08 | Portuguese      |
| 0x09 | Dutch           |
| 0x0b | German          |
| 0x0c | Spanish         |
| 0x0d | French          |
| 0x0e | English (UK)    |
| 0x0f | English (US) - the core's and this port's hardcoded default |

**V.Smile Motion** (`vsmilemotion.bin`, `vmotionbios.bin`) uses a
*different* table - MAME's own comments flag some of these as uncertain
(e.g. Italy was reportedly 0x0a in an earlier firmware revision) and
logo/voice/cartridge-art differences rather than pure language swaps for a
few entries, carried over as-is rather than cleaned up:

| Code | Region/variant   |
|------|-------------------|
| 0x02 | Italy             |
| 0x05 | English (1)       |
| 0x06 | English (2)       |
| 0x07 | China             |
| 0x08 | Mexico            |
| 0x09 | Netherlands (?)   |
| 0x0b | Germany           |
| 0x0c | Spain             |
| 0x0d | France            |
| 0x0f | English (3)       |

Codes not listed in either table (0x00, 0x01, 0x03, 0x04, 0x0a, and 0x06/
0x05 on the standard system) are genuinely undocumented in MAME's own
driver, not an omission here.

The Library & Storage Region row now shows the actual language name next
to the number (`switch_menu.cpp`'s `RegionLabel()`), not just the raw
nibble - and picks the right table automatically from the active BIOS
file's CRC-32 rather than guessing from the filename, which the user could
rename to anything.

**Superseded (later pass):** the standard-BIOS table above is MAME's, and
it turned out to be right only for v1.03 - see "Region labels per BIOS
version" under "New this pass: player 2 bindings and BIOS version
detection" for the per-version table now in use. The Motion table above
is still current.

**A second, unrelated finding from the same investigation**: of the 8 BIOS
files dropped into `reference/bios/`, CRC-32 hashing showed `bios
german.bin` is byte-identical to `vsmile_v100.bin`, and `sysrom_france.bin`
is byte-identical to `vsmile_v102.bin` - both are just re-named duplicates,
not independently-different firmware (consistent with this whole
investigation: the filenames some collectors give these dumps describe
where they were pulled from, not what language they necessarily boot to -
that's set by Region, above). More importantly, `vsmilebios.bin` doesn't
match any known MAME dump directly, but byte-swapping every 16-bit word of
it produces an exact match for `vsmile_v102.bin` - it's **v102 stored in
the opposite word-endianness** from what `VSmile::LoadSysrom()` expects
(`data[i*2] | (data[i*2+1]<<8)`, little-endian words). Loading
`vsmilebios.bin` as-is would feed the interpreter scrambled instructions -
likely a hang, crash, or garbage screen, not just a wrong language.
**Now handled automatically** (later pass): known byte-swapped dumps are
detected by CRC and un-swapped at load - see `core/switch_bios.cpp`.

## Fast Forward: tried and removed

Fast Forward was implemented, tested on hardware, and found to barely speed
anything up - Rewind worked perfectly the same pass, so the *architecture*
wasn't the problem. Checked both reference apps before concluding anything,
since guessing wrong here would mean shipping a no-op twice: DraStic's own
fast-forward (`reference/DrasticDS_nx-main/source/main.c`) isn't portable
at all - it's a config bit handed to its prebuilt, closed-source DS core,
with no source and no equivalent hook into `VSmile`. Android's own
(`app/src/main/cpp/jni_bridge.cpp`'s `Loop()`) turned out to be the useful
reference, and the useful finding: it doesn't batch `RunFrame()` calls
either - it calls it exactly once per loop iteration, same as this port,
and gets its speedup purely by shortening (or, uncapped, skipping entirely)
the sleep between iterations. That's computationally the same thing
skipping `eglSwapBuffers()` already did here, so there was no missing
"batch N frames, then present" mechanism to add. What it confirmed instead:
the weak result is a real compute ceiling on `VSmile::RunFrame()`/
`Ppu::DrawLine` at stock (non-overclocked) clock - skipping presentation
only recovers speed proportional to whatever slack exists between
`RunFrame()`'s actual cost and the 16.6ms frame budget, and Frame Skip
needing to kick in at all for smooth 1x is evidence there wasn't much. One
safe, provably-correct lever was pulled regardless - see the sprite-scan
dedup under "performance" below - but rather than ship a "Fast Forward"
that mostly doesn't fast-forward, it's been removed outright:

- `GameAction::FastForward` and everything that read it are gone, not just
  hidden: the Controller binding row, the pause menu's Fast Forward ON/OFF
  row, Settings > Graphics > Fast Forward Speed, and the skip-presentation-
  on-a-ratio logic in `main.cpp`.
- The `StickR` default binding it used now defaults to **Load State**
  instead (previously unbound by default) - `GameAction`'s count dropped
  from 13 to 12.
- Rewind is unaffected and stays exactly as described below - it's the one
  half of this feature that actually delivered, and is staying enabled.

## Rewind

Hold `Rewind`'s bound button (default StickL - no menu toggle, same as
Android, which doesn't have one for this either) - a ring buffer of one
`VSmile::SaveState()` snapshot per simulated frame, ~15 seconds of history
(900 frames, same cap Android's `jni_bridge.cpp` uses). Popping and loading
the most recent snapshot each rewinding frame, then simulating one frame
forward from it, steps the machine back exactly one recorded frame per real
frame - the same design Android's `Loop()` uses, ported faithfully
including *why* it works (the first rewind frame reloads the current state
as a harmless no-op, since the last snapshot was taken right after the
frame that just played - seeing this the first time and wondering why
rewind doesn't visibly jump backward until the *second* held frame is
expected, not a bug). Audio is discarded while rewinding, matching Android
(played back it would just be noise, not a coherent reversed track). Gets
reset (history cleared) on a fresh game load or returning to the grid -
loading a different cart's snapshots would just fail `LoadState()`'s
checksum guard anyway, but there's no reason to keep them in memory.

## New in the previous pass: performance

**Build flags (free, zero behavioral risk):** `-O2` -> `-O3` (this now
actually matches the core's own `CMakeLists.txt`, which a previous
revision of this file's comments incorrectly claimed was already true) and
**LTO** (`-flto=auto`, both compile and link flags). LTO matters more than
it might sound: `UnSP::Step()` (`unsp.cpp`) calls `Spg200::Read()`/`Write()`
(`spg200.cpp`) on *every single instruction fetch and operand access* -
the hottest call in the entire emulator - but the two live in different
`.cpp` files, so without LTO that was a genuine non-inlinable function call
every time, for build-structure reasons alone, not any correctness need.
Neither change touches a single line of the shared core itself, so Android
is completely unaffected.

**Sprite-scan dedup in `Ppu::DrawLine`** (revisited after Fast Forward
testing showed little speedup - see "Fast Forward: tried and removed"
above): this was previously "found, not changed" over the same draw-order-
regression worry as this section's own LTO note raised for unrelated code -
8 full passes over the 256-entry sprite table per scanline (2 passes x 4
layers, re-scanning every slot from scratch each time) just to find which
sprites belong to which layer. Fixed by bucketing all 256 slots into
per-(layer, blend) index lists in one pass *before* the layer loop, then
having the layer loop walk those small lists instead of re-scanning -
sprite registers can't change mid-`DrawLine()` (no bus writes happen
between here and the draws below), and bucketing in increasing index order
reproduces the exact same per-layer draw order the old nested scan
produced, so this is a pure redundant-work removal, not a behavior change.
Cuts the fixed per-scanline scanning cost roughly 4x (2048 slot checks ->
256), though this is unlikely to have been the dominant cost next to
per-pixel tile decoding in `DrawTileLine` - it's also why Fast Forward's
ceiling didn't move enough to be worth keeping. Not independently verified
against `test/host_test.cpp`'s framebuffer hash (no host C++ compiler or
sample ROM available in this environment) - worth an eye on sprite-heavy
games during hardware testing, though the change is provably order-
preserving by construction.

**Frame skipping** (`source/core/switch_frameskip.{h,cpp}`) - MAME-style in
spirit (this V.Smile core was itself verified against MAME's SPG2xx driver,
see the main README's acknowledgements), though it's a from-scratch
reimplementation of the *idea* - measure speed, skip a proportional and
evenly-spaced share of frames, adjust gradually - not a port of MAME's own
frameskip pattern tables/thresholds, which weren't available to reference
directly. `VSmile::RunFrame()` (game logic + audio) always runs at full
rate, every frame, whether or not that frame ends up on screen - only the
GL upload+draw+`eglSwapBuffers()` cost is ever skipped, so gameplay speed
and audio timing stay correct regardless of what the video framerate is
doing. Settings > Graphics > **Frame Skip**:

- **Off** (default) - every frame renders; identical to every prior pass's
  behavior.
- **Manual** - always skip **Skip Amount** (0-10, a second row that only
  appears in Manual) out of every (N+1) frames, evenly spaced by
  construction (renders when a running counter mod (N+1) is 0).
- **Auto** - N is instead adjusted automatically, based on a rolling
  average of how long `VSmile::RunFrame()` itself has actually been taking
  versus a 60fps target, re-evaluated about twice a second and moved at
  most one level at a time (deliberate damping against hunting/oscillation).
  The Frame Skip row shows the live current level, e.g. "Auto (currently
  3)". Resets to "no skip assumed yet" on a fresh game load or whenever
  gameplay resumes from a pause, so the pause itself never gets mistaken
  for the emulator suddenly running slow.

## New in the previous pass: save states

`VSmile::SaveState()`/`LoadState()` and the byte-stable `StateWriter`/
`StateReader` serialization they're built on (`app/src/main/cpp/core/state.h`)
needed zero changes - same core code Android's `jni_bridge.cpp` already
calls. This pass is entirely Switch-side file I/O and UI around them,
mirroring Android's own `EmuActivity.kt` design in both pieces it has:

- **Direct hotkeys** - Settings > Controller's Save State / Load State
  bindings (unbound by default, same as Android's own `InputMapper.kt`
  defaults) now actually do something: quick-save/quick-load to whichever
  slot was used last for the running cart, no menu involved - same as
  Android's `onHotkey()` calling `saveState(lastSlot())`/
  `loadState(lastSlot())` directly. Fires alongside that frame's gameplay,
  doesn't pause anything.
- **Pause menu picker** - the pause menu's new **Save State** / **Load
  State** rows open a 3-slot picker (Slot 1/2/3, each showing "Empty" or
  the save's timestamp). Picking any slot - including an empty one in Load
  mode, which just no-ops - closes the picker straight back to gameplay,
  matching Android's `pickSlot()` exactly (its `AlertDialog` dismisses on
  any item tap regardless of what the handler does, then gameplay resumes
  since nothing else is keeping it paused).

Both paths funnel through the same two functions (`SaveStateToSlot()`/
`LoadStateFromSlot()` in `main.cpp`), same as Android's own `saveState()`/
`loadState()` back both its hotkey and dialog paths. File layout mirrors
Android's naming one-for-one - `sdmc:/switch/dsmile/states/<cart filename
without extension>.slotN.dss` in place of Android's app-private
`<romName>.slotN.dss`. `LoadState()` is cart-checksum guarded in the core
itself (see `vsmile.h`), so loading a state saved by a different cart or
version just fails harmlessly, same as `nativeLoadState()` returning false
on Android.

**Not ported**: state thumbnails. Android captures and saves a 160x120 PNG
of the frame alongside each slot's data purely for the picker's visual
list; this port's slot picker is text-only (label + timestamp, no
image) - `stb_image` (already vendored for cover art) only decodes PNGs, it
doesn't encode them, so writing a thumbnail would need a second image
library pulled in just for this. Low priority given the picker is already
fully functional without it.

## New in the previous pass: in-game pause menu

Android's Start button opens a full quick-access menu mid-game - Resume,
save/load state, fast-forward, touch-control layout/theme/opacity (N/A
here, no touchscreen overlay), Shader/CRT/Aspect/Render mode/Background/
Bezel, button mapping, trigger sensitivity, Reset game, Quit. This port's
**Menu** action (Settings > Controller, default **+**) opens a deliberately
smaller version of the same idea:

- **Resume** - close the menu, continue playing.
- **Save State** / **Load State** - opens the 3-slot picker; see "New this
  pass: save states" above (added the pass after this one, once the menu
  itself existed to put it in).
- **Reset Game** - `VSmile::Reset()` on the running game, then resume.
- **Quit** - unload the running game and return to the grid (same as
  Android's own Quit, which closes back to the game list - not "quit to
  hbmenu"; L+R+Plus still does that as a separate hard-quit shortcut).
- **Settings** - opens the same Settings root every other entry point uses
  (Launcher/Library & Storage/Graphics/Controller) - not a duplicate copy.
  Backing out of Settings returns to this pause screen instead of the grid,
  the way it should when opened mid-game.

Unlike Android's version, gameplay doesn't keep rendering (frozen or
otherwise) behind the menu - pressing Menu fully replaces the screen with
the same chrome-styled menu look everything else uses, and nothing runs
(`VSmile::RunFrame()`/audio push are simply skipped) while it's open. A
translucent overlay on top of a frozen frame would look closer to Android's
actual behavior, but this is the simpler version to get working first.

*(At the time of this pass, fast-forward still wasn't wired to anything -
it was built two passes later, tested on hardware, and then removed; see
"Fast Forward: tried and removed" above.)*

`main.cpp` only owns the Playing <-> Paused state transition itself (and
the one frame where Menu's or a state hotkey's bound button is detected);
everything about what the pause screen contains and how it navigates lives
in `switch_menu.cpp`, the same ownership split every other screen already
has.

## Fixed after hardware testing: settings screens that don't scroll

Every row-based screen used to hand-roll its own "panel sized to fit every
row + sliding highlight + row loop" - and only two of them (the game list
and the directory browser) ever actually implemented "keep the selected row
scrolled into view" as part of that. Every settings-style screen that grew
past what fits in the visible area on one screen - most visibly Settings >
Controller's 14 rows - just kept drawing every row into a panel that grew
past the bottom of the screen, with no way to scroll down to the ones that
didn't fit.

Fixed by extracting the whole pattern into one shared, reusable widget -
`switch_chrome_draw_row_list()` in `switch_chrome.cpp` - and moving every
row-based screen (Settings root, Launcher, Graphics, Controller, Library &
Storage, Game Folders, the game list, and the directory browser) onto it.
It owns sizing the panel to whatever actually fits the available height,
keeping the selection scrolled into view (only moving the window when the
selection would otherwise land off-screen, not every frame), and the
sliding highlight - so this class of bug can't recur by a screen forgetting
to reimplement it: there's now exactly one implementation to get right
instead of eight. Each screen still owns its own persistent `scroll` int and
`highlight_y` float (same ownership convention `switch_chrome_animate_to`
already established), and just calls the shared function with its row count,
selection, and a small callback that fills in each visible row's label/value
text.

## New in the previous pass: Controller settings

*(At the time of this pass, Settings > Controller listed all 13 gamepad
actions Android's `Action` enum has, and none of the 5 hotkeys below did
anything yet. Since then Save/Load State, Menu, and Rewind were all wired
up; Fast Forward was too, but was later removed after hardware testing -
see "Fast Forward: tried and removed" above - leaving 12 actions and 4
hotkeys today.)*

Settings > Controller lists all 13 gamepad actions Android's `Action` enum
(`InputMapper.kt`) has, in the same order, each showing its currently bound
physical button (or "Unbound"):

- The 8 real V.Smile controller buttons - Enter/Back/Help/ABC/Red/Yellow/
  Blue/Green - which are the only ones that actually do anything right now.
  Their defaults match the hardcoded mapping every prior pass has used
  (A/B/Y/X/R/ZL/ZR/L), so nothing changes for anyone who doesn't open this
  screen.
- The 5 hotkeys - Save State, Load State, Fast Forward (hold), Rewind
  (hold), Menu - which Android has but this port doesn't implement *at all*
  yet (see "What's still missing"). They're fully bindable and persisted
  here anyway, on purpose - the settings will already be in place once each
  feature actually gets built, rather than needing its own remap UI bolted
  on later. Right now binding one of these does nothing in-game.
- **Trigger Sensitivity**, mirroring Android's analog-trigger pull-depth
  setting. Also currently inert - Switch's L2/R2 equivalents (ZL/ZR) are
  digital buttons, not analog pulls, so there's nothing for a threshold to
  apply to yet. Stored for parity in case that ever changes (e.g. a
  Bluetooth pad with real analog triggers).

**Binding**: select a row, **A** opens a "press a button" prompt (a
blocking capture loop, same shape as the directory browser's own), **X**
unbinds it directly from the list with no prompt. **B always cancels the
capture prompt** (matches this menu's convention everywhere else), which
means B itself can never be assigned to anything through this screen - a
known, deliberate limitation (see `switch_settings.cpp`'s `kButtonMeta`
comment). D-Pad isn't bindable either - it's hardcoded to the V.Smile
joystick, same as Android's InputMapper never offers it in its own wizard.
Assigning a button that's already bound elsewhere steals it away (strict
1:1, ported directly from `InputMapper.bind()`'s exact behavior) - so it's
possible to accidentally unbind Enter by reusing A for something else;
that's Android's real behavior too, not a bug introduced here.

## New in the previous pass: Graphics settings

Settings > Graphics, covering the same "Look" options the main
[README.md](../README.md) describes for the Android app:

- **Renderer** - Fast / Accurate. Not a rendering-layer setting at all -
  this is `VSmile::SetAccurate()`, applied by `main.cpp`'s `LoadGame()` when
  a game loads (same as Android's `nativeSetAccurate()`). Takes effect on
  the *next* launch, not live mid-game, same as every other setting here.
- **Shader** - Pixel / Sharp (default) / CRT.
- **Aspect Ratio** - 4:3 (default) / Stretch / Integer.
- **Background** - Black (default) / Wavy Blue / V.Smile Purple - the
  letterbox behind the picture.
- **TV Bezel** - None (default) / Silver / Black - wraps the picture in a
  rounded frame with tube glass behind it.
- **5 CRT intensity sliders** (Curvature, Glow, Scanlines, Aperture Grille,
  Vignette) - only appear while Shader is set to CRT, same
  appear-only-when-relevant pattern `VisibleLauncherRows()` already
  established for the grid-only Launcher rows. 0-100% in 5% steps.

`switch_render.cpp` was rewritten around this: every shader
(Pixel/Sharp/CRT/the letterbox background/the bezel) is copied verbatim
from `GameRenderer.kt` - same source of truth the original "sharp" shader
already came from - so gameplay should look pixel-identical to the Android
app once a matching option is picked, not just similar. Aspect math (4:3
fit / stretch / integer scale) and the bezel's "picture shrinks to fit
inside the frame" behavior are the same 1:1 ports. Everything reads
`g_settings` directly each frame, the same way `GameRenderer.kt` reads its
own mutable fields every `onDrawFrame` - no extra plumbing needed beyond
what `switch_settings.cpp` already provides for every other setting.

## Fixed after hardware testing

- **Overlapping/unbounded tile text.** The grid's placeholder-tile name and
  below-cover title caption had no width limit at all, so anything longer
  than the tile just ran straight into its neighbors (very visible with a
  full library and no cover art yet - see the screenshot from testing).
  Fixed with the same two-part pattern DraStic itself uses everywhere text
  might overflow: `switch_ui_ellipsize()` trims unselected text to fit with
  a trailing "..." (`ellipsizedText()`), and `switch_ui_draw_text_marquee()`
  slides selected text left then back on a 6-second loop inside a clipped
  box instead of cutting it off (`drawScrollTextL`/`R`) - so the one row/
  tile you're actually looking at is still fully readable even when it
  doesn't fit. Applied generally: every list-style row
  (`switch_chrome_draw_row` - Settings, Library & Storage, Game Folders, the
  directory browser, and list-view games) gets this for free now, not just
  the grid.
- **Theme backgrounds not actually changing.** Every theme's panels/text/
  accent correctly changed, but the plain background behind everything
  stayed the "animated" theme's navy regardless - because that clear color
  was hardcoded in `main.cpp`/`switch_dirbrowse.cpp` instead of coming from
  the theme. `COL_BG` was the one DraStic palette value the first port of
  this pass never actually ported (its own comment at the time admitted as
  much). Fixed by adding `g_col_bg` to `switch_chrome`'s per-theme palette
  and a `switch_chrome_clear_background()` helper both call instead of
  clearing to a literal color themselves - OLED's background is real black
  now, and the other 4 themes' backgrounds should visibly differ too.

## New this pass: list/grid toggle

Settings > Launcher > **View** switches between **List** (default) and
**Grid (cover art)**. The three grid-only rows (Games per row, Rows per
page, Show game titles) only appear in the Launcher screen while Grid is
selected - `VisibleLauncherRows()` in `switch_menu.cpp` builds the row list
dynamically instead of always showing all of them. List view reuses the
same sort modes, theme, and (now-fixed) text truncation/marquee as the
grid.

## Scope so far

1. Boot a game with graphics/logic/sound working (no menu, hardcoded ROM
   path) - the original bring-up milestone.
2. ROM browser + optional BIOS loading, reading `.bin` files from the SD
   card instead of a hardcoded path.
3. Menu redesign (header/footer/glass-panel chrome) + a Settings screen
   scoped to Launcher and Library & Storage, plus a glyph-upside-down text
   fix.
4. **Game grid with cover art, themes, multiple game folders, and sorting**
   (this pass) - see below for what's actually in each.

Everything else the Android app has builds on top of this once each piece
lands - save states and fast-forward/rewind are next, then further visual
options (CRT shader, aspect modes, bezels) - see "What's still missing".

## What's new this pass

**Game grid with local cover art.** The game list is now a paged grid
(`Wrapper`-equivalent `grid_columns` x `grid_rows` from Settings > Launcher).
Cover art is **local only** - there's no V.Smile game database to
auto-fetch from the way DraStic uses SteamGridDB, so a game only gets a
cover if you supply one yourself: drop a `.png` next to its `.bin` with the
**same base filename** (`Aladdin.bin` -> `Aladdin.png`, same folder). PNG
only (matches `third_party/stb_image.cpp`'s `STBI_ONLY_PNG`, the same
choice the Yokoi reference port made for its own art - keeps the decoder
small). No downscaling or size cap - a very large source image just costs
more VRAM and decode time, so keep them reasonably sized. A game with no
matching `.png` gets a plain tinted placeholder tile with its name on it
instead of a blank box.

**Themes.** All 5 of DraStic's built-in palettes, exact colors ported from
`applyLauncherAppearance()` - **Glow**, **XMB (PS3)**, **Classic**,
**OLED black**, **Bubbles** - plus a 6th, **V.Smile**, added for the 1.0.0
release and made the default: the console/controller's own purple and
orange, built from this project's own colors rather than ported from
DraStic (deep purple background, off-white/lavender text, the exact
orange `app/src/main/res/values/colors.xml`'s `ic_launcher_bg` already
uses - `#F7941D` - for values and the selection highlight). Switch in
Settings > Launcher > Theme. `switch_chrome.cpp` owns the current palette
as a set of global colors every screen (grid, settings, folder browser)
reads from, so a theme change applies everywhere at once.

**Multiple game folders.** Settings > Library & Storage > Game folders opens
a list of configured folders (`[ Add game folder ]` plus each one already
added) - confirmed with **A**. Adding opens a directory browser
(`switch_dirbrowse.cpp`) starting at `sdmc:/switch/dsmile`: **A** enters a
folder, **Y** picks whatever folder is currently open, **B** cancels. All
configured folders are scanned together into one combined list. At least
one folder must remain - the last one can't be removed. No rename/reorder,
and **no confirmation dialog on removal yet** (see "Known gaps").

**Sorting.** **X** on the grid cycles A-Z / Recently played / Recently
added (recently-added uses the `.bin`'s file mtime; recently-played is
tracked in `settings.ini` and updated whenever a game is actually launched).
Current mode shows in the header alongside the page indicator.

## What's here (files)

- `Makefile` - devkitA64/libnx build. Now also builds `source/third_party`
  (`stb_image.h`/`.cpp`, copied from the Yokoi reference port's own copy -
  PNG decoding for cover art, `STBI_ONLY_PNG` to keep it small).
- `source/main.cpp` - two-state loop (menu, gameplay), otherwise unchanged
  from prior passes. Now also calls `switch_menu_init()`/`_shutdown()`
  instead of the old settings-load + rescan calls, and marks a game played
  (`switch_settings_mark_played()`) right after a successful launch. The
  automatic rescan after returning from a game (L+R+Plus) was **removed** -
  it would re-decode every cover PNG again, which isn't free anymore;
  Library & Storage > Rescan library covers the "SD card changed" case
  instead. Exposes `switch_present()` (wraps its own `eglSwapBuffers`) so
  the directory browser's own frame loop can drive the same display/surface
  without main.cpp handing out its EGL objects directly.
- `source/core/switch_menu.{h,cpp}` - the state machine: game grid, Settings
  root, Launcher, Library & Storage, Game Folders. Owns navigation
  (D-Pad/stick, L/R page, X sort, A/B/Y/+) and delegates rendering to
  `switch_chrome`/`switch_ui`, data to `switch_library`/`switch_settings`.
- `source/core/switch_library.{h,cpp}` - the game list's data model. Scans
  every configured folder, resolves + decodes + uploads cover art, sorts
  per the current mode. `switch_menu.cpp` never touches the filesystem or
  GL texture lifecycle directly anymore - this owns it. Now also lists
  `.zip` archives (validated via `switch_zip_is_valid()`) alongside `.bin`
  files - see "New this pass: zip game support" above.
- `source/core/switch_zip.{h,cpp}` - **new**: minimal ZIP reader, just
  enough to pull one `.bin` out of a `.zip` (stored or deflated, via
  zlib's raw `inflate()`) - see "New this pass: zip game support" above
  for what it does and doesn't handle.
- `source/core/switch_dirbrowse.{h,cpp}` - **new**: the folder-picker modal.
  Runs its own blocking input/render/present loop (nested inside
  `switch_menu_update()`'s call for "Add game folder" - the same
  nested-blocking-loop shape DraStic's own `browseFolder()` uses from
  inside its settings screens).
- `source/core/switch_settings.{h,cpp}` - generalized from a single bool
  into a small generic key=value store (closer to DraStic's own `Store`/`KV`
  now, just without its hashed index - our scale doesn't need one) backing:
  `ui_animations`, `theme`, `grid_columns`, `grid_rows`, `show_game_titles`,
  `sort_mode`, the game-folder list, and per-game last-played timestamps.
  Same file, `sdmc:/switch/dsmile/settings.ini`.
- `source/core/switch_present.h` - **new**: the one-function seam described
  above.
- `source/render/switch_chrome.{h,cpp}` - **new**: factored out of
  `switch_menu.cpp` once the directory browser needed the exact same header/
  footer/row/theme chrome. Every list-style screen now gets the sliding
  highlight animation consistently (previously only the game list itself
  actually used it - the settings screens computed it but never drew with
  it, a leftover from the prior pass that this cleaned up along the way).
- `source/render/switch_ui.{h,cpp}` - added `switch_ui_draw_texture()` for
  blitting an arbitrary GL texture (cover art) into a pixel rect, with the
  same V-flip `switch_ui_draw_text()` needed - `stb_image` decodes
  top-row-first too, so it has the identical upside-down risk the glyph fix
  addressed last pass. Applied proactively this time rather than found on
  hardware.
- `source/third_party/stb_image.{h,cpp}` - **new**: vendored from the Yokoi
  reference port unmodified. Public-domain/MIT-0, PNG-only build.
- `source/render/switch_header_icon.h` - **new**: the rounded-corner mascot
  icon shown in the game grid/list header, embedded as a PNG byte array -
  see "New this pass: header icon" above.
- `source/render/switch_render.cpp` - the in-game renderer. Gained 8 more
  Shader options this pass, ported from `reference/DrasticDS_nx-main`'s
  shader bundle - see "New in the previous pass: shader ports" above.
- `source/audio/`, `source/input/` - the in-game bring-up pieces, unchanged.

## Controls

**Game grid**: D-Pad/stick to move, **L/R** to change page, **X** to cycle
sort, **A** to launch, **Y** to open Settings, **+** to quit to hbmenu.

**Settings** (all screens): **B** goes back one level. Root: Up/Down + A to
open a category. Launcher: Up/Down to pick a row, Left/Right or A to change
its value. Library & Storage: Up/Down + A opens Game Folders / BIOS, or
triggers Rescan; Left/Right on Region adjusts it. BIOS and Region only
appear here when reached from the main grid, not from the in-game pause
menu's Settings (see "New in the previous pass: BIOS and Region locked to
the main menu" above). Game Folders: Up/Down + A adds (opens the directory
browser) or removes a folder. Controller: Player row switches between
player 1's and player 2's bindings; player 1 also has Controller Type
(Joystick / Smart Keyboard (USB)) and, for the keyboard, Keyboard
Language. No Players setting; a second controller being connected is
what "2 players" means, checked live every frame during gameplay (see
"Two-player controller support: the `UartRxDone()` regression, and
dropping the pairing applet" above). Pair one via the system's own
HOME-menu overlay whenever you want it in.

**Directory browser**: Up/Down to move, **A** to open the selected folder,
**Y** to pick the folder currently open, **B** to cancel.

**In-game**: default bindings - A=Enter, B=Back, Y=Help, X=ABC, L=Green,
R=Red, ZL=Yellow, ZR=Blue, D-Pad/left stick=joystick - all remappable in
Settings > Controller, and shared by player 2's controller whenever one's
connected. **Menu** (default **+**) pauses and opens
Resume/Save State/Load State/Reset Game/Quit/Settings. **Save State**
(unbound by default) / **Load State** (default **StickR**) quick-save/load
to the last-used slot without opening the menu at all. **Rewind** (default
**StickL**) steps gameplay backward while held, up to ~15 seconds. There's
no Fast Forward - built, hardware-tested, and removed; see "Fast Forward:
tried and removed" above. Holding **L+R+Plus** is still a hard shortcut
straight back to the game grid, bypassing the pause menu entirely (useful
if Menu itself is unbound).

**Smart Keyboard** (Controller Type set to the keyboard): type on a USB
keyboard; Enter/Esc/F1 are OK/Quit/Help, F2/F3/F4 are Player 1/Player
2/Symbol, F5-F8 are Red/Yellow/Blue/Green, F9 is ABC, Tab is Typing Time,
Delete is Erase. Set Keyboard Layout to match your USB keyboard. The pad stays the
keyboard's joystick and OK/Quit/Help. See "New this pass: Smart Keyboard
(USB keyboard)" above.

**Pause menu**: Up/Down to move, **A** to select, **B** resumes directly
(same as selecting Resume).

**Save/Load State slot picker**: Up/Down to move, **A** to select a slot
(saves/loads and returns straight to gameplay either way), **B** cancels
back to gameplay directly (not back to the pause menu - matches Android's
own picker, which has no such step either).

## BIOS support

Optional, same as Android - games boot without one, it just improves
compatibility on a few titles. Drop any number of `.bin` files into
`sdmc:/switch/dsmile/bios/`; Settings > Library & Storage > **BIOS** picks
which one is active (defaults to whichever sorts first alphabetically if
you never open the picker - see "New in the previous pass: BIOS picker"
above). No longer a fixed `sysrom.bin` filename requirement. The displayed
*language* is a separate setting, **Region** (same screen, Left/Right to
adjust, 0-15, labeled with the actual language name) - see "BIOS picker:
language not changing, and the new Region setting" and "Region mapping"
above for why the two are independent and where the label text comes from.
Both are only changeable from the main grid's Settings, not mid-game - see
"New in the previous pass: BIOS and Region locked to the main menu" above.

## Cart-side NVRAM saves

Automatic, no settings involved - only relevant to V.Smile Art Studio and
its regional re-releases (see "New this pass: cart-side NVRAM saves"
above), the one cart family with its own onboard save memory. Recognized
automatically by the cart dump's hash; its save data lives in
`sdmc:/switch/dsmile/nvram/<cart name>.nvram`, separate from
`sdmc:/switch/dsmile/states/`. Every other cart is completely unaffected.

## Building

Requires devkitPro's `switch-dev` group (devkitA64 + libnx) plus
`switch-glad`, `switch-mesa` (EGL/GLESv3), `switch-freetype` and its
dependencies (`switch-harfbuzz`, `switch-libpng`, `switch-zlib`,
`switch-bzip2`) - all already installed on this machine. No new portlib for
cover art - `stb_image` is a vendored single-header decoder, not a package.

From a plain Git Bash shell (not devkitPro's bundled MSYS2), the same
workarounds prior passes needed still apply:

```bash
cd switch
export PATH="/c/devkitPro/devkitA64/bin:/c/devkitPro/tools/bin:$PATH"
make DEVKITPRO=/c/devkitPro DEVKITPATH=/c/devkitPro 'TMP=C:\Users\<you>\AppData\Local\Temp' 'TEMP=C:\Users\<you>\AppData\Local\Temp'
```

Produces `dsmile_switch.nro` (~7.6 MB).

## Testing on hardware

1. Create `sdmc:/switch/dsmile/games/` (or any folder(s) you'll add via
   Settings > Library & Storage > Game folders) on the SD card.
2. Copy V.Smile cartridge dumps there as `.bin` files, or `.zip` archives
   each containing one `.bin` (previous pass - not yet hardware-confirmed,
   see "New in the previous pass: zip game support" above). Optionally,
   drop a same-named `.png` next to any of them for cover art.
3. Optional: any `.bin` BIOS dump(s) in `sdmc:/switch/dsmile/bios/` - pick
   which one's active, and the region/language nibble, in Settings >
   Library & Storage > BIOS / Region.
4. Copy `dsmile_switch.nro` to `sdmc:/switch/dsmile/dsmile_switch.nro`.
5. Launch from hbmenu or an emulator's homebrew loader.

Region's language labels, the BIOS/Region main-menu lock, the audio
self-heal, single-player controller input, automatic two-player
*detection*, combined Joy-Con pairs staying combined, cart-side NVRAM, and
all 11 ported shaders (including 5xBR/SABR at a playable frame rate with
frame skip on stock clocks), `.zip` games, the header icon, player 2
bindings and BIOS version detection are all hardware-confirmed working now.
The Smart Keyboard (USB keyboard detection, typing, Enter/Esc/F1) is
hardware-confirmed too. Not yet tested: its follow-up - F5-F8 colours
(red skips Cyber Adventure's intro), F9 ABC, and a non-US Keyboard Layout
if a matching keyboard is available.
Two-player separation is confirmed working too -
the earlier "crosstalk" was a turn-taking test game that reads both ports
on purpose (see "Two-player controller support" above). Still worth
checking if it comes up:

1. **Unplugging player 2 mid-session** - does play continue normally as
   solely player 1 rather than anything getting stuck or crashing. Not
   specifically tested yet.
2. Two-player with Pro Controllers or other USB controllers - only two
   combined Joy-Con pairs have been tested so far.

## What's still missing

No major Android-parity feature remains outstanding that's both feasible
and worth keeping - Rewind is in, and Fast Forward was tried, tested, and
found not worth shipping in this port's architecture (see "Fast Forward:
tried and removed" above). Every remaining `GameAction` in Settings >
Controller is wired to something real. What's left below is polish and
robustness rather than a missing feature, roughly in the order it's worth
tackling next:

1. **Touch-screen drawing pad** - Art Studio is fully playable with the
   standard controller already (cart loading, drawing, and NVRAM saving
   all work), but emulating the drawing tablet or the writing-pad
   controller via the Switch touch screen would be much nicer. Blocked on
   the fact that nobody has ever captured either device's protocol (see
   "Noted, not implemented: V.Smile Art Studio's digitizer/pen" above and
   `reference/findings.md` §2).
2. **A reusable confirm-prompt screen** - once built, covers every
   "no confirmation dialog" gap below in one pass instead of one-off fixes.
3. ~~ABC/Smart Keyboard accessory support~~ - done this pass with a USB
   keyboard (see "New this pass: Smart Keyboard (USB keyboard)" above).
   Hardware-confirmed.

Done: two-player controller support (confirmed working, including clean
separation in simultaneous two-player games) and cart-side NVRAM saves,
both found by checking
MAME's V.Smile Motion driver for wand/motion-controller support (which
turned out not to exist there either - see "V.Smile Motion investigation"
above) and porting the two gaps that investigation surfaced instead (see
"New this pass: two-player controller support" and "New this pass:
cart-side NVRAM saves" above); Rewind, both the hotkey and
its ~15s history (see "Rewind" above; Fast Forward was removed, see "Fast
Forward: tried and removed" above); a BIOS picker instead of a fixed
filename, plus a Region setting - labeled with the real language name,
sourced from MAME's driver, both locked to the main menu - for the
language the BIOS displays (see "New in the previous pass: BIOS and
Region locked to the main menu", "New in the previous pass: BIOS picker",
"BIOS picker: language not changing", and "Region mapping" above); a
first performance pass - build flags (-O3, LTO), frame skipping, and the
sprite-scan dedup (see "New in the previous pass: performance" above);
save states, both the direct hotkeys and the pause menu's slot picker (see
"New in the previous pass: save states" above - no thumbnails, see that
section's own note); gamepad remapping (Settings > Controller); visual
options (CRT shader, pixel/sharp/CRT display modes, aspect ratio,
letterbox backgrounds, TV bezels, the fast/accurate renderer toggle) - see
"New in the previous pass: Graphics settings" above.

Also not done yet, lower priority:

- **No confirmation dialog** removing a game folder, or anywhere else - any
  destructive action (folder removal, eventually save deletion) just
  happens immediately. Worth a small reusable confirm-prompt screen once
  more than one place needs it.
- **No cover art beyond the local `.png`-next-to-`.bin` convention** - no
  fetching, no alternate art sources, no per-game art override UI (remove/
  replace a cover without touching the file manually).
- No error UI for a corrupt/invalid cart dump - load failure just silently
  stays on the grid.
- No subfolder recursion within a configured game folder or the BIOS
  folder, no per-game metadata beyond the filename and file mtime.
- No `nxlink`/log-file diagnostics - `printf` (visible only via
  `RunConsoleErrorLoop`'s console screen on a hard failure) is the only
  output.
- Beyond this pass's build flags and frame skip, no actual *profiling* has
  been done - the sprite-scan redundancy noted above is a candidate, found
  by reading rather than measuring, and there may well be others. Cover art
  decode/upload specifically happens synchronously on every rescan
  (startup, or an explicit Rescan library / folder add-remove) - fine for a
  personal-sized library, untested at real scale.
- Frame skip's auto mode reacts to `VSmile::RunFrame()` cost specifically -
  it has no way to notice if the GL side (a heavier shader like CRT, or the
  Accurate renderer's post-processing) is what's actually slow, since
  neither is timed. Manual mode doesn't have this blind spot (the user
  picks the number directly).

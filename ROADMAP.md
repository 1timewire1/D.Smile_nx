# D.Smile NX roadmap

Open work, grouped by kind, roughly in the order we plan to tackle it. Tick items off (`[x]`) as
they land and add new ones where they fit. Each item says enough to start from; deeper notes live
in `switch/README.md`, the write-ups in `docs/`, and the local research notes
(`reference/findings.md`, not tracked).

Current release: **1.2.0** (the performance pass). Next: the core accuracy checklist (section 3)
and the other open items; performance stays where it is for now.

## 1. Input reliability (top priority)

- [x] **Controller freeze (fixed in 1.2.0, confirmed on hardware).** On the BIOS test screen
      (v1.03, region F, joystick), mashing all seven buttons and every stick level froze the screen
      and LEDs within 30-60 s.
      - **Cause:** the console ignores an RTS edge that arrives under ~8 ms after UART activity,
        or during a busy transfer (BIOS `EXT1_update`, `0xA9CD`). Our controller held RTS and
        waited until its 0.5 s timeout threw the queue away.
      - **Fix:** re-request the line with a fresh edge after 12 ms without CTS
        (`VSmileJoy::RearmRts()`), plus a re-arm when CTS drops with bytes still queued.
      - **Joystick only.** The re-arm broke Toy Story 3's Motion controls (menus ignored OK, "Please
        reconnect your controller"), so Motion controllers keep exactly 1.1.0's link handling.
      - **Related:** MAME's *Shrek: Dragon's Tale* mashing lock-up doesn't reproduce on our core.
- [ ] **Two-player Motion at full rate, safely.** The re-arm also let two Motion controllers
      report every 40 ms instead of 80 ms (about 15 reports/s each, up from 8), but it's off for
      Motion now. Find a variant that doesn't release RTS mid-report (e.g. only between packets,
      only with two controllers) and check it against Toy Story 3 one-player as well.
- [ ] **Cars 2 (USA, Pocket version): brief Motion disconnect notice** on its main screen, seen on
      hardware with 1.2.0. A good test case for the Motion link; compare with 1.1.0 first.
- [ ] **Input log** that can be replayed on the desktop harness (see Diagnostics, section 4), for
      cases like these.

## 2. Performance (1.2.0 pass done)

- [x] **Full speed on stock clocks (1.2.0).** Measured with the new per-frame log, then: inline
      device-tick fast paths, next-event scheduling for the devices, bus read maps, cheaper fetch,
      and wait-loop replay - all bit-exact (`test/core_digest.cpp`, every ROM in
      `reference/roms`). Stock clocks: Toy Story 3, Alphabet Park, Cars 2 at 60 fps (from
      40-43), Action Mania ~59. Details in `switch/README.md`, "Performance pass (1.2.0)".
- [x] **Frame skip blind spot (1.2.0).** Auto now also watches emulated frames per real second,
      so shaders and the Accurate renderer count; skipped frames are paced.
- [x] **Audio dying, and clicks (1.2.0).** A stale wavebuf state made the queue look stuck, and
      the self-heal re-created the driver after 3 s of silence. Fixed, plus rate control.
- [ ] **PGO (deferred).** Likely another 10-20% for the interpreter; best done right before a
      speed-focused release, since the profile goes stale as the core changes. Needs an
      instrumented build played on hardware to collect the profile.
- [ ] **Action Mania's last ~1-2 fps** (58.4-59.8 fps; its waits are short, so replay barely helps).
      PGO is the likeliest fix.

## 3. Core accuracy checklist

From V.simer's hardware test ROMs and later MAME/veesem fixes. None of these has been checked
against our core (`app/src/main/cpp/core`) yet, and each affects every game.

- [ ] **SpongeBob: A Day in the Life of a Sponge, Boating Lesson.** It stops at the first spot in
      MAME and veesem: an unimplemented SPU "repeat mode", for which V.simer has an unpushed fix.
      On our core the boat leaves the dock and the course appears, then it doesn't move from the
      first spot. Confirm against the game manual's controls before treating that as the bug.
- [x] **German and French *Shrek Forever After* (Motion) didn't boot.** *Fur immer Shrek
      (Germany) (Rev 1)* and *Shrek 4 - Il etait une fin (France) (Rev 1)* showed a garbled or black
      screen. **Fixed:**
      - Their two 8 MB ROM blobs are wired the other way round from the US cart: MAME loads blob1
        at `0x800000`. The dumps store blob1 first, so the boot code ran from the wrong half and
        crashed in frame 0.
      - `VSmile::LoadCart` now swaps the halves when the first half's CRC matches either cart's
        blob1.
      - Both reach their title and menus; confirmed on hardware.
- [x] **Multi-chip cart check (1.2.0).** 15 of the multi-chip dumps boot-tested on the harness
      (2026-10-22): Cars 2 USA/Pocket, Super WHY!, Toy Story 3 USA, Alphabet Park Adventure
      (USA Alt 2/Alt 3, UK), Care Bears, Lion King, Mickey UK, Pooh USA/UK, Spider-Man UK all
      reach their intros and menus.
      - **Cars 2 Germany and France were black: swapped halves**, the same fault as the Shrek
        pair, but MAME loads them in dump order (so it likely fails on them too). Added to the
        `LoadCart` swap list; both reach their menus now (confirmed on hardware).
      - **Toy Story 3 Germany and Spain: swapped too** (both match MAME's CRCs; MAME loads them
        in order). The common French dump boots as it is; MAME's French copy is the same data
        swapped, so it's in the list as well. All four boot to the Learning Zone.
      - Toy Story 3 (Italy) [b] is only 8 MB, one chip of two; it pairs with the USA upper chip
        and boots in English, so its partner chip is simply missing. Can't be fixed.
- [ ] **Shrek: Dragon's Tale, math answer flicker.** In adventure mode, when something brings
      up a math problem, the selected answer jumps rapidly between two slightly different
      positions. Possibly intended; compare with 1.1.0 and MAME before treating it as a bug.
- [ ] **External memory decode mode 1** ("2 ROMs"). Our core (following veesem) reads its upper
      2M words from cart offset `0x100000` (CSB1). MAME and the wiki use CSB2 (cart offset
      `0x200000`). None of the 74 carts tested uses mode 1 (all use modes 0 and 2), so it only
      matters if one turns up.
- [ ] **SPU: ADPCM is IMA ADPCM**, not the OKI variant (MAME PR #12525).
- [ ] **SPU: `CHANNEL_STOP`** is set automatically at auto-end or when the envelope reaches zero.
      `CHANNEL_ENABLE` is left alone, and a channel runs if it's enabled and not stopped (MAME PR
      #12572).
- [ ] **SPU: wave address and phase accumulator** visibly update while a channel plays (MAME PR
      #12580).
- [ ] **SPU: rampdown.** veesem fixed missing Shrek sounds by not setting rampdown-enable on
      non-playing channels.
- [ ] **SPU: low-pass filter** on the output, as real hardware has.
- [ ] **Timer A** can AND its two clock sources together (veesem#26).
- [ ] **UK games with broken music without a real BIOS.** V.simer found the cause.
- [ ] **PPU sprite-blend priority quirk.** A sprite with its blend flag set has an odd hardware
      priority (VSmileDev #software 2021-10-09).

Already confirmed in our core (no action): MULS FIR_MOV, blending over transparent pixels,
blended sprites in depth order, the wave-in mix, the watchdog, and Port C/ADC details.

## 4. Front end features and polish (Switch)

- [ ] **Hidden debug codes.**
      - Investigate both colour-button codes (test-mode checksum and copyright, shared across
        VTech's library, documented on TCRF): check no inputs are dropped while entering them,
        and compare checksums against zipped copies.
      - Look for other undiscovered codes or test-mode features (Little Einsteins matches button
        sequences against a table at `0xA4BA`).
      - Add pause-menu options that trigger them, instead of only entering them by hand.
- [ ] **Standby (power-off state) management.** There's currently no way to manage this data.
      Add a pause-menu "full reboot" that wipes the game's saved standby (a cold boot), and/or a
      settings screen to list and delete saved standby states.
- [ ] **Reusable confirmation prompt.** Removing a game folder happens immediately; one prompt
      screen would cover this and the standby deletion above.
- [ ] **Load errors.** A corrupt or unloadable cart currently just stays on the grid with no
      message.
- [ ] **Subfolders.** Game folders and the BIOS folder aren't searched recursively.
- [ ] **Diagnostics / logging.** *Partly done in 1.2.0:* Settings > Graphics > Performance >
      "Shown + log" writes per-frame timings, clocks and audio counters to
      `sdmc:/switch/dsmile/perf/`. Still to do: a general log file (or nxlink output), and an
      input log that can be replayed on the desktop harness (section 1).
- [ ] **Smart Keyboard switch mid-game.** The cart only detects the keyboard at boot (as on
      hardware), so switching Controller Type in-game does nothing until a reset. Offer to
      restart.
- [ ] **Cover art.** Only a local `.png` next to the ROM is supported: no override or remove UI,
      no other sources.

Parked as low value (revisit if wanted):
- [x] **FPS counter overlay** (veesem's `-fps`): the Performance overlay (1.2.0).
- [ ] **IOC6 audio-output enable** (port C bit 6 mutes the whole output). Only matters if a game
      mutes deliberately; none is known to.

## 5. Consoles and BIOS research

- [x] **Spanish Motion BIOS deep dive (1.2.0)** (`vsmilemotion.bin`). See
      `docs/console-type-and-bios.md`.
      - It's a later revision than v1.03 (reset at `0x704C`; about half its 256-word blocks are
        shared with v1.03) with its own test program, **v7.05**, entered by holding OFF at
        power-on and pressing OFF about 5 more times. The BIOS test screen option plays that
        sequence, and the BIOS row shows "Motion (Spanish unit, v7.05)".
      - All 16 region codes identified from the test screen's "Language:" line (08 is
        Portuguese, not MAME's Mexican Spanish; 09 is Dutch). The Region row uses them.
      - Found along the way: v1.03's second test-screen entry, IOC5 as test point TP5.
- [ ] **V.Motion boot animation.** The V.Motion rebrand (region-specific?) has its own boot
      animation, different from both the standard and Spanish Motion intros. It doesn't appear
      in the Spanish dump under any region code or console-type strap. Is that BIOS undumped,
      or is the animation behind something not yet tried (another strap, a test-point input)?
- [ ] **v7.05 vs v1.03, the rest of the differences.** Beyond the test-program entry and the
      region handling, what else the Spanish Motion BIOS changes. Open-ended; worth doing if a
      specific question comes up (KanedaFr's v1.03 disassembly and the Motion schematic are the
      references).
- [ ] **Voice clip per region code** on the Spanish Motion BIOS. 5/6/A/E draw identical frames,
      so a different voice line is the only thing that could still tell them apart. Unchecked:
      the desktop harness has no audio output.
- [ ] **Motion controller arrow LEDs.** The controller has LEDs for the four arrows, like the
      colour-button LEDs. Find how games drive them (undocumented). Extend the LED overlay when
      known.
- [ ] **Motion report modes 3/4.** These are the 6-bit packed reports, X and Z inverted; they're
      implemented from Action Mania's code but never seen in use. Find a game that uses them, or
      confirm none do. They may or may not relate to the arrow LEDs.
- [ ] **Cyber Pocket controller type** (the non-ambidextrous model). It has a D-Pad rather than a
      joystick, which some games may display or handle differently. Check whether the regular
      V.Smile Pocket's D-Pad behaves differently too; it reportedly acts like a joystick. Its
      writing pad comes after the base Writing Pad (section 6).
- [ ] **Region jumpers in games.** Soccer Challenge changes its title to Football Challenge with
      certain region codes. Survey which games react to the region code.

## 6. Peripherals research

Each needs its protocol worked out from the games' own code. toadster172's u'nSP decompiler
(VSmileDev) and KanedaFr's BIOS disassembly are the likeliest tools.

- [ ] **PC Pal as a wired controller.** The PC Pal appears to run a standard V.Smile BIOS (likely
      v1.03, with the standard intro; from a power-on video, not confirmed technically). A PC
      Pal dongle exists for other consoles' controller ports. So it may be addable as a
      controller type, like the Motion: infrared keyboard, mouse and writing pad. Its writing
      pad likely matches the base Writing Pad.
      - *Wow! Wow! Wubbzy! - Attack of the 50-foot Fleegle* also supports the PC Pal. It's the
        only dump marked "(World)": check whether its other languages, and other-region keyboard
        support, are all included, possibly selected by the region jumpers.
- [ ] **Pen devices**, in this order:
      1. **Writing Pad** (first: the base pen protocol).
      2. **Smartbook.** It also works as a normal controller: the pen docks into a holder on the
         side panel and acts as a joystick, with the other buttons above it. There's a gap with
         no touch area between the two pages; find out whether it's accounted for as dead space.
         Emulation idea: a button toggles an overlay of the book's pages, which you tap to
         trigger the effect. Toy Story 2's manual has surprisingly high-res page images; extract
         them at native size rather than as rendered in the PDF.
      3. **Art Studio tablet** (last).
      All map naturally to the Switch touch screen.
- [ ] **V.Link.** A plain SPI serial flash on the console's SPI bus (Motion schematic: IOA0–IOA4
      = clock, MOSI, MISO, busy, chip select). Two confirmed uses in games:
      - **Scores**, uploaded to an online leaderboard. We could parse and show them locally.
      - **Coins**, about four per game, which unlocked PC "Bonus Games". Several game manuals
        give the exact coin unlock conditions, which makes an easy test. Known bonus game names:
        Fish Tank Fix, Smarty Pants, Crossing Crab, Hurry Back, Cover Story (packaging), plus In
        The Zone, Funny Bowling, Dancing Monkeys, Chicken Dance (a low-res website mockup in a
        commercial; more aren't readable). Unclear whether they copied back to the V.Link or
        were web games.
      - **PC Companion software.** Two different setup packages are in
        `reference/V.Link PC Companion Software`. Extract the programs without installing, then
        decompile to see how they read the V.Link's data and whether any bonus-game data is
        inside. Its file manager counts storage in "memory blocks": about 432–433 per V.Link;
        Action Mania's data takes 1. Bring as much of the companion software into the emulator as
        we can.
- [ ] **Microphone.** In hardware it's summed into the audio output; whether any game reads it is
      unknown. Wanted, but one of the last items.

## 7. Research housekeeping and documentation

- [ ] **Manuals pass.** Go through every manual and other PDF in `reference/` and its
      subfolders:
      - **Game manuals:** note any peripheral mentions (V.Link coin conditions, PC Pal or Motion
        support, ...) or any other odd special cases.
      - **Hardware manuals:** record everything they say.

      Log it all in `findings.md`. *Progress:* the Motion and Smart Keyboard manuals are logged
      (`findings.md` §17). Still to go: the other 15 in `reference/Manuals/Hardware` (V.Smile,
      Pocket, Cyber Pocket, Writing Pad, Smartbook, Art Studio, PC Pal, V.Link, Jammin' Gym Class,
      V.Motion, power packs, car adapter) and the 70 game manuals in `reference/Manuals/Games`.
- [ ] **Community write-ups in `docs/`.** One technical write-up per peripheral we work out that
      isn't documented anywhere (Motion, Smartbook, Art Studio tablet, PC Pal, ...). Plus one
      for small undocumented facts, e.g. the Smart Keyboard's Spanish language code (`0x43`)
      and the console-type routine's IOC5 check. Give the findings back to the community.
      *Started:* `docs/README.md` indexes the Motion controller, standby and power buttons,
      console type / BIOS / test screen, additional findings, and emulating the V.Smile fast
      (1.2.0). Add a page per peripheral as each is worked out, and keep the existing ones
      current.
- [ ] **Saved chats: look for repo links.** Check the chats in `reference/` once more for GitHub
      (or similar) repository links we haven't looked at.
- [ ] **ROM requests.** `reference/roms/full romset.txt` lists the whole set (ignore the V.Smile
      Baby entries). Check exact filenames there when asking for specific test ROMs.

## 8. Long term

- [ ] **PC version.** Once the Switch build is in good shape, adapt it into a PC emulator. The
      desktop test harness already runs the core, so it's mainly GUI work.

## Done (for reference)

- 1.1.0: Smart Keyboard (USB keyboard), V.Smile Motion (one or two players, gyro-gated
  steadiness filter), Jammin' Gym Class dance mat, analog stick tilt levels, real standby with
  Console ON/OFF/RESTART, Reset Game as RESTART, Quit kept as a power-off, PAL/NTSC, the
  VTech-logo jumper, the controller LED overlay, BIOS-only boot and its test screen, the
  corrected Motion console type, and save states that keep the console configuration.
- 1.2.0: the performance pass (stock-clock full speed for most games), the audio fixes,
  Auto frame skip that counts drawing, the performance overlay and log, the joystick freeze fix,
  German/French Shrek Forever After and Cars 2 and German/Spanish Toy Story 3 booting, the
  Spanish Motion BIOS's test screen and region labels.
- Checked since 1.1.0: direction mashing in *Shrek: Dragon's Tale* doesn't lock up our core
  (USA, USA Rev 2, Germany).

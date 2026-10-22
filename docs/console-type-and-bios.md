# Console type, BIOS identity and the test screen

## Console-type straps (port A)

The wiki's `io` page, from the V.Smile Motion schematic, gives the console type as two strapped
pins on port A:

| Console | IOA14 | IOA15 | Port A value |
|---|---|---|---|
| V.Smile, V.Smile Pocket | 0 | x | `0x0000` (or `0x8000`) |
| V.Smile Motion | 1 | 0 | `0x4000` |
| Cyber Pocket | 1 | 1 | `0xC000` |

MAME's `vsmilem` driver returns `0xC000` for the Motion, which by this table is the Cyber Pocket.

### How games read them

We disassembled every port A read (`3D01`) in the Motion carts we have. Two patterns:

**IOA14 only.** Used by Action Mania, Winnie the Pooh, Up, Little Einsteins, Scooby-Doo!, Thomas
and Spider-Man. For example, Action Mania's `0x5608C`:
```
r2 = [3D01]
if (r2 & 0x4000) return 2      ; Motion-type board
if (r2 & 0x8000) return 1
return 0
```
Little Einsteins has the same routine at `0x4BBFB`.

**The shared three-way routine.** At `0xB9C8` in most carts; `0x41101F` in Shrek Forever After. It
stores a console-type variable (Shrek: `[142B]`):

| `3D01 & 0xC000` | Result |
|---|---|
| `0x4000` or `0x8000` | type 0 |
| `0x0000` | type 2 |
| `0xC000` | type 1 if **IOC5** (`3D0B` bit 5, port C) reads high, else type 0 |

Type 1 then reconfigures port A; it's presumably the Cyber Pocket's handheld (LCD) mode. So
`0x4000` is the Motion value every game agrees on.

IOC5's role is otherwise unknown (the wiki suggests LCD/TV detection). An emulator that drives
IOC5 high and reports the MAME value `0xC000` sends these games down the type-1 path.

### The console type is part of a running game's state

Changing the console type under a running game makes it restart. On the harness, the Smart Keyboard
cart restarts mid-game when the board changes from standard to Motion. Changing the BIOS image
alone doesn't. Save states should therefore record the console type and restore it.

The BIOS intro doesn't depend on the console type: neither Motion BIOS changes its boot animation
with it. IOC5 held low also changes nothing visible at boot.

## What the US "Motion" BIOS really is

`vmotionbios.bin` (CRC32 `427087ea`) was dumped from a US Motion unit. MAME's note says it "doesn't
show the 'Motion' logo at all". It is the standard **v1.03** BIOS (`387fbc24`) word for word,
except the first 10 words, which hold the ASCII tag `078800VER0`. Those words sit under internal
RAM and can never be read by the CPU, so the code is v1.03 in full: v1.03 region codes and the
v1.03 test screen.

`0788` matches that console's FCC ID (62R-0788) and PCB code (35-078800-001). So that unit
genuinely boots with the plain V.Smile intro, under every region code and console type. Only the
Spanish dump (`vsmilemotion.bin`, `60fa5426`) contains the Motion animation, and it has no test
screen.

A third boot animation exists on the **V.Motion** rebrand. Whether its BIOS is undumped, or the
animation is in the Spanish dump behind a strap or region code, is open.

## The BIOS test screen

KanedaFr found that holding RESTART, OFF and ON (all active low) at boot opens the BIOS's "Main
Unit Test Program". Keeping RESTART held goes on to a colour-bar pattern. Our addition: **it only
appears when the BIOS boots without a cartridge.** With a cart inserted, the cart's code runs from
reset and the buttons do nothing.

With the slot empty, the BIOS answers at every bank (MAME's `banked_map`). To emulate an empty slot:
1. Map the BIOS everywhere.
2. Hold all three buttons for about a second from power-on.
3. Release them.

What it shows:
- **Version and language.**
- **Two checksums**, over `008000-` and `300000-`. They're BIOS-specific:

  | BIOS | Checksums |
  |---|---|
  | v1.03 | `DA64` / `DA64` |
  | v1.00 | `8F05` / `2F61` |

- **"Power"**, the battery ADC; `03FF` is full.
- **A live controller test.** The X and Y readouts show the joystick level: **3–7 for levels
  1–5**, with a direction dot. Buttons light up as pressed. This makes it a precise check of any
  controller encoder. We used it to verify analog stick levels and every dance-mat panel byte.
- **"Current Stick: A (ON Key)"**: pressing ON switches the test to controller port B.

### How the BIOS decides (v1.03)

From KanedaFr's disassembly, `showBiosScreens` (`0x19317`) shows the test screen when, at
power-on:
- ON was held (`[27D2] == 1`), set by `getOnOffStatus`;
- and OFF is held (`readOFFStatut`, `3D06` bit 7 low);
- and **either** RESTART is held (`readResetStatus`, bit 3 low) **or** `getTP5` returns 0.

`getTP5` (`0x17FC5`) reads `3D0B & 0x20`: **port C bit 5, IOC5**. So IOC5 is a test point ("TP5"),
and grounding it replaces RESTART in the combination. The wiki lists IOC5 as "unknown, maybe
LCD/TV detection? test point in older consoles?".

IOC5 grounded also makes the BIOS run `sub_1A0A7` first. That routine waits, then sets port C
bit 11 and port A bit 12 with short delays: power sequencing for extra hardware. Inferred: IOC5
grounded marks a handheld unit (V.Smile Pocket, LCD on port A, no RESTART button, so ON+OFF
alone opens the test). The console-type routine games use (above) also treats `0xC000` with IOC5
high differently.

v1.03 also writes a RAM **signature** (`55AA AA55 5A5A A5A5 5456…`) at `0x0000` and `0x27B8`
(`install_signature`, `0xADE4`). A wake from standby finds it, a cold power-on doesn't.

### The Spanish Motion BIOS has one too (v7.05)

`vsmilemotion.bin` doesn't react to RESTART+OFF+ON, but it does contain a test program: **"Main
Unit Test Program" Ver 7.05**, checksums `42C5` / `4519`. Its entry check at `0x11A16` (called from
`0x11650`):
1. Runs only if OFF is held at power-on.
2. For about 2 s (`0x7CF` ticks of the `[0842]` counter) counts OFF press/release changes.
   Changes within `0x32` ticks of the previous one don't count.
3. At **10 changes** it sets up the screen (`0x621F` with `0x16F`/`0x46F`, the same arguments
   v1.03 passes before its test screen) and runs it.

So: **hold OFF while powering on, then press OFF about 5 more times, roughly 3 presses a
second**. On the harness, changes 9–15 frames apart (0.15–0.25 s) work. Faster ones are
debounced away; slower ones miss the window. ON isn't needed, which fits the Motion's front
panel: there is no ON line, and power-up is the reset button. The procedure on real hardware is
inferred from the code and the emulation.

This BIOS was restructured (reset at `0x704C` against v1.03's `0xAEE2`), but it keeps v1.03's helpers
byte for byte at new addresses: `getTP5` `0x4606`, `readOFF`/`readReset`/`readON`
`0x43F4`/`0x43FB`/`0x4400`, `sleepmode_microcode` `0x11FA6`. A console-button dispatcher at `0x452D`
handles OFF (`0x4515`), RESTART (code `0x600`, `0x460A`) and ON (code `0x601`). About half of its
256-word blocks are shared with v1.03, mostly graphics and sound.

**No V.Motion animation found.** All 16 region codes show the same V.Smile Motion logo, apart from
the subtitle and the trademark mark (®, ™ or none). Codes 0/1 skip the logo, as on v1.02+. The
console-type straps don't change it either. So the V.Motion boot animation is either in an
undumped BIOS or behind something not yet found.

### Its region codes, read from the test screen

The test screen prints a **"Language:"** line, which gives each region code a direct reading
instead of a guess from the logo. Each code was booted on the harness three ways: the test
screen, the boot logo, and the insert-cartridge screen with no cart.

| Code | Test screen | Logo subtitle | Mark | Insert-cartridge prompt |
|---|---|---|---|---|
| 0 | UK | (logo skipped) | | "Please insert a Learning Game" |
| 1 | US | (logo skipped) | | none |
| 2 | IT | Active Learning System | ® | "Per favore inserisci una cartuccia di gioco" |
| 3 | US | none | ® | none (V.Smile cart picture) |
| 4 | US | none | ® | "Please insert a Learning Game" |
| 5 | US | Active Learning System | ® | "Please insert a Learning Game" |
| 6 | UK | Active Learning System | ® | "Please insert a Learning Game" |
| 7 | 中 (Chinese) | Active Learning System | ® | 請插卡 |
| 8 | PO | Sistema Educativo | ® | "TV Learning System" |
| 9 | DU | Active Learning System | ® | "Plaats een game" |
| A | UK | Active Learning System | ® | "Please insert a Learning Game" |
| B | GE | Aktives Lernspiel - System | none | "Bitte Lernspiel einstecken" |
| C | SP | Aprendizaje Inteligente En Acción | ® | "Por favor, inserta un cartucho" |
| D | FR | Apprendre En Mouvements | ® | "Insérer une cartouche" |
| E | UK | Active Learning System | ® | "Please insert a Learning Game" |
| F | US | Active Learning System | ™ | "Please insert a Smartridge™" |

What this corrects or adds compared with MAME's list (`vsmilem`'s REGION port):
- **8 is Portuguese, not Mexican Spanish.** MAME went by the "Sistema Educativo" subtitle, which
  fits both languages. The test screen says PO. The insert prompt falls back to English.
- **9 is Dutch.** MAME marked it "(?)". The test screen says DU and the prompt is Dutch.
- **0, 1, 3, 4 and A**, which MAME doesn't list, are English variants (the table above).
- **5, 6, A and E draw identical frames** from power-on to the insert screen; every frame hash
  matches. Only the test screen's tag separates 5 (US) from 6/A/E (UK). F is the US retail
  setting, with the ™ mark and the "Smartridge" prompt.

Not checked: the voice clips, since the harness has no audio output. Also not checked: what a
cartridge does with each code, since games read the same jumper. The emulator's Region row uses
these labels when the Spanish Motion BIOS is selected.

### A stress test for controller emulation

Mashing all seven buttons plus every joystick level on this screen is a good stress test. On
D.Smile NX 1.1.0 it froze the screen for tens of seconds at a time, with the last colour LEDs
stuck lit; the CPU kept running in `wait_blank`, but no input arrived. The cause was the
emulated controller holding RTS while the console ignored the edge. See
[the RTS edge rule](additional-findings.md#the-console-ignores-rts-edges-it-isnt-ready-for).

## Other BIOS-like images

`vsmileswedenbios.bin` (8 MB, CRC32 `8b464b19`), labelled as a Swedish V.Smile BIOS, is neither a
V.Smile BIOS nor a V.Smile cart. It's built on the same code base as the **V.Smile Baby** BIOS
(`ddc7f845`):
- **Same library banner:** `Ours-135Lib V1.0.03 generated by TV4, 2006-02-07`.
- **Same vector table.**
- **Byte order:** the Baby dump is stored byte-swapped relative to it.
- **Different content:** about 24% of their 512-byte blocks are shared, and the part numbers
  differ (`99021-069-001 A12` against the Baby's `99000 D07`).

It's most likely a V.Smile Baby BIOS from another region or revision.

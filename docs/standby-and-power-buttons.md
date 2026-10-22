# Standby and the power buttons

The V.Smile's ON and OFF buttons don't switch the power. They're inputs to the SoC, and the
**game** decides what they do. "Off" is a low-power standby in which RAM stays powered. That's
how games can "remember your place" across power-off: the Smart Keyboard manual, for example,
says that after OFF and ON you get a Continue / New Game screen.

Prior notes: V.simer and others in VSmileDev described the general idea (software-driven buttons,
standby, wakeup sources) and the wakeup-source register. This page adds the exact sequence, wake
behaviour, and what RESTART really is.

## The pins

Port B, all active low (wiki `io` page):

| Pin | Button |
|---|---|
| IOB6 | ON |
| IOB7 | OFF |
| IOB3 | labelled "RESTART"; on the Motion board it goes only to test point TP3 (see below) |

## Going into standby

What a game does when OFF is pressed (Little Einsteins, traced on the harness):

1. It plays a short localised goodbye clip ("Goodbye!", "Tschüss!", ...).
2. It copies a few instructions into RAM and runs them from there (Little Einsteins: from
   `0x2625`), since the cartridge is about to be ignored.
3. That code writes:
   - **`3D29 ← 0x0080`**: wakeup source = key change (a console button).
   - **`3D2A ← 0x00C8`**: wakeup time.
   - **`3D28 ← 0xAA55`**: sleep.
4. The `0xAA55` write halts the CPU. System control `3D20` was set to `0x4006` first (sleep
   enabled, audio and video disabled).

The v1.03 BIOS does exactly the same; KanedaFr's disassembly calls it `sleepmode_microcode`
(`0x19B81`). Its other sleep path (`0x19AB9`) also sets `3D29 = 0x80`, `3D2A = 0xC8`.

How long the goodbye takes varies by game (frames from OFF to standby, at 60 fps):

| Game | Frames |
|---|---|
| Scooby-Doo! Funland Frenzy | 15 |
| Thomas & Friends | 55 |
| Smart Keyboard | 60 |
| Little Einsteins | 80 |
| Disney-Pixar Up | 150 |

## Waking up

When a wakeup source fires, the CPU continues **right after the `0xAA55` write**. In the BIOS's
version, the RAM code runs a short delay loop, sets external memory control (`3D23 |= 6`), and
jumps through the vector just before the reset vector: `[0xFFF7] − 1`, i.e. the word at
`0xFFF6`. That vector is the wakeup handler (`WAKEUP_handler` in the BIOS disassembly). Games
behave the same way (verified by waking them on the harness); their vector table is the cart's.

The handler re-initialises the hardware (watchdog, GPIO directions, ADC, SPU) and clears and
re-initialises the program's own variables. The game then runs its startup again, but RAM outside
what it clears survives. So a game can find its saved state and offer to continue. In the Smart
Keyboard cart, Cyber Adventure > Adventure Play shows **New Game / Continue Game** after OFF and
ON, but goes straight to the level settings after a cold boot.

The key-change wakeup has to mean ON. If any button change woke the console, pressing OFF on a
console that's already off would turn it back on. We wake on the ON line being pressed.

## Booting holds ON

At power-on, games read the ON line early and put themselves straight back into standby unless ON
is held. A real console boots while you're pressing ON. An emulator should hold ON for the first
part of boot. D.Smile holds it for 20 frames, and none of 41 carts tested falls asleep at boot.

## RESTART is a hardware reset

The manuals describe two front-panel designs:
- **Original V.Smile:** separate ON, OFF and RESTART buttons. "RESTART: Press this button when
  the unit is ON, to start the game over from the beginning". There's also a separate recessed
  Reset button underneath, for troubleshooting.
- **The later V.Smile (Writing Pad model), the Motion and the PC Pal:** a single **ON/RESTART**
  button. "If you press the ON/RESTART Button while playing a game, the game will start over from
  the beginning."

The Motion schematic (FCC filing, drawing 80-78800 "VSA CONSOLE") shows how it's wired:
- **Front-panel connector:** CON5 "ON-OFF" carries only **OFF**, **GND** and **START** (the
  ON/RESTART button).
- **START goes straight to `/RST`.** In the "new reset circuit" it reaches the SoC's reset line
  through D4, marked "short".
- **No ON circuit.** The old circuit that drove the ON line is boxed "REMOVE 'ON' CIRCUIT", with
  every part marked NC.
- **IOB3 "RESTART"** goes only to test point TP3.

So on the Motion, ON/RESTART is a **hardware reset**. A reset doesn't clear SRAM, so it does
double duty:
- **While playing**, the game restarts.
- **From standby**, the chip comes out of sleep through reset and the game still finds its saved
  state. On the harness, the Smart Keyboard cart offers Continue Game after OFF then RESTART, and
  also after RESTART mid-game.

Games' own code agrees that IOB3 isn't the normal restart path. Little Einsteins' console-button
handler (`0x2FFAB`):
- It reads `3D06 & 0xC8`, the pressed console buttons.
- It acts on RESTART (bit 3) only when port A says it's on a Motion-type board.
- It calls its power-off routine on OFF (bit 7).
- It matches other button combinations against a table at `0xA4BA`, possibly a hidden code.

Older games may still respond to IOB3 themselves: Scooby-Doo! reboots when IOB3 is pulled.

Our best reading for the original console's RESTART button is the same hardware reset, which fits
both the manual and the wiki's "IOB3: labelled RESTART, not used?". That's **inferred**: we only
have the Motion's schematic.

## Emulating it

What D.Smile does, and what we'd suggest for any emulator:
- A `0xAA55` write to `3D28` (with `3D20` bit 14 set) halts the CPU. Video and audio output stop.
- An ON press (falling edge of IOB6) wakes it when `3D29` has the key-change bit (`0x80`). The 2 Hz
  bit (`0x04`) is honoured too.
- RESTART resets the SoC while keeping RAM.
- Standby and its wakeup sources belong in save states.

MAME and veesem ignore the sleep write. A game then "wakes" the instant it sleeps, so OFF plays the
goodbye clip and reboots.

Games also turn themselves off after a long idle spell; the BIOS has "Auto Shut-off..." messages.
A front end without an ON button needs another way to wake the console.

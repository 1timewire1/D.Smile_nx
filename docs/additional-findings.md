# Additional findings

Smaller facts that don't need a page of their own. Each says where it was observed.

## Smart Keyboard

- **Language byte `43` is Spanish.** The wiki lists `40` US, `41` UK, `42` French, `44` German.
  Sending `43` to the Spanish cart (*Teclado Interactivo V.Smile*) gives ñ and an accent dead key.
  Every Smart Keyboard cart tested (US, FR, DE, ES) switches its whole typing layout to whatever
  language byte it receives; with `40` they all type US characters.
- **The first probe after the language byte varies.** The wiki shows the console answering the
  language byte with `70` (keyboard `BA`). On the US cart we saw `7D` (reply `B9`). It's the
  normal `7x` probe, and its low nibble varies.
- **The keyboard joystick may send only the axis that changed** (`7F` alone), as MAME does, rather
  than the pair (`7F 80`) the wiki lists. The cart accepts both.
- **The key right of P** on the US layout is `/ ?`, not `'` as MAME labels it.
- **The keyboard is only detected at boot.** The manual's troubleshooting says it "does not work"
  if it was reconnected after the program started, and a power cycle fixes it. Presumably the
  cart only runs the `52 52 52` handshake at boot (inferred, not traced).
- *Quirk:* the US cart's login NAME field refuses the word "hell" ("hello" → "helo"). That's the
  game, not a dropped key.

## The console ignores RTS edges it isn't ready for

The console notices a controller wanting to send on the **falling edge** of its RTS line
(external interrupt EXT1/EXT2, IRQ5). The v1.03 BIOS's handler for controller A (`EXT1_update`,
`0xA9CD`) returns **without granting CTS**, and without scheduling a retry, when:

- a transfer is already marked busy (`controller_settings & 0x28`), or
- the last UART activity was less than about 8 ms ago (`tmb2CounterLow − lastUARTTime < 5`).

So a controller that asserts RTS during that window and then just holds it is never served. Fast
input makes that window routine: every LED byte (`6x`), probe (`7x`) or Motion command the console
sends is UART activity. A controller has to release RTS and assert it again (a fresh edge) when no
grant arrives within roughly 10–15 ms.

How it showed up in D.Smile NX 1.1.0, whose emulated controller held RTS and waited (a 0.5 s
timeout then threw its queue away):
- **BIOS test screen:** mashing everything froze the screen for 25–58 s at a time.
- **Two Motion controllers:** they lost button presses, which forced a halved report rate.

With a 12 ms re-request (`VSmileJoy::RearmRts()`), 60 s of mashing on the test screen delivers
all 3,665 bytes with no timeouts. Two Motion controllers streamed at full rate too, but see the
next paragraph.

**But not for a Motion controller mid-report.** A Motion report is a multi-byte packet, and the
console takes CTS away in the middle of one to send its own commands (`05`, `8n`, `9n`).
Releasing RTS there and asking again broke *Toy Story 3*: its main menu ignored OK, and in play it
kept saying "Please reconnect your controller". Of the three re-request triggers (no grant within
12 ms; CTS dropped with bytes queued; CTS gone when a byte finishes), the last did most of the
damage, but leaving out only that one still produced occasional disconnects. D.Smile NX 1.2.0
therefore re-requests for the joystick only; a Motion controller holds RTS as 1.1.0 did, and two
Motion controllers keep the halved report rate.

## Controller LEDs

- The console lights a controller's colour-button LEDs with `6x` bytes: bit 0 green, 1 blue,
  2 yellow, 3 red, `60` = all off.
- Games use them as **press feedback**, and not all do. On the harness, Disney-Pixar *Up* and
  *Little Einsteins* light Red (`68`) and Blue (`62`) while those buttons are pressed.
  *Scooby-Doo! Funland Frenzy* and *Thomas & Friends* never send LED bytes.

## Joystick levels

- The joystick reports 5 levels per direction (the wiki's `C3`–`C7` / `CB`–`CF` / `83`–`87` /
  `8B`–`8F`). The BIOS test screen shows the received level directly: its X/Y readout is
  **level + 2**.

## Jammin' Gym Class dance mat

- The mat sends the bytes in MAME's `mat.cpp` (which match the wiki's captures). Several of them
  reuse joystick bytes with different meanings:
  - Right arrow is `CD`, the pad's "left, level 3".
  - Yellow footprint is `CB`, "left, level 1".

  So a standard controller can't play mat games. On the harness, the US cart's athlete picker
  responds to the mat's Right (`CD`) and ignores a joystick's right.
- The manual's layout is a 3×3 grid:
  - **Corners (1, 3, 7, 9):** the colour footprints.
  - **Edges (2, 4, 6, 8):** the arrows.
  - **5:** the centre panel.
- Two-player mode needs a mat on each port.

## Carts with swapped ROM halves

Several 16 MB Motion carts are built from two 8 MB epoxy blobs, wired the opposite way round from the
US release of the same game. The Shrek rows come from MAME's `vsmilem_cart.xml`; the Cars 2 and Toy Story 3 rows come from testing:

| Cart | blob1 (CRC32) | blob2 (CRC32) | Layout |
|---|---|---|---|
| *Shrek Forever After* (USA) | `175117f1` at 0 | `5dc681ea` at `0x800000` | in order |
| *Für immer Shrek* (Germany) | `3f1c79e2` at **`0x800000`** | `78cf731b` at **0** | swapped |
| *Shrek 4 - Il était une fin* (France) | `bb3d3e09` at **`0x800000`** | `7c633e07` at **0** | swapped |
| *Cars 2* (Germany) | `8d5eeb71` | `7a5b9ead` | swapped, though MAME loads it in order |
| *Cars 2* (France) | one 16 MB file, first half `e9ce0620` | | swapped, though MAME loads it in order |
| *Toy Story 3* (Germany) | `845bbb46` | `c006e9b6` | swapped, though MAME loads it in order |
| *Toy Story 3* (Spain) | `a26c152a` | `8ab8e24b` | swapped, though MAME loads it in order |
| *Toy Story 3* (France) | common dump `1eebefa2`; MAME's `2cb9d987` | | common dump in order; MAME's file is the same data swapped (first half `f1596894`) |

Common 16 MB dumps of the German and French Shrek carts store blob1 first. Loaded that way, the
boot code runs from the wrong half: the CPU crashes into data in the first frame, and the screen
is garbage. Swap the halves at load (we match on the first half's CRC).

The German and French **Cars 2** and the German and Spanish **Toy Story 3** dumps match MAME's
CRCs and MAME's load order exactly, yet they stay on a black screen until the halves are swapped. Then both boot normally. So MAME's
`cars2mge`/`cars2mfr`/`toystor3mge`/`toystor3msp` entries most likely don't boot either. The two halves of every 16 MB cart
start with the same boot stub at byte `0x10000`, and no header word reliably marks which chip is
which. So the dump order can't be checked structurally; it's a per-CRC list.

All three games select the upper half almost immediately (IOB1 low, MAME's `/CS2`), so the
"second" blob is the one they boot from.

## Region codes

- The console's region/language straps (port C bits 0–3) are also read by some games, not just
  the BIOS. *Soccer Challenge* becomes *Football Challenge* with some codes.

## Device IDs

- Keep-alive/ID bytes as Action Mania's library maps them: `55` joystick, `50`/`51` writing-pad
  controller (`54` is stored as `50`), `52` Smart Keyboard, `53` unknown, `56` Motion. See
  [v-smile-motion.md](v-smile-motion.md).

## Game-specific notes

- **Shrek: Dragon's Tale (USA):** Shrek's position is at `0x2131` (X) and `0x2132` (Y); walking for
  60 frames moves 48 units. Rapid direction mashing (every 1, 2 or 4 frames, USA, USA Rev 2 and
  German) doesn't lock up input on our core. MAME is reported to lose control for a few seconds
  after mashing.
- **Spider-Man & Friends: Secret Missions, Safari Showdown:** reacts to shakes, not held tilts.
  See [v-smile-motion.md](v-smile-motion.md).
- **Shrek Forever After, Bowling Bonanza:** throws on a 1-unit wobble. See
  [v-smile-motion.md](v-smile-motion.md).
- **Little Einsteins:** its console-button handler matches button sequences against a table at
  `0xA4BA`, possibly a hidden code. See
  [standby-and-power-buttons.md](standby-and-power-buttons.md).

# V.Smile Motion controller

The V.Smile Motion's wireless controller adds an accelerometer to the standard pad. To the SoC it
is just another serial controller: inside the console, a separate RF microcontroller turns the
wireless link back into the standard controller protocol on ports 1 and 2 (the controller's
PLAYER 1 / OFF / PLAYER 2 switch picks which). Everything below is what that serial stream looks
like and how games use it.

Reverse-engineered from *Action Mania* (USA) and *Shrek Forever After* (USA) with the emulator's
watch hooks, and confirmed working with real games in D.Smile NX. The base controller protocol
(4800 baud, RTS/CTS, `55` keep-alive, `7x`/`Bx` probes, button and joystick bytes) is on the
wiki's `controllers` page and isn't repeated here.

Contrary to some owner reports, motion is **not** just tilt converted into joystick bytes. Games
receive raw accelerometer samples and do their own interpretation.

## Two gates: the controller and the console

A game only offers motion controls when both of these hold:

1. **The controller identifies itself as a Motion controller.** A controller's `5x` keep-alive
   byte doubles as its device ID. Action Mania's library maps it to a device type (function at
   `0x43EA5`, reached through a jump table at `0xA6CE`):

   | ID byte | Device type | Device |
   |---|---|---|
   | `55` | (ignored) | standard joystick |
   | `50`, `51` (`54` is stored as `50`) | 3, 4 | writing-pad controller (Toy Story 3 shows its icon) |
   | `52` | 5 | Smart Keyboard (its boot "hello") |
   | `53` | 6 | unknown |
   | `56` | **7** | **Motion controller** |

   The ID is stored per port (Action Mania: `[22BD]` port 1, `[22BE]` port 2; parser at
   `0x86AC`–`0x86C6`). Many callers test for type 7.

2. **The console is a Motion console.** Games read port A's **IOA14** (`3D01` bit 14). With it
   low (an original V.Smile), a Motion controller silently falls back to standard controls. Action
   Mania's control-scheme routine (`0x45E34`) calls `0x5608C`, which returns 2 when IOA14 is
   set. See [console-type-and-bios.md](console-type-and-bios.md) for the full strap table.

With both in place, Motion carts show a "1P" screen offering Motion (option 1) or Standard
(option 2). In Action Mania the choice lands in RAM `0x0077` (1 or 2).

## Commands (console to controller)

Found in Action Mania's controller library (commands queued by `0x44D3E`, sent by `0x80EA`):

| Byte | Meaning | Reply |
|---|---|---|
| `05` | (Re)initialise. Sent when a Motion controller appears, and again after every `56` ID byte | none; hold reports briefly while the console sends its setup |
| `8n` | Set report mode *n* (0 = off) | **`Dn`**. Without the acknowledgement the game resends `8n` forever. The requested mode is kept in `[22B3]`, the acknowledged one in `[22B0]` |
| `9n` | A 4-bit setting (default `F`, `91` also seen) | none needed; meaning unknown |

Menus request mode 0. Gameplay requests mode 2 (Action Mania's Rapid Race asks for 2 whichever
control scheme is chosen). Types `50`/`51` (the writing pad) use other commands: `20`, `22`, `10`,
`11`.

## Reports (controller to console)

While a mode other than 0 is on, the controller streams accelerometer reports.

**Modes 1 and 2: `43 X Y Z`.** A `43` header, then one raw byte per axis.

**Modes 3 and 4: `43` plus four 6-bit bytes.** These pack the same 24 bits (each data byte is
below `40`). The game unpacks them into three 8-bit values and stores each as `FF − v`; in
practice X and Z arrive inverted. This is implemented from the code (Action Mania's unpacking at
`0x8309`), but **no game has been seen using modes 3/4**.

A controller byte `D8`/`D9` goes to `[2271]`, and `E0`/`E1` start another short sequence into
`[2270]`–`[2275]`. Neither has been seen from a real controller, so their purpose is unknown.

## Axes

Axes are in the order X, Y, Z. `80` is 0 g, and 1 g is about `40` units, so a controller lying
still and level reads **`80 80 C0`**.

| Axis | Direction | Example |
|---|---|---|
| X | right | tilting right raises X. Action Mania's Rapid Race steers on X, with a dead zone of about ±`10` (roughly 15°) |
| Y | toward the player | tipping the top away from you (tilting forward) lowers Y. In *Spider-Man & Friends: Secret Missions*' tree climb, "tilt forward to climb up" needs Y to drop |
| Z | down | gravity shows as +1 g, so `C0` when level |

The controller behaves as a gravity sensor, so tilt shows up as gravity moving between the axes,
and swings or shakes as short spikes.

## Timing and line discipline

These rules are what made the emulated controller reliable against real games. The real
controller's exact timing isn't known.

- **Leave the line idle between reports.** The console can only send its own commands (`8n`,
  `9n`, `05`, LED bytes) while the controller isn't requesting to send. Back-to-back reports
  starve it. About 40 ms of idle line between reports (roughly 19 reports a second) works well.
- **Re-send the `56` ID about every 250 ms while streaming.** Otherwise a port the game reset
  (see below) is never re-identified, because the keep-alive never fires while reports flow.
- **The console withholds CTS for long spells** while it sends its own commands. If a report is
  stuck half-sent, drop the rest of it rather than interleaving other bytes; the next `43` header
  realigns the game's parser.
- **Don't flush queued button events** on a timeout, only stale report and ID bytes. If a button
  release is ever lost, re-send the full current input state next time; otherwise a button can
  stay "held" forever.
- **Re-requesting the line: not mid-report.** The console ignores an RTS edge it isn't ready for
  and never comes back to it (see [the RTS edge rule](additional-findings.md#the-console-ignores-rts-edges-it-isnt-ready-for)),
  and for the joystick, releasing RTS and asserting it again after ~12 ms without a grant fixes
  the resulting stalls. For the Motion controller it backfired: the console drops CTS in the
  middle of a report to send its own commands, and re-requesting there broke *Toy Story 3*
  (menus ignored OK; in play, "Please reconnect your controller"). D.Smile NX 1.2.0 therefore
  holds RTS for the Motion controller, as 1.1.0 did. A variant that only re-requests between
  packets is untested.
- **Two controllers** need room to share the line. D.Smile NX reports every 80 ms with two
  controllers (40 ms with one) and sends the ID only once a second, which keeps the console's
  repeated Motion setup (`05`/`8n`/`9n` after every `56`) from eating into it. With the re-request
  above, both streamed at 40 ms (about 15 reports a second each in *Shrek Forever After*'s
  two-player character select, against about 8 at 80 ms), so the halved rate is a consequence of
  the stalls rather than a hardware limit, but the re-request isn't safe for Motion as it stands.

### A game-side race that looks like a disconnect

Action Mania's disconnect check (`0x88F3`) computes "now − last seen" as `[21C8] − [22D3]`.
`[21C8]` is a roughly 300 Hz tick (5 per frame, incremented at `0xB95E`), and `[22D3]` is stamped by
the receive interrupt. If that interrupt lands between the two reads, the result goes negative
(for example `FFFC`) and the game declares the controller disconnected.

After that the port is reset, and every report byte is parsed as an ordinary pad byte:
- `5x` values become a new device ID (Toy Story 3 switched to the writing-pad controller).
- `Cx`/`8x` look like joystick moves.
- `9x`/`Ax` look like button presses.

Keeping reports spaced out and re-sending the ID (above) makes this rare and self-correcting.

After a console reset, the console doesn't serve the port for about 2 s.

## How games use the samples

Games buffer samples per port and run their own recognisers. Action Mania's packet callback
(`0x47701`) copies each sample into a 5-entry ring buffer; a Motion library (configured at
`0x478xx`, helper `0x50804`) reads it.

What we've measured:

- **Spider-Man & Friends: Secret Missions, Safari Showdown, ignores held tilts entirely**, even
  ±`40` (90°). It reacts to shakes. Samples go through a 5-entry queue (`0x56FC3`, from `21C4`–`21C6`
  = Z, Y, X). An X shake of ±`30` (±0.75 g), alternating every 4 frames, makes Spider-Man run;
  ±`20` does nothing. A Y shake makes him crouch.
- **Shrek Forever After, Bowling Bonanza, throws on almost any change.** A change of just 1 unit
  that returns a quarter second later throws the ball, and so does a drift of +1 per second.
  Constant readings never throw, nor does a single step change of up to 8. So a real controller
  held still must report perfectly constant values.

For an emulator driving this from a modern controller's sensors, both cases have to work:
- **Pass real motion through unfiltered.** Spider-Man needs large, fast changes intact.
- **Hold the value perfectly steady when the hand is still.** Shrek throws on tremor and sensor
  noise.

D.Smile NX does this with a gyro-gated filter: it holds the last value while the gyro reads under
20°/s and the accelerometer isn't changing quickly, and passes raw values while moving.

## The Motion console's buttons

The Motion console's front panel has just OFF and a combined ON/RESTART button. ON/RESTART is
wired to the SoC's hardware reset; see
[standby-and-power-buttons.md](standby-and-power-buttons.md).

## Open questions

- **Arrow LEDs.** The Motion controller has LEDs around the joystick for the four directions, like
  the colour-button LEDs. Owners report they light as you tilt. How games drive them, if they do,
  is unknown.
- What `9n`, `D8`/`D9` and `E0`/`E1` are for.
- Whether any game uses report modes 3/4.
- The real controller's report rate.

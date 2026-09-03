# D.Smile - Switch port (bring-up)

**Prepping for a 1.0.0 release** - not code-untested, still needs the
two-player work above to get a hardware round, but the theme/branding
work in "1.0.0 release prep" below is done. Build's `.nro` metadata now
reads `D.Smile NX` by `SenseiFromClubPenguin`, version `1.0.0`. Also did a
documentation-only pass on several other V.Smile peripherals this port
doesn't support (microphone, Smartbook, PC Pal's keyboard/mouse/
digitizer, V.Link, plus a Jammin' Gym Class dance mat finding that turned
out to need no work at all) - see "Documented for later: other V.Smile
peripherals" below; nothing here changes any actual behavior.

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
splitting regression from the first attempt at fixing the next bug. That
next bug - **two controllers connected produces full crosstalk, either one
driving both players** - was never resolved with the same confidence as
the other three. **Shipping with it as a known limitation rather than
continuing to chase it**: V.Smile's own two-player games are turn-based/
alternating rather than simultaneous, so this doesn't block actually
playing them, just means both players currently share unrestricted access
to both controllers rather than each being confined to their own. Full
history, including why the manual **Players** setting turned out to be
both redundant and not actually a fix for this, in "Two-player controller
support" below. Testing also surfaced that V.Smile Art Studio expects a
digitizer/pen input this port doesn't have yet - noted, not investigated
further this pass (see its own section below).

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

### Crosstalk: either controller could drive both players

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

**Decision: ship with this as a known limitation rather than keep
chasing it.** Three real fixes landed from this investigation (the
`UartRxDone()` regression, the pairing applet's `LibAppletBadExit`, and
the Single-vs-Dual assignment-mode regression), each backed by an actual
mechanism found in libnx's own source - but the root cause of the
underlying crosstalk itself was never pinned down with the same
confidence, and further guessing without hardware access to reproduce it
directly doesn't seem like a good use of time against a 1.0.0 release.
Two controllers connected currently means *either one* can drive *both*
V.Smile players, rather than a clean 1:1 split - not desirable, but fully
functional for the common case: V.Smile's own two-player games are
turn-based/alternating rather than simultaneous-split-screen, so this
doesn't block actually playing them. Worth revisiting post-1.0.0 with a
fresh angle - most likely candidate for next time: whether the Switch's
own Controllers menu (HOME overlay, pre-assigning player numbers *before*
launching the app, entirely outside this port's own code) produces a
clean split where in-app assignment-mode calls alone haven't.

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
this pass. Worth knowing: this means Art Studio's load/save loop (cart
selection, NVRAM persistence) is confirmed working, but the actual
drawing gameplay most likely isn't usable yet without this input.

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

**Jammin' Gym Class (dance mat) - confirmed to need no extra work.**
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
nibble - and picks the right table automatically, by checking the active
BIOS file's CRC-32 against the two known Motion dumps' hashes
(`kMotionBiosCrc`) rather than guessing from the filename, which the user
could rename to anything.

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
likely a hang, crash, or garbage screen, not just a wrong language. Worth
deleting it or byte-swapping it before use; a correctly-ordered copy of the
same firmware already exists as `sysrom_france.bin`/`vsmile_v102.bin`, so
there's no need to fix it separately unless the goal is specifically having
a correctly-ordered `vsmilebios.bin` on disk.

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
- `source/core/switch_library.{h,cpp}` - **new**: the game list's data
  model. Scans every configured folder, resolves + decodes + uploads cover
  art, sorts per the current mode. `switch_menu.cpp` never touches the
  filesystem or GL texture lifecycle directly anymore - this owns it.
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
- `source/render/switch_render.cpp`, `source/audio/`, `source/input/` - the
  in-game bring-up pieces, unchanged.

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
browser) or removes a folder. Controller is entirely per-action button
binding now - no Players setting; a second controller being connected is
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
2. Copy V.Smile cartridge dumps there as `.bin` files. Optionally, drop a
   same-named `.png` next to any of them for cover art.
3. Optional: any `.bin` BIOS dump(s) in `sdmc:/switch/dsmile/bios/` - pick
   which one's active, and the region/language nibble, in Settings >
   Library & Storage > BIOS / Region.
4. Copy `dsmile_switch.nro` to `sdmc:/switch/dsmile/dsmile_switch.nro`.
5. Launch from hbmenu or an emulator's homebrew loader.

Region's language labels, the BIOS/Region main-menu lock, the audio
self-heal, single-player controller input, automatic two-player
*detection*, combined Joy-Con pairs staying combined, and cart-side NVRAM
are all hardware-confirmed working now. Two-player *crosstalk* (either
controller driving both players) is a known, accepted limitation rather
than something still being chased - see "Two-player controller support"
above for the full history. Worth keeping an eye on if it comes up again
anyway:

1. **Unplugging player 2 mid-session** - does play continue normally as
   solely player 1 rather than anything getting stuck or crashing. Not
   specifically tested yet.
2. If Pro Controllers or other USB controllers are ever tried, worth
   noting whether the crosstalk behaves the same as with two Joy-Con
   pairs (the only setup tested so far) or differently - not required
   before 1.0.0, just useful data if it happens to come up.

## What's still missing

No major Android-parity feature remains outstanding that's both feasible
and worth keeping - Rewind is in, and Fast Forward was tried, tested, and
found not worth shipping in this port's architecture (see "Fast Forward:
tried and removed" above). Every remaining `GameAction` in Settings >
Controller is wired to something real. What's left below is polish and
robustness rather than a missing feature, roughly in the order it's worth
tackling next:

1. **V.Smile Art Studio's digitizer/pen** - confirmed needed (drawing
   itself doesn't work without it, though cart loading/NVRAM saving both
   do), no reference implementation anywhere to check against (see "Noted,
   not implemented: V.Smile Art Studio's digitizer/pen" above) - noted for
   later, not investigated further this pass.
2. **A reusable confirm-prompt screen** - once built, covers every
   "no confirmation dialog" gap below in one pass instead of one-off fixes.
3. **ABC/Smart Keyboard accessory support** - deliberately left as a
   documented idea rather than attempted, pending resolving whether
   `swkbd`'s whole-string input model can be translated into the
   accessory's real one-keystroke-at-a-time protocol at all - see "Noted,
   not implemented: ABC/Smart Keyboard accessory" above. Lowest priority
   of everything here; very few titles used either accessory.

Known, accepted limitation (not blocking 1.0.0): **two-player crosstalk**
- with two controllers connected, either one currently drives both V.Smile
players rather than each being confined to its own. Detection, combined
Joy-Con pairs staying combined, and everything else about two-player
support is confirmed working - see "Two-player controller support" above
for the full four-round investigation, including three other bugs that
*were* found and fixed along the way. Since V.Smile's own two-player games
are turn-based/alternating rather than simultaneous, this doesn't block
actually playing them. Worth a fresh look post-1.0.0, most likely starting
from whether pre-assigning player numbers via the Switch's own Controllers
menu (outside this port's own code entirely) produces a clean split where
in-app assignment-mode calls alone haven't.

Done: two-player controller support (crosstalk aside - see the known
limitation above) and cart-side NVRAM saves, both found by checking
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

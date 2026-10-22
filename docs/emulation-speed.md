# Emulating the V.Smile fast, without changing what it does

Notes from D.Smile NX 1.2.0's performance work, for anyone writing or speeding up a V.Smile
(SPG200) emulator. The aim was full speed on a 1 GHz ARM core (the Switch at stock clocks)
without changing a single emulated cycle: every change was checked by hashing every frame, every
audio sample and the final machine state of a run, on every cartridge dump we have (122), against
the unoptimized core.

## The workload is fixed, not game-dependent

Every game we measured runs about **100,000 unSP instructions per frame** - roughly the whole
frame's 449,592 CPU cycles (NTSC) at 4-5 cycles each - however much work it actually has. When a
game finishes a frame's work, it spins in a wait loop until an interrupt changes a RAM flag:

```
0107d4: r1 = [0036]      ; Toy Story 3
0107d6: cmp r1, r2
0107d7: jne 107da
0107d8: pc = 07d4
```

That loop was 10% of Toy Story 3's instructions, 6% of Action Mania's, and over 80% of
Alphabet Park Adventure's. So an interpreter's per-instruction cost sets the frame time for every
game, and skipping the waits is worth a lot for some games and little for others.

## Skipping wait loops exactly

A pass of a wait loop can be replayed instead of interpreted when all of these hold for one
recorded pass:

- **No memory changes.** Some loops store on every pass - Alphabet Park copies the flag into a
  local before comparing it (`[bp+1] = [1dfd]`) - but rewriting the value already there changes
  nothing, so it's allowed. Any other write disqualifies the pass.
- **No device-register reads.** Polling a hardware register (PPU line, timer, UART status) can
  change from one pass to the next, and reads can have side effects.
- **No interrupt taken**, and the pass **returns to exactly the CPU state it started from** (all
  registers, flags, the shift buffers and the interrupt flags).

Then every later pass is identical until an interrupt is taken: only CPU writes change RAM on
this hardware (no device writes main RAM; DMA is CPU-triggered). Replaying means feeding each
recorded instruction's cycles to the devices as usual, and checking before each recorded
instruction whether an interrupt would be taken; if so, restore that boundary's CPU state and let
the interpreter take it.

Two practical points:
- **The first pass after entering a loop often differs** - flags left over from the code before
  it, or the first copy of a flag an interrupt handler just changed. Re-record a couple of passes
  before giving up on a loop.
- **Busy games run many short loops that aren't waits** (copies, clears). Remember the ones that
  failed, or the recording overhead eats the gain.

## Running the devices less often, exactly

The obvious speed-up - run many instructions, then catch the devices up in one batch - changes
behaviour on this hardware, for two reasons that are easy to miss:

- **The CPU sees device state through register reads and interrupts.** Before any read or write
  of a device register, the devices must be caught up to that instruction; any write can move
  their next deadline.
- **A device's work at a deadline can depend on how its cycles are split.** In D.Smile's core the
  32,768 Hz timebase charges an instruction's cycles once more when it fires (the timer-paced
  games depend on that phase), and a controller's re-arm counter restarts its retry counter within
  the same call. So batching is only exact when the event-free cycles before a deadline are run
  as one lump, and the instruction that reaches the deadline is run on its own.

With that, the devices only run when the nearest deadline (a scanline, an SPU sample every 96
cycles, a timebase tick, a UART byte, a controller counter) is reached: one comparison per
instruction instead of a check per device.

## Smaller things that added up

- **A page table for bus reads.** RAM, cartridge, BIOS and Art Studio NVRAM pages map straight to
  memory; I/O pages take the slow path. Keep one table per CS2 state (cartridges above 8 MB switch
  banks through a GPIO line) so a bank switch is free.
- **Don't rebuild CS:PC every fetch.** The unSP keeps its code segment in SR; incrementing PC and
  touching SR only on a 64K wrap saves a read-modify-write per instruction.

## Audio at a mismatched rate

The console makes 60.05 frames' worth of audio a second (27 MHz / (429 × 4 × 262 cycles)); a 60 Hz
display shows 60. Pushing each frame's samples at a fixed resampling ratio lets the output queue
drift empty and click. Nudging the ratio by at most 0.5% to hold the queue at a target level
absorbs the difference (it settles near -0.1%) without audible pitch change.

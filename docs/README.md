# V.Smile technical notes

Write-ups of V.Smile hardware and software behaviour that we worked out while building D.Smile NX
and couldn't find documented elsewhere. They're meant to be useful to anyone working on the
V.Smile, not just this emulator. Where something builds on existing community work (the
[V.Smile development wiki](https://vtech.pulkomandy.tk/), the VSmileDev and LFHacks Discord
servers, MAME, veesem, KanedaFr's v1.03 BIOS disassembly), that's credited inline.

| Document | Covers |
|---|---|
| [V.Smile Motion controller](v-smile-motion.md) | The Motion controller's serial protocol (device ID, commands, report formats, timing), how games detect the Motion console, axis conventions, and how games interpret motion |
| [Standby and the power buttons](standby-and-power-buttons.md) | How OFF puts the console into standby, how it wakes, and why RESTART is a hardware reset |
| [Console type, BIOS identity and the test screen](console-type-and-bios.md) | The IOA14/IOA15 console-type straps as games read them, what the US "Motion" BIOS really is, and how to reach the BIOS test screen |
| [Additional findings](additional-findings.md) | Smaller facts: the Smart Keyboard's Spanish language code, controller LED use, the dance mat, joystick levels, and other details |
| [Emulating the V.Smile fast](emulation-speed.md) | For emulator authors: the fixed ~100,000-instruction frame, replaying wait loops exactly, running the devices less often without changing behaviour, and audio at the console's 60.05 Hz |

**Conventions.** Addresses are unSP word addresses. Byte values are hex. "Harness" means our
desktop test build of the emulator core (`test/keyboard_test.cpp`), which can script inputs,
dump RAM and watch memory accesses. Statements are marked when they're inferred rather than
observed.

[iisu-integration.md](iisu-integration.md) is unrelated to the above: it documents launching the
Android app from front ends.

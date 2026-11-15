# Real-hardware test (New 3DS / New 2DS XL)

We need real frame-time data: the emulator's float performance is probably pessimistic, so ~3-4 fps in Azahar is not a
hardware verdict. A **profile build** runs a scripted route by itself (button input is ignored) and writes timing
to `session.log`. It is a measurement build, not a playable one.

1. Build it: `python 3ds-port/ps2rc3ds.py build --mode profile --max-seconds 1500`
2. Copy to the SD card (dry run first, then `--yes`):
   `python 3ds-port/ps2rc3ds.py stage-sd <SD root> --yes` (about 3.7 GB into `SD:/3ds/ps2rc-sa/`; the SD must have
   room and FAT32's 4 GB file limit is checked).
3. Launch `ps2rc-3ds-profile.3dsx` from the Homebrew Launcher (not as a CIA). Leave it alone: the start takes about a
   minute, then the route runs until the time cap. Power off or press START when it stops.
4. Copy `SD:/3ds/ps2rc-sa/session.log` back and attach it to an issue (it is a text log of timings and runtime messages, not game assets or code; read through it
   first and remove anything you would not want public).

Look for these lines in the log:

* `Citro3D timing (120 frames, ms/frame): between-presents ...` frame pacing (Azahar reference: ~230-340 ms)
* `Citro3D convert (120 frames, per frame): static ... lit ...` CPU vertex-conversion cost and counts
* `PROF window ... ms/it` per-iteration cost (25 fps means 40 ms per iteration)
* any line containing `fatal`, `error`, `abort`, or a crash screen

Not covered by this build: physical input, audio, sustained play. A release build (`build` without `--mode`) is the
one to try for those; please report what you see.

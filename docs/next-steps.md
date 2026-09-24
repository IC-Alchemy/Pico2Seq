**Pico2Seq: observations and the next steps I would take**

Written on 2026-09-21 after merging the delay into `DeCluttered` and adding
Shift + tempo-fader feedback control. These are findings and proposed work,
not claims that the proposed fixes have been implemented or hardware-tested.
I would stabilize this instrument's current sound and controls before adding
another effect or doing a large refactor.

The performance surface now reads:

| Fader | Normal movement | Shift + movement |
|---|---|---|
| 1 | Tempo, 45–200 BPM | Delay feedback, 0–100% |
| 2 | Delay wet mix, 0–100% | Delay time, 10–750 ms |
| 3 | Master volume | Compressor macro, Warm / Glue / Punch |
| 4 | Selected voice's gate length | Existing gate-length behavior |

With a step selected, the four faders retain their ENV assignments and
Shift-reset behavior. Feedback defaults to 85%; it reaches an actual 1.0
coefficient at 100%. Filtering and saturation remain in the loop, so 100%
is not a promise of an unchanged or endless repeat.

**Things worth improving**

1. **Compressor smoothing has a startup inconsistency.**
   [VoiceManager](../src/voice/VoiceManager.cpp) sets `macroAlpha_` to its
   30 ms smoothing coefficient in `init()`, but its constructor leaves the
   member at `1.0f`. [VoiceSetup](../src/app/VoiceSetup.cpp) constructs the
   manager and adds voices without calling the manager's `init()`. In that
   normal startup path the macro target itself can jump immediately, even
   though the compressor's detector/gain smoothing still operates. The
   existing macro-motion tests call `init()`, so they miss this difference.
   This is a source-confirmed discrepancy; its audible severity has not
   been measured on the board.

2. **The full test suite already has unresolved failures.**
   The merge passed 484 of 504 registered CTest checks. The same 20 failures
   occurred on the untouched compressor commit `df6b50d`; some checks are
   registered in both general and focused executables. Failures involve
   pitch/scale expectations, preset parameter ranges, envelope behavior and
   displayed values. They need classification, not blanket suppression.

3. **Combined DSP timing is not yet measured on hardware.**
   Compilation and host DSP comparisons passed, but neither measures the
   RP2350's worst rendering time while sensors, LEDs and OLED are active.
   The delay continues processing at zero wet mix so its history stays
   available. That is musically useful, but it still costs CPU.

4. **The compressor is not a guaranteed peak limiter.**
   [rpdsp::Compressor](../src/rpdsp/src/rpdsp/dynamics.h) applies envelope
   detection and smoothed gain reduction. The hard output ceiling comes
   from [PCM conversion](../src/app/Pcm16.h). Hot transients can therefore
   reach the final clamp. Also, master volume precedes compression, so
   moving volume changes how hard the compressor is driven. I would test
   whether that interaction feels right before considering a different order.

5. **Master effect settings do not survive a session reload or reboot.**
   [Session](../src/app/Session.cpp) captures master volume, but the current
   [settings snapshot](../src/pico2seq-core/persistence/ProjectSnapshot.h)
   does not contain delay mix/time/feedback or the master compressor macro.
   A saved pattern can consequently return with a different effect balance.

6. **The delay reserves more memory than its current time range needs.**
   [MasterDelay](../src/voice/MasterDelay.h) reserves 48,000 floats, about
   187.5 KiB, while its tuned maximum uses 36,000 samples at 48 kHz. There
   may be roughly 47 KiB to recover after allowing interpolation guard
   samples. Simply shrinking the capacity would also shrink the current
   maximum, because the two constants are coupled. This is an optimization
   opportunity, not evidence of an existing allocation failure.

7. **Master-control definitions are scattered.**
   DSP limits, fader mappings, OLED payloads and documentation repeat some
   of the same information. The earlier 1-second display versus 750 ms DSP
   limit was one consequence. A small shared description of master controls
   could keep names, units, defaults and ranges aligned.

8. **Build defaults and some older documentation need reconciliation.**
   The [build helper](../scripts/build_pico2seq.ps1) now defaults to the
   stable 150 MHz baseline; higher clocks remain explicit choices. Dependency
   versions come from the installed Arduino environment. Some architecture
   text also still describes a PPQN read/modify/write race, while the current
   [clock consumer](../src/app/ClockService.cpp) disables interrupts around
   taking and clearing the pending count. Verify the current ownership and
   update the explanation rather than assuming an old warning is current.

**The order I would work in**

1. **Keep a reproducible baseline and exercise the actual instrument.**

   Preserve the merged revision, the feedback patch, both submodule revisions,
   the exact board options and the resulting firmware artifacts. Record the
   Arduino core and library versions too. For this session the core is
   rp2040 6.1.0; installed FastLED is 3.10.5 and uClock is 2.3.0. Those are
   observations about this machine, not a universal compatibility promise.

   On the board, test each normal and Shift function independently. Hold
   Shift, sweep feedback, release Shift and confirm tempo stays where it was
   until the fader is deliberately moved again. Repeat for mix/time and
   volume/macro, including physical endpoints. Check the OLED shows the
   appropriate percentage or milliseconds. Then select a step and exercise
   all four ENV sliders, their Shift resets, and voice/step changes.

   Listen at feedback 0%, 85% and 100%, with short and long delay times.
   At zero feedback, a note should produce one delayed copy; high feedback
   should add regeneration while the existing darkening and saturation remain.
   Move time while a tail is audible. Stop/start transport and set master
   volume to zero. Check that audible tails follow the established mute behavior.

   **Finished when:** the control table matches the physical instrument,
   endpoints work, Shift release causes no unrelated parameter jumps, and
   the sound is accepted by listening. Keep this as a short repeatable check
   for subsequent changes.

2. **Fix the constructor-versus-init smoothing difference in one small change.**

   Add a host test that creates a `VoiceManager` exactly as firmware startup
   does, without explicitly calling its `init()`. Move the compressor macro
   while a deterministic signal plays. Compare it with an explicitly
   initialized manager at the same sample rate, volume and macro setting.

   Initialize the macro coefficient consistently in the constructor and
   `init()`, preferably through a small shared initialization helper. Do not
   change the Warm/Glue/Punch anchors or introduce a second compressor.
   Ensure reinitialization remains a startup/stopped-audio operation; UI
   movement must never reset a live detector or delay history.

   **Finished when:** both startup paths produce the same intended macro
   response, existing compressor tests pass, and a board listen confirms
   smooth movement without changing the settled sound at each anchor.

3. **Make the existing tests describe the intended musical behavior.**

   Group the 20 registered failures by shared cause. Start with pitch and
   octave expectations, then preset parameter ranges, then envelope behavior
   and displayed units. For each group, trace the value through patch base,
   recorded modifier, composed playback value and final DSP mapping.

   Decide whether an earlier musical change deliberately changed the contract
   or whether the code is wrong. Update an expectation only after establishing
   the intended behavior. Do not retune all presets merely to satisfy stale
   tests. Preserve the rule that recording modifies patch bases rather than
   overwriting them.

   Fix one cause at a time with a focused regression, then run the full suite.
   Keep the original failure names and baseline logs available so a new
   failure cannot hide among existing ones.

   **Finished when:** every remaining failure has an explicit explanation
   and owner, with the target being a genuinely passing full suite rather
   than an exclusion list.

4. **Measure audio deadlines and headroom before optimizing or adding DSP.**

   Use the existing audio heartbeat/diagnostic path. At 48 kHz, a 256-frame
   buffer represents about 5.33 ms. Record worst render time and underrun
   changes while four demanding voices run and delay time, feedback and
   compressor macro move. Include both sustained notes and dense retriggers,
   plus normal lidar, OLED and LED activity. Run long enough to catch occasional
   spikes; a ten-minute session is a useful first check, not a complete proof.

   Add an inexpensive audio-core peak/clipped-sample counter if existing
   diagnostics cannot distinguish DSP overload from hitting the PCM clamp.
   Publish summaries to Core 0 through the existing bounded handoff; never
   print from the audio loop. Inspect heap headroom after constructing voices,
   delay and audio buffers, rather than treating the compiler's global-RAM
   figure as remaining runtime memory.

   If timing is tight, identify the expensive path first. Optimize or cache
   only that work. Preserve delay history at wet mix zero unless a tested
   bypass policy is deliberately chosen. Do not assume a higher CPU clock
   fixes the underlying issue or is stable on the physical unit.

   **Finished when:** the chosen clock has measured margin below the buffer
   deadline, underruns do not increase in the tested scenarios, and output
   clipping is understood separately from timing failures.

5. **Save the master sound with the session.**

   Add delay mix, time, feedback and compressor macro to a versioned extension
   of the current snapshot format. First write portable codec and migration
   tests. Old sessions should load with explicit defaults: dry delay, 300 ms,
   85% feedback and the 50% compressor macro. Reject invalid/non-finite values
   and clamp or reject out-of-range values according to a documented policy.

   Then wire capture and restore through the existing atomic control targets.
   Do not copy delay-buffer contents or compressor detector history into a
   session. Reuse the existing validated file-write/rename flow and account
   for retained-RAM layout changes. Test save/load at effect extremes and
   failed/truncated loads before trying power-cycle restoration on the board.

   **Finished when:** old sessions still load, new sessions round-trip all
   master controls, failed loads preserve a usable session, and restored
   settings match what the user hears after playback resumes.

6. **Reduce duplication and recover memory only after behavior is stable.**

   Introduce a small shared master-control definition containing the few
   actual controls, their units, defaults and limits. Keep the control-to-audio
   boundary explicit. Use the same definitions for fader scaling and displayed
   targets; retain separate audio-owned smoothing state. Avoid a general
   parameter framework unless several concrete uses justify it.

   Separately decouple maximum delay samples from buffer capacity. If measured
   memory headroom warrants shrinking the buffer, retain the full 750 ms range
   and enough neighboring samples for cubic interpolation. Test impulse timing,
   full buffer wrap, the longest delay, pitch glides and 100% feedback before
   comparing the sound. Make this a different change from the control cleanup
   so a regression has a clear origin.

   **Finished when:** advertised limits agree with DSP, source remains easy
   to modify, and any measured memory saving preserves timing and sound.

7. **Finish with a release procedure that can be repeated.**

   Make the chosen clock explicit in the documented build command. Record
   parent/submodule revisions, dependency versions, firmware hashes, test
   results and hardware observations together. Reconcile obsolete documentation
   with current code, including PPQN ownership and compressor-versus-clamp
   terminology. Keep one concise manual table for the physical controls.

   After software and hardware checks, package the verified artifact and the
   accepted changes for the next main-branch update. Commit and push only as
   part of the agreed release work. The release note should state what changed,
   which clock was tested, which checks passed and any remaining limitations.

   **Finished when:** another checkout can reproduce the build, and someone
   picking up the instrument can find its controls and trust its save/restore
   behavior without knowing this conversation.

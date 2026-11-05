# REAPER routing and installation

This setup uses a single stereo track. The plugin's channel mappings are set in
REAPER's plug-in pin connector, not hard-coded in the VST. Example assignments:

| Signal | Default track pair | Alternative example |
| --- | --- | --- |
| Main processed chain | 1/2 | 1/2 |
| Original reference | 3/4 | 5/6 |
| Matcher output (what you hear) | 1/2 | 1/2 |

## Install

1. Unzip the macOS artifact. Copy `LUFS Compensation.vst3` to
   `~/Library/Audio/Plug-Ins/VST3/` and rescan VST3 plugins in REAPER.
2. In REAPER choose **Options > Show REAPER resource path in Finder**. Copy
   `LUFS-Reference.jsfx` into that resource folder's `Effects` directory, optionally
   in a `Yodo Audio` subfolder. Refresh the FX browser. The helper appears as
   **JS: LUFS Compensation - Original reference copy**.
3. Use a stereo audio track and set its channel count to at least four in its
   routing controls. For a 5/6 reference route, set it to at least six.

## Default 1/2 main, 3/4 reference

1. Put the **JS reference copy first**, before every effect to be compared. If an
   instrument produces the track audio, place the copy immediately after the
   instrument and before the audio effects.
2. Open the helper's pin connector. Assign input Left/Right to track 1/2; assign
   output Left/Right to 1/2 and Original left/right to 3/4. Use one-to-one mappings,
   removing any duplicate mappings that would sum unwanted signals.
3. Put the effects to compare after the helper. Route their main inputs/outputs
   through 1/2. Keep them from writing into 3/4, and do not use 3/4 for a different
   sidechain in this chain. The reference pair must pass through unchanged.
4. Put **VST3: LUFS Compensation** last. In its input pins map **Processed** L/R to
   1/2 and **Original reference** L/R to 3/4. Map the plugin's output L/R to 1/2 only.
   Bus labels can be displayed differently across REAPER versions; it exposes two
   stereo input buses (four input pins) and one stereo output bus (two output pins).
5. Keep the track's audible parent/master send on 1/2 only. Do not also sum the
   reference pair into the audible output. Keep REAPER's FX wet control at 100%.
6. Start playback. The reference signal indicator should show activity. Click
   **Learn Match**, play a representative section, then let it finish or use
   **Finish Learning** after at least 3 seconds of accepted reference audio.
7. Compare with **A: Original / B: Processed**. Leave B selected for normal use.
   Match ON/OFF compares compensated and uncompensated processed audio.

The helper copies samples only; it adds no processing or delay to 1/2. The VST
also adds no fixed latency. Neither tool touches the intervening plugins' DSP.

## Choose other channels

For reference on 5/6, change the helper's **Original left/right output pins** to
track channels 5/6 and map the VST's **Original reference input pins** to 5/6.
Reserve that pair through every intervening effect. No code change or second VST
instance is needed. The helper's original output pin names describe roles; pin
numbers are local to the helper, not fixed track channel numbers.

You can independently map the helper's source inputs, its dry-through outputs,
the matcher's Processed inputs, and the matcher's audible output to other track
pairs. Ensure the pair selected at the start is the signal whose loudness you
want to preserve. Matcher's A/B follows its two named input buses. The channel
assignments are saved by REAPER with the project; gain and UI size are saved by
the plugin. The VST cannot inspect REAPER's external pin assignments, so its
signal indicator verifies activity, not that the chosen source is correct.

## Timing, failures, and scope

- This measures playback, not a file scan. It does not automatically scan a whole
  track, seek transport, or insert routing. Replay the same passage when relearning.
- Use a representative passage; the 15-second accepted-audio measurement pauses
  on reference windows below the LUFS gate. A very quiet or missing reference can
  leave learning waiting. Reset/stop learning and fix routing rather than waiting
  for the plugin to invent a reference.
- If Original is silent, check the helper, track channel count, bus enablement,
  and pin mappings. An enabled but silent reference is silent in Original mode;
  the UI reports no reference signal. Host bypass gives the uncompensated chain.
- If the sidechain bus is disabled, learning is unavailable and Original falls
  back to the processed path. An existing fixed match remains usable.
- A chain that is fully muted cannot be matched; the previous learned adjustment
  is retained on failure. Gain is capped at ±24 dB, and there is no limiter.
- The plugin does not know the upstream chain's delay. REAPER handles plugin delay
  compensation, but you must verify the routed reference and processed audio
  correspond to the same passage, particularly with look-ahead/latent effects or
  separately routed tracks. If they do not align, fix timing in REAPER before
  learning or interpreting A/B. This release does not auto-align paths.
- These steps are the intended REAPER workflow. Linux automated tests validate
  plugin buses and processing; actual macOS REAPER routing and the JSFX helper
  still need a host smoke test.

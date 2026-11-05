# Attribution and provenance

- `Source/LoudnessMatch.h` and its independent tests are adapted from the
  MIT-licensed Thunder/Wide/ButterComp family of wrappers. Thunder reference:
  https://github.com/johandoornenbal/thunder, commit
  `c19ff8d` (learned LUFS matching and interface sizes). The wrapper/UI license
  notices are retained in LICENSE. No Airwindows audio engine is included.
- The BS.1770 K-weighting design follows libebur128 by Jan Kokemueller (MIT),
  pinned at `67b33abe1558160ed76ada1322329b0e9e058b02` from
  https://github.com/jiixyj/libebur128. Original C sources and license are under
  `third_party/libebur128`, copied from the reference repositories and linked only
  into tests. Queue compatibility code retains its embedded BSD notices.
- JUCE 8.0.12 is pinned to `29396c22c93392d6738e021b83196283d6e4d850`,
  https://github.com/juce-framework/JUCE. JUCE is separately licensed under AGPLv3
  or commercial terms. The wrapper's MIT license does not relicense JUCE. For
  distribution, comply with JUCE's AGPLv3 terms (including corresponding source)
  or obtain an appropriate commercial license. Retain the bundled VST3 SDK notices.
- `scripts/LUFS-Reference.jsfx` is an original MIT-licensed REAPER utility. It
  copies the main stereo samples to reference output pins without modifying
  the main samples. REAPER is a separate host and is not distributed here.

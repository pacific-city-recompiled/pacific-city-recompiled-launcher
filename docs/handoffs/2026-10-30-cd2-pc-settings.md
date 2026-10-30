# 2026-10-30: Crackdown 2 PC settings in the launcher

Branch `feat/cd2-pc-settings`, PR https://github.com/pacific-city-recompiled/pacific-city-recompiled-launcher/pull/2

## What changed (`main.cpp`)

- Aspect ratio row (Display) and Camera turn speed slider (Gameplay) are no longer CD1-only. Same keys as CD1:
  `aspect_ratio`, `camera_sensitivity` (0.25-4.0, default 1.0, removed from the toml at default).
- CD2 frame-rate combo: 60 (default) / 30 (stock) / 120 / 144 (experimental). Writes `fps_cap` only; `vsync` and
  `sim_game_clock` are cleared to their defaults, as before. The settings read maps `fps_cap` 30/120/144 back.

## Checked

- Combined build (`out-combined`, x64 vcvars + LLVM, ninja -j4), ctest 7/7.
- `CrackdownLauncher --review-ui <dir> 1280 720 --game crackdown2` (hidden window) with a test toml
  (fps_cap 144, 21:9, 2.0x): rows show those values; toml untouched. CD1 run (vsync, 32:9): unchanged.

## Next

1. Merge after crackdown-2-recomp PRs #2-#4 (the game must know the flags first; older CD2 exes would warn on
   unknown keys at most, not tested).
2. A play test from the launcher (pick 21:9 / 2.00x / 144, press Play, confirm the game log shows the values).
3. No release published. Ship with the next CD2 release + launcher version bump.

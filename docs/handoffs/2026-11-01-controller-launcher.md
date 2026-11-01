# Handoff 2026-11-01: controller-friendly launchers (for Quiver)

**Branch:** `claude/project-thread-h50s3z` (launcher repo). Quiver installs `Crackdown-Windows-<ver>.zip` /
`Crackdown2-Windows-<ver>.zip` and starts the single-game launcher at the top of the zip, so this reaches Quiver
users with the next CD1 and CD2 release that bundles this launcher.

## What changed (main.cpp, visuals.*)
- Left/right value pickers replace the settings dropdowns (A or a click steps forward).
- On/off switches and pickers can be reached with the D-pad. Before, they were skipped: since ImGui 1.91
  `InvisibleButton` needs `ImGuiButtonFlags_EnableNav`.
- A on a text box opens the on-screen keyboard (ImGui only starts text entry from Y). The co-op address boxes get
  it too. In the keyboard, X deletes, Y types a space (key-owned so it doesn't also press the focused key) and
  Start finishes.
- Home page: a save profile button next to PLAY (Y), Start plays, X opens Settings. Footer hints follow the page.
- B on home opens Quit again (the same press used to close it in the same frame).
- LT + RT or F11 switches fullscreen. Gameplay > Advanced has "Open the launcher fullscreen" (`launcher_fullscreen`)
  and "Start the game right away" (`quick_start`: 3 s countdown, any button stops it). `--fullscreen` arg.
- The launcher hides while the game runs, then shows again and takes focus when the game exits. Pad input is read
  in the background (SDL hint), but only while the launcher owns it: lost when another app takes focus, regained
  when the launcher is focused again or its game exits. While the game runs, gamepad nav is off.
- `--review-ui <dir> W H --pad-script <file>`: drives the hidden launcher with a virtual SDL gamepad, saving
  `shot <name>` frames. Used for the tests below.

## Tested
- Linux (offscreen): pad scripts covered every page, the pickers, the keyboard, profile creation from the pad, and
  the quit dialog. The old smoke review still writes its frames. Unit tests pass. cd1, cd2 and combined builds compile.
- Windows (Austin's PC, hidden window): cd1 and cd2 Release builds, both pad scripts exit 0 with all frames.
  Exes: `out\launcher-pad\build-cd1\CrackdownRecomp.exe`, `out\launcher-pad\build-cd2\Crackdown2Recomp.exe`.

## Still needs a person with a controller
Hiding while the game runs, coming back with focus when it closes, the quick-start countdown, LT+RT, and how the
pickers feel. None of this could run unattended without starting a game window on the PC Austin was using.

## Next
Merge, then ship it in the next CD1 and CD2 releases (CD2 1.0.1 may still be waiting to be published; it would need
a rebuild to include this). Austin runs the publish scripts.

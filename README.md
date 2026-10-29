# Pacific City Recompiled launcher

The launcher for the [Crackdown](https://gitlab.com/pacific-city-recompiled/crackdown-recomp) and
[Crackdown 2](https://gitlab.com/pacific-city-recompiled/crackdown-2-recomp) PC recompilations: game file install
(disc image or extracted folder, title update, DLC, all checked file by file), settings, save profiles,
Archipelago and co-op. It ships no game data; players supply their own dump.

## Builds

| CMake option | Output | Used by |
|---|---|---|
| `-DLAUNCHER_GAME=` (default) | `CrackdownLauncher.exe` | the combined launcher: both games, updates itself and each game from GitLab releases |
| `-DLAUNCHER_GAME=cd1` | `CrackdownRecomp.exe` | Crackdown's `Crackdown-Windows-<ver>.zip` |
| `-DLAUNCHER_GAME=cd2` | `Crackdown2Recomp.exe` | Crackdown 2's `Crackdown2-Windows-<ver>.zip` |

The single-game builds sit at the top of that game's release package, the one app launchers such as
[Quiver](https://github.com/tgeorgiadis/quiver-launcher) install. Quiver starts the only exe at the top of the
install folder and updates the package itself, so these builds have no game selector and no update checks.
The game goes in a subfolder next to the launcher (`cd1\` or `cd2\`), so the launcher is the only exe at the top.
The combined launcher keeps Crackdown 1 at the top of its folder and Crackdown 2 in `cd2\`.

Dependencies (SDL3, Dear ImGui, toml++, stb, SHA-256/SHA-1) come from either game's ReXGlue SDK checkout:

```
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
      -DSDK_THIRDPARTY=<crackdown-2-recomp>/sdk/thirdparty -DLAUNCHER_GAME=cd2
cmake --build out --target CrackdownLauncher
```

On Windows run it from the x64 Visual Studio environment (`vcvars64.bat`) with LLVM on `PATH`.
`-DBUILD_TESTING=ON` adds the unit tests (`ctest --test-dir out`).

Headless commands for unattended tests: `--install-iso <iso> <game home>`, `--install-update <package> <game home>`,
`--install-dlc <package> <save folder>`, `--verify-folder <folder> <manifest> [--full]`, and `--play` (press PLAY
once the files are verified).

History before the move is from crackdown-recomp's `crackdown/launcher` folder (kept with `git subtree split`).

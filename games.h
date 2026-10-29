#pragma once
// The games this launcher manages. Crackdown 1 keeps the package root as its folder (existing installs need no
// migration), except in its single-game build, where it lives in cd1/ so that the launcher is the only exe at the
// top. Crackdown 2 lives in cd2/ with its own exe, SDK DLLs, settings, manifest and game files, because its
// runtime DLLs differ from Crackdown 1's (separate SDK patch set) and share their names.
#include "core.h"
#include "v2.h"
#include <string>

namespace launcher {
struct GameProfile {
  const char* id;           // launcher.toml selected_game value
  const char* title;        // header button, logo fallback, About
  const char* folder;       // package subfolder ("" = package root)
  const char* exe;          // in that folder
  const char* config;       // the game's settings file, next to its exe
  const char* manifest;     // disc file list (sizes + SHA256), next to its exe
  const char* user_folder;  // Documents\<name>: the SDK's default user data folder for that exe
  const char* log_prefix;   // <folder>/logs/<prefix>NNN.log
  const char* xex_hash;     // SHA256 of the disc's default.xex
  const char* profile_key;  // launcher.toml key for the selected save profile
  const char* disc_hint;
  bool coop, archipelago, title_update, steam;
  const UpdateSource* updates;  // its GitLab releases (Crackdown 2 installs its runtime into its folder from there)
  uint32_t title_id;            // Xbox title id of its content packages (title update, DLC)
  const char* update_xexp;      // title_update: SHA256 of the supported update's default.xexp
  bool dlc;                     // the launcher installs DLC packs (and passes --dlc_root)
  // Optional title update with its own exe built from it (Crackdown 1: crackdown_tu.exe for Title Update #6, which the
  // Free-For-All Pack needs). The update stays in tu/ (--update_data_root; the exe applies tu/default.xexp itself),
  // never next to default.xex, which the disc exe would then try to apply. nullptr: no such exe.
  const char* update_exe;
};

// Single-game build (CMake LAUNCHER_GAME=cd1|cd2): the launcher an app launcher such as Quiver opens, at the top of
// that game's release package. Quiver installs and updates the package, so this build has no game selector, no
// update checks and no runtime download. -1: the combined launcher.
#ifdef LAUNCHER_SINGLE_GAME
inline constexpr int kSingleGame = LAUNCHER_SINGLE_GAME;
#else
inline constexpr int kSingleGame = -1;
#endif
// Crackdown 2: World multi-language disc (XGD2), default.xex sha256 below. It runs with title update 0.0.5.1
// applied: default.xexp next to default.xex and the update's files in tu/ (passed as --update_data_root); the
// launcher installs both from the update package (content.cpp). See the gitlab.com/pacific-city-recompiled/crackdown-2-recomp README.
inline constexpr GameProfile kGames[] = {
    {"cd1", "CRACKDOWN", kSingleGame == 0 ? "cd1" : "", "crackdown.exe", "crackdown.toml", "game-manifest.txt",
     "crackdown", "crackdown_", supported_xex, "profile", "Select your supported World multi-language disc image or an extracted game folder.",
     true, true, false, true, &cd1_updates, 0x4D5307DC,
     "a0d4fe06d6a4a2fd96d18f5de426f761fe1b8cce5d5bc7f981d738c3519c819e", true, "crackdown_tu.exe"},
    {"cd2", "CRACKDOWN 2", "cd2", "crackdown2.exe", "crackdown2.toml", "game-manifest.txt", "crackdown2",
     "crackdown2_", "8959c449e382661d29b8ea479c685f506cd5bd3eca40b3d73a51cc10f281069f", "profile_cd2",
     "Select your Crackdown 2 World multi-language disc image or an extracted game folder. The title update "
     "(0.0.5.1) is installed next, from its package.",
     false, true, true, false, &cd2_updates, 0x4D5308BC,
     "a5e49fc1ff7f6f65e6f95a7b5c4b9392bb9e38674d1237ac0e2f0149d05929d7", true, nullptr},
};
inline constexpr int kGameCount = int(sizeof(kGames) / sizeof(kGames[0]));
static_assert(kSingleGame < kGameCount);
// Accepts the profile id or the game's name as the Archipelago clients pass it (--game crackdown2).
inline int game_index(const std::string& id) {
  for (int i = 0; i < kGameCount; ++i)
    if (id == kGames[i].id) return i;
  if (id == "crackdown2") return 1;
  return 0;
}
inline fs::path game_home(const fs::path& package, const GameProfile& g) {
  return *g.folder ? package / g.folder : package;
}
}  // namespace launcher

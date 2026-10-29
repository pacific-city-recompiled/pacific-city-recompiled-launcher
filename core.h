#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "common/toml_edit.h"

namespace launcher {
namespace fs = std::filesystem;
inline constexpr char supported_xex[] = "7aaa7a8920d1c3c737e58cf583debafe6013be0a96bec3ec136e5de7e3377e5a";
struct Entry { std::string path; uint64_t offset, size; std::string hash; };
using Progress = std::function<void(uint64_t, uint64_t)>;
std::string utf8(const fs::path& path);
std::string hash_file(const fs::path& path, const std::atomic_bool* cancel = nullptr,
                      const std::function<void(uint64_t)>& hashed = {});
bool safe_name(const std::string& name);
std::vector<Entry> load_manifest(const fs::path& path);
// Quick: every size, the XEX and files up to quick_hash_limit (startup, Play).
// Full: every hash; run once after an import or folder selection, and on request.
enum class Check { quick, full };
inline constexpr uint64_t quick_hash_limit = 1024 * 1024;
std::string verify_folder(const fs::path& folder, const std::vector<Entry>& manifest,
                          const std::atomic_bool* cancel = nullptr, Check check = Check::quick,
                          Progress progress = {}, const std::string& expected_xex = supported_xex);
// verify_folder, but a quick check is skipped when `stamp` (written after the last
// successful check of the same folder) still matches every file's size and modification
// time. Any change, or a full check, hashes again and rewrites the stamp. Guards against
// damaged or replaced files, not deliberate tampering.
std::string verify_folder_cached(const fs::path& folder, const std::vector<Entry>& manifest,
                                 const fs::path& stamp, const std::atomic_bool* cancel = nullptr,
                                 Check check = Check::quick, Progress progress = {},
                                 const std::string& expected_xex = supported_xex);
class Image {
 public:
  explicit Image(const fs::path& path);
  const std::vector<Entry>& files() const { return files_; }
  void validate(const std::vector<Entry>& manifest) const;
  std::string xex_hash(const std::atomic_bool* cancel = nullptr) const;
  void extract(const fs::path& destination, const std::atomic_bool& cancel, Progress progress) const;
 private:
  fs::path path_;
  uint64_t length_ = 0, partition_ = 0;
  std::vector<Entry> files_;
};
// Owns only a newly-created staging directory; existing game folders are never deleted.
// The staging copy gets a full hash check before it becomes game/.
// home is the game's own package folder (the package root for Crackdown 1, cd2/ for Crackdown 2).
void install_image(const fs::path& iso, const fs::path& home, const std::vector<Entry>& manifest,
                   const std::atomic_bool& cancel, Progress extracted, Progress verified = {},
                   const std::string& expected_xex = supported_xex,
                   const std::string& manifest_name = "game-manifest.txt");
// Only marked, inactive staging folders; selected game data is always protected.
void mark_import(const fs::path& folder);
std::vector<fs::path> unfinished_imports(const fs::path& home, const fs::path& selected = {});
// Save profiles. The default profile ("") is the SDK's user folder as it is, so existing saves stay
// where they are. A named profile is <user>/profiles/<name>: its own saves and achievements, with the
// shared shader cache in <user>/cache so a new profile doesn't recompile shaders.
bool valid_profile_name(const std::string& name);
std::vector<std::string> list_profiles(const fs::path& user);
void create_profile(const fs::path& user, const std::string& name);
// The profile's save folder (the game's user_data_root); throws if it is missing or a link.
fs::path profile_folder(const fs::path& user, const std::string& name);
// Always absolute save and shared cache arguments, including the default profile.
std::vector<std::string> profile_args(const fs::path& user, const std::string& name);
// Crackdown 1 saves (<saves>/<profile id>/4D5307DC/00000001/Crackdown.sav/Crackdown.sav) start with
// "CRAK" and a big-endian version: 4 from the disc build, 6 from the title update build. The disc
// loader sub_8254CAD8 accepts only 4 ("Save Game Damaged", and Yes overwrites the save); the update's
// loader takes versions 1-28 (sub_825B7598 in the update), so it reads disc saves. Returns the saves only
// the title update can read.
std::vector<fs::path> title_update_saves(const fs::path& saves);
// Archipelago connection for one profile, in <profile>/archipelago/connection.toml beside the
// game's session sidecar. Never in crackdown.toml, so "Copy system info" can't include the password;
// the game reads the password from this file (not argv/environment/cvars, which can reach logs).
struct ApConnection { bool enabled=false; std::string server, slot, password; };
ApConnection load_ap_connection(const fs::path& saves);
void save_ap_connection(const fs::path& saves, const ApConnection& connection);
// --ap_server/--ap_slot (validated), or nothing when disabled. Never an empty value (the SDK
// drops the whole command line on one) and never the password.
std::vector<std::string> ap_args(const ApConnection& connection);
// crackdown.toml keys the launcher removes before Play, so only the profile decides AP.
inline constexpr const char* kApConfigKeys[]={"ap_server","ap_slot","ap_password"};
// archipelago://[slot[:password]@]host[:port] from a room page (via the Archipelago Launcher's
// "Crackdown Client"). Percent-decodes slot and password; port defaults to 38281 and must be 1-65535.
// Enabled on success. Malformed links (unencoded '@' after the host, control characters) are refused.
std::optional<ApConnection> parse_ap_uri(const std::string& uri);
// Settings for a bug report: parsed and re-serialized with every key containing "password"
// (any case, any table) replaced; an unparsable file is left out entirely.
std::string redacted_config(const fs::path& config);
// "seed S, slot N" when the game has bound this profile to a seed, else empty.
std::string ap_binding(const fs::path& saves);
// System Link co-op (SETUP.md "Co-op"), written to crackdown.toml. Without net_mode the game
// greys out Campaign > Play Now - Co-op (the HDD check, hooks_storage.cpp). Player 1 hosts;
// player 2 joins with a second profile identity (save folder B100000000000002), since both games
// would otherwise sign in as the same player.
struct CoopSettings { bool enabled=false, join=false; std::string own_address, partner_address; };
inline constexpr const char* kCoopConfigKeys[]={"net_mode","net_bind_ip","net_peer_endpoints","coop_profile_xuid","coop_profile_name"};
CoopSettings load_coop(const toml::table& config);
// Every co-op key removed when disabled. Throws when an address isn't a plain IPv4 address or
// both are the same.
std::map<std::string, std::optional<std::string>> coop_changes(const CoopSettings& coop);
// This PC's IPv4 addresses (home network, Tailscale), loopback left out.
std::vector<std::string> local_ipv4_addresses();
// Keep the launcher API while using the same editor in the game.
using crackdown::config::read_config;
using crackdown::config::setting;
using crackdown::config::Changes;
using crackdown::config::update_config;
using crackdown::config::toml_string;
}

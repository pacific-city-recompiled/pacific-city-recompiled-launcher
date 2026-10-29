#pragma once
#include "core.h"
#include <span>

namespace launcher {
// Where one game's runtime files come from. Each game updates from its own GitLab project's releases into its own
// folder; only Crackdown 1's package carries the launcher (CrackdownLauncher.exe), so only its updates restart it.
// A 1.0 launcher knows only the Crackdown 1 source, so that package must keep to the files 1.0 allows.
// update_host is the only server the updater talks to (every redirect hop included). It serves the releases API
// and every release asset: uploaded files, generic packages and /-/releases/<tag>/downloads/ links.
inline constexpr char update_host[] = "gitlab.com";
struct UpdateSource {
  const char* project;      // group/name on update_host
  const char* project_id;   // numeric project id (uploaded files are linked as /-/project/<id>/uploads/...)
  const char* zip_prefix;   // release asset <prefix>*.zip + .sha256, one package directory of the same prefix
  const char* folder;       // install folder under the package ("" = package root)
  const char* version_file; // written after a launcher install: the installed release tag
};
inline constexpr UpdateSource cd1_updates{"pacific-city-recompiled/crackdown-recomp", "88052940", "CrackdownRecomp-", "", nullptr};
inline constexpr UpdateSource cd2_updates{"pacific-city-recompiled/crackdown-2-recomp", "88052946", "Crackdown2Recomp-", "cd2", "release.txt"};
// https://<update_host>/<project>, its releases page, and whether a release asset URL belongs to the project.
std::string project_url(const UpdateSource& source);
std::string releases_url(const UpdateSource& source);
bool asset_url_allowed(const std::string& url, const UpdateSource& source);
using Bytes = std::vector<unsigned char>;
struct XzpEntry { std::string name; Bytes data; };
std::vector<XzpEntry> read_xzp(std::span<const unsigned char> bytes);
const Bytes* xzp_entry(const std::vector<XzpEntry>& entries, const std::string& name);
Bytes read_bytes(const fs::path& path, uint64_t limit = 128 * 1024 * 1024);
void write_bytes(const fs::path& path, std::span<const unsigned char> bytes);

// Binary KeyValues are preserved, including unrelated Steam shortcut fields.
struct VdfNode { unsigned char type=0; std::string key, value; std::vector<VdfNode> children; };
std::vector<VdfNode> read_vdf(std::span<const unsigned char> bytes);
Bytes write_vdf(const std::vector<VdfNode>& nodes);
bool append_steam_shortcut(std::vector<VdfNode>& nodes, const fs::path& executable);
std::string add_to_steam(const fs::path& executable);

struct Release { std::string tag, notes, url, zip_url, sha_url; bool prerelease=false; };
// GitLab releases API (GET /api/v4/projects/:id/releases). GitLab has no prerelease flag: a SemVer prerelease
// tag (v0.5.0-beta) is a prerelease. Packages are the release's asset links (direct_asset_url, else url).
std::vector<Release> parse_releases(const std::string& json, const UpdateSource& source = cd1_updates);
// Strict SemVer, optional leading v. Build metadata does not affect ordering.
int compare_versions(const std::string& a, const std::string& b);
void verify_download(const fs::path& zip, const std::string& checksum);
bool update_path_allowed(const std::string& relative, const UpdateSource& source = cd1_updates);
// Complete package allowlist, regular files only. Roll back on errors; preserve user data.
// `package` is the source's install folder (the launcher folder for Crackdown 1, cd2/ for Crackdown 2).
void replace_staged(const fs::path& package, const fs::path& staging,
                    const std::function<void(size_t)>& before_copy = {}, const UpdateSource& source = cd1_updates);
void recover_update(const fs::path& package, const UpdateSource& source = cd1_updates);
void rollback_completed_update(const fs::path& package, const UpdateSource& source = cd1_updates);
// The installed release of a source without the launcher (Crackdown 2): the tag the launcher wrote after
// installing, else the version in the package's VERSION.txt ("Crackdown 2 Recomp beta 0.5.0" -> v0.5.0-beta, "1.3.0" -> v1.3.0).
// Empty when nothing is installed or the version is unknown.
std::string installed_version(const fs::path& folder, const UpdateSource& source);
// WinHTTP only; explicit caller consent is required. POSIX throws without networking.
std::vector<Release> check_releases(bool beta, const std::atomic_bool& cancel, const UpdateSource& source = cd1_updates);
// Installs into package/source.folder. For a source without the launcher the update is final when this returns.
void install_release(const Release& release, const fs::path& package, const std::atomic_bool& cancel,
                     const UpdateSource& source = cd1_updates);
void restart_launcher(const fs::path& package);
}

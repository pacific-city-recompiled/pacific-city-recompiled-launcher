#include "games.h"
#include "v2.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace launcher;
namespace {
int checks=0;
void require(bool ok,const char* s) { ++checks; if(!ok) throw std::runtime_error(s); }
template<class F> void rejects(F f,const char* s) { bool thrown=false; try{f();}catch(const std::exception&){thrown=true;} require(thrown,s); }
void be(Bytes& b,size_t p,uint32_t v,size_t n) { for(size_t i=0;i<n;++i) b.at(p+i)=static_cast<unsigned char>(v>>((n-i-1)*8)); }
Bytes fixture() {
  Bytes b(22+11+13+5); std::copy_n("XUIZ",4,b.begin()); be(b,4,1,4); be(b,8,static_cast<uint32_t>(b.size()),4); be(b,16,24,4); be(b,20,2,2);
  be(b,22,3,4); b[30]=1; b[32]='a'; be(b,33,2,4); be(b,37,3,4); b[41]=2; b[43]='b'; b[45]='c'; std::copy_n("hello",5,b.begin()+46); return b;
}
void write(const fs::path& p,const std::string& s) { fs::create_directories(p.parent_path()); write_bytes(p,Bytes(s.begin(),s.end())); }
std::string text(const fs::path& p) { auto b=read_bytes(p); return {b.begin(),b.end()}; }
void xzp() {
  auto b=fixture(); auto entries=read_xzp(b); require(entries.size()==2,"two XZP entries"); require(entries[0].name=="a" && entries[0].data==Bytes({'h','e','l'}),"XZP payload");
  require(*xzp_entry(entries,"bc")==Bytes({'l','o'}),"XZP lookup"); require(!xzp_entry(entries,"missing"),"missing art fallback");
  for(size_t i=0;i<b.size();++i) rejects([&]{read_xzp(std::span(b).first(i));},"truncation accepted");
  for(auto [offset,n]:std::vector<std::pair<size_t,size_t>>{{0,1},{4,4},{8,4},{12,4},{16,4},{20,2},{22,4},{26,4},{37,4}}) { auto broken=b; be(broken,offset,999,n); rejects([&]{read_xzp(broken);},"invalid XZP field"); }
  auto unicode=b; be(unicode,42,0xd83d,2); be(unicode,44,0xde00,2); require(read_xzp(unicode)[1].name=="\xf0\x9f\x98\x80","UTF16 surrogate pair");
  be(unicode,44,65,2); rejects([&]{read_xzp(unicode);},"invalid surrogate accepted");
  entries.push_back(entries[0]); rejects([&]{xzp_entry(entries,"a");},"ambiguous art accepted");
}
void vdf(const fs::path& home) {
  std::vector<VdfNode> nodes={{0,"shortcuts",{},{{0,"0",{},{{1,"AppName","Other game",{}},{1,"Exe","\"C:\\other.exe\"",{}},{2,"AllowOverlay",std::string("\1\0\0\0",4),{}},{0,"tags",{},{{1,"0","Keep tag",{}}}}}}}}};
  auto before=write_vdf(nodes); require(write_vdf(read_vdf(before))==before,"VDF round trip");
  require(append_steam_shortcut(nodes,home/"CrackdownLauncher.exe"),"Steam append"); auto result=write_vdf(nodes); auto parsed=read_vdf(result);
  require(parsed[0].children.size()==2,"preserved existing shortcut"); require(write_vdf({parsed[0].children[0]})==write_vdf({read_vdf(before)[0].children[0]}),"preserve all existing fields");
  require(!append_steam_shortcut(parsed,home/"CrackdownLauncher.exe"),"duplicate Steam shortcut"); require(write_vdf(parsed)==result,"duplicate unchanged");
  auto empty=read_vdf({}); require(append_steam_shortcut(empty,home/fs::path(u8"\u00e9/CrackdownLauncher.exe")),"new unicode shortcut"); require(write_vdf(read_vdf(write_vdf(empty)))==write_vdf(empty),"Unicode roundtrip");
  for(size_t i=1;i<result.size();++i) rejects([&]{read_vdf(std::span(result).first(i));},"truncated VDF accepted");
  auto invalid=result; invalid[0]=99; rejects([&]{read_vdf(invalid);},"unknown VDF type accepted");
}
void versions() {
  require(compare_versions("v1.2.3","1.2.3+pc")==0,"SemVer metadata"); require(compare_versions("1.10.0","1.9.9")>0,"numeric SemVer");
  const std::vector<std::string> sequence={"1.0.0-alpha","1.0.0-alpha.1","1.0.0-alpha.beta","1.0.0-beta","1.0.0-beta.2","1.0.0-beta.11","1.0.0-rc.1","1.0.0"};
  for(size_t i=1;i<sequence.size();++i) require(compare_versions(sequence[i-1],sequence[i])<0,"SemVer prerelease order");
  for(const auto& bad:{"","beta1","1.2","01.2.3","1.2.3-01","1.2.3-","1.2.3+","1.2.3\n","18446744073709551616.0.0"}) rejects([&]{compare_versions(bad,"1.0.0");},"invalid version accepted");
  // GitLab releases API shape: a link with a direct_asset_url, one with only url, source archives, an upcoming release.
  auto releases=parse_releases(R"([{"name":"1.0","tag_name":"v1.0.0","description":"Notes\n\uD83D\uDE00","upcoming_release":false,"_links":{"self":"https://gitlab.com/pacific-city-recompiled/crackdown-recomp/-/releases/v1.0.0"},"assets":{"count":3,"sources":[{"format":"zip","url":"https://gitlab.com/x.zip"}],"links":[{"id":1,"name":"CrackdownRecomp-beta1.zip","url":"https://gitlab.com/-/project/88052940/uploads/0123456789abcdef0123456789abcdef/CrackdownRecomp-beta1.zip","direct_asset_url":"https://gitlab.com/pacific-city-recompiled/crackdown-recomp/-/releases/v1.0.0/downloads/CrackdownRecomp-beta1.zip","link_type":"package"},{"id":2,"name":"CrackdownRecomp-beta1.zip.sha256","url":"https://gitlab.com/-/project/88052940/uploads/0123456789abcdef0123456789abcdef/CrackdownRecomp-beta1.zip.sha256","link_type":"other"}]}},{"tag_name":"v1.1.0-beta.1","assets":{"count":0,"links":[]}},{"tag_name":"v9.0.0","upcoming_release":true}])");
  require(releases.size()==2&&releases[0].prerelease&&!releases[1].prerelease,"release history sort, prerelease from tag, upcoming skipped");
  require(releases[1].zip_url.ends_with("/-/releases/v1.0.0/downloads/CrackdownRecomp-beta1.zip")&&releases[1].sha_url.ends_with("/uploads/0123456789abcdef0123456789abcdef/CrackdownRecomp-beta1.zip.sha256"),"paired assets, direct_asset_url preferred");
  require(releases[1].notes=="Notes\n\xf0\x9f\x98\x80"&&releases[1].url.ends_with("/-/releases/v1.0.0"),"release notes escapes and page");
  require(asset_url_allowed(releases[1].zip_url,cd1_updates)&&asset_url_allowed(releases[1].sha_url,cd1_updates),"project asset URLs");
  for(const auto& url:{"https://gitlab.com/api/v4/projects/88052940/packages/generic/cd/1.0.0/a.zip","https://gitlab.com/api/v4/projects/pacific-city-recompiled%2Fcrackdown-recomp/packages/generic/cd/1.0.0/a.zip","https://gitlab.com/pacific-city-recompiled/crackdown-recomp/uploads/0123/a.zip"})
    require(asset_url_allowed(url,cd1_updates),"GitLab package and upload URLs");
  for(const auto& url:{"https://github.com/Unowa001/crackdown-recomp/releases/download/v1.0.0/a.zip","http://gitlab.com/pacific-city-recompiled/crackdown-recomp/-/releases/v1.0.0/downloads/a.zip",
                       "https://gitlab.com/pacific-city-recompiled/crackdown-recomp-fork/-/releases/v1/downloads/a.zip","https://gitlab.com/-/project/1/uploads/0123/a.zip",
                       "https://gitlab.com/pacific-city-recompiled/crackdown-recomp/-/releases/../../other/a.zip","https://evil.test/pacific-city-recompiled/crackdown-recomp/-/releases/v1/downloads/a.zip"})
    require(!asset_url_allowed(url,cd1_updates),"foreign asset URL rejected");
  require(!asset_url_allowed(releases[1].zip_url,cd2_updates)&&releases_url(cd2_updates)=="https://gitlab.com/pacific-city-recompiled/crackdown-2-recomp/-/releases","per-game project");
  for(const auto& bad:{"{","{\"message\":\"Not Found\"}","[]trailing","{\"tag_name\":\"1.0.0\",\"id\":01}","{\"tag_name\":\"1.0.0\",\"id\":nan}","{\"tag_name\":\"1.0.0\",\"tag_name\":\"2.0.0\"}"}) rejects([&]{parse_releases(bad);},"malformed release JSON accepted");
}
void sha(const fs::path& home) {
  write(home/"archive.zip","abc"); auto p=home/"archive.zip";
  verify_download(p,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  archive.zip\n"); require(true,"SHA256 valid");
  verify_download(p,"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"); require(true,"uppercase SHA256");
  rejects([&]{verify_download(p,std::string(64,'0'));},"SHA mismatch accepted"); rejects([&]{verify_download(p,"abc");},"short checksum accepted");
  rejects([&]{verify_download(p,hash_file(p)+"BAD");},"malformed checksum accepted"); write(p,"abd"); rejects([&]{verify_download(p,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");},"tampered download accepted");
}
void staged(const fs::path& home) {
  auto package=home/"installed",stage=home/"staged"; fs::create_directories(package); fs::create_directories(stage);
  for(const auto& name:{"CrackdownLauncher.exe","crackdown.exe","game-manifest.txt"}) { write(package/name,"old"); write(stage/name,"new"); }
  write(package/"game/protected","game"); write(package/"crackdown.toml","settings"); write(package/"launcher.toml","preferences"); write(package/"saves/protected","save");
  write(stage/"assets/fonts/OFL.txt","license");
  write(stage/"xui/Scene.xpatch","scene recipe");
  rejects([&]{replace_staged(package,stage);},"incomplete XUI recipe pair rejected");
  require(text(package/"crackdown.exe")=="old","incomplete package leaves game unchanged");
  write(stage/"xui/menuHUD_3.xpatch","menu recipe");
  rejects([&]{replace_staged(package,stage,[](size_t i){if(i==2) throw std::runtime_error("Injected disk failure");});},"replacement failure injection");
  for(const auto& name:{"CrackdownLauncher.exe","crackdown.exe","game-manifest.txt"}) require(text(package/name)=="old","rollback intact");
  require(!fs::exists(package/".launcher-update-backup"),"rollback cleanup"); require(!fs::exists(package/"assets/fonts/OFL.txt"),"new file rolled back");
  replace_staged(package,stage); for(const auto& name:{"CrackdownLauncher.exe","crackdown.exe","game-manifest.txt"}) require(text(package/name)=="new","staged replacement");
  require(text(package/"xui/Scene.xpatch")=="scene recipe"&&text(package/"xui/menuHUD_3.xpatch")=="menu recipe","both native menu recipes installed");
  require(!update_path_allowed("xui/Scene.xzp")&&!update_path_allowed("xui/other.xpatch"),"unreviewed XUI payloads rejected");
  require(text(package/"CrackdownLauncher.old.exe")=="old","old running launcher retained");
  rollback_completed_update(package); require(text(package/"crackdown.exe")=="old","restart failure rolls back committed files");
  replace_staged(package,stage);
  recover_update(package); require(!fs::exists(package/"CrackdownLauncher.old.exe")&&!fs::exists(package/".launcher-update-backup"),"next start cleanup");
  require(text(package/"game/protected")=="game"&&text(package/"crackdown.toml")=="settings"&&text(package/"launcher.toml")=="preferences"&&text(package/"saves/protected")=="save","protected files untouched");
  for(const auto& name:{"../escape","game/data.png","crackdown.toml","launcher.toml","saves/save.bin","assets/fonts/unknown.ttf","LICENSES/../escape","C:/bad","assets/glyphs/ps5/CON.png"}) require(!update_path_allowed(name),"forbidden update path");
  write(stage/"game/oops","bad"); rejects([&]{replace_staged(package,stage);},"game data package rejected"); require(text(package/"crackdown.exe")=="new","invalid package unchanged"); fs::remove_all(stage/"game");
  // Simulate interruption after one write; persistent journal restores original files.
  auto backup=package/".launcher-update-backup"; fs::create_directory(backup); write(backup/"crackdown.exe","restored"); write(package/"crackdown.exe","partial");
  write(backup/"journal.toml","format=1\n[[files]]\npath='crackdown.exe'\nexisted=true\n"); recover_update(package); require(text(package/"crackdown.exe")=="restored","interrupted update recovered");
  fs::create_directory(backup); write(backup/"CrackdownLauncher.exe","launcher restored"); write(package/"CrackdownLauncher.exe","new launcher");
  write(backup/"journal.toml","format=1\n[[files]]\npath='CrackdownLauncher.exe'\nexisted=true\n"); recover_update(package); require(text(package/"CrackdownLauncher.exe")=="launcher restored","recovery renames mapped launcher before restoring");
  fs::create_directory(backup); write(backup/"unrelated","preserve"); rejects([&]{recover_update(package);},"unmarked update backup rejected"); require(text(backup/"unrelated")=="preserve","unmarked backup retained"); fs::remove_all(backup);
  std::error_code ec; fs::create_directory_symlink(home,stage/"link",ec); if(!ec) { rejects([&]{replace_staged(package,stage);},"linked stage rejected"); fs::remove(stage/"link"); }
}
// Crackdown 2 updates from its own repository into cd2/, with its own package allowlist and no launcher restart.
void cd2(const fs::path& home) {
  require(game_index("cd2")==1&&game_index("crackdown2")==1&&game_index("cd1")==0&&game_index("other")==0,"game ids and Archipelago client names");
  require(kGames[1].archipelago&&kGames[1].dlc&&!kGames[1].coop,"Crackdown 2 features");
  auto json=R"([{"tag_name":"v0.5.0-beta","assets":{"links":[{"name":"Crackdown2Recomp-beta0.5.0.zip","url":"https://gitlab.com/-/project/88052946/uploads/00/Crackdown2Recomp-beta0.5.0.zip","direct_asset_url":"https://gitlab.com/pacific-city-recompiled/crackdown-2-recomp/-/releases/v0.5.0-beta/downloads/Crackdown2Recomp-beta0.5.0.zip"},{"name":"Crackdown2Recomp-beta0.5.0.zip.sha256","url":"https://gitlab.com/-/project/88052946/uploads/00/Crackdown2Recomp-beta0.5.0.zip.sha256"}]}}])";
  auto r=parse_releases(json,cd2_updates);
  require(r.size()==1&&r[0].prerelease&&r[0].zip_url.ends_with("Crackdown2Recomp-beta0.5.0.zip")&&asset_url_allowed(r[0].zip_url,cd2_updates)&&asset_url_allowed(r[0].sha_url,cd2_updates),"Crackdown 2 package asset");
  require(parse_releases(json)[0].zip_url.empty(),"Crackdown 1 ignores Crackdown 2 packages");
  for(const auto& name:{"crackdown2.exe","rexgpu-xenos.dll","game-manifest.txt","setup_game.py","LICENSES/ReXGlue-and-Xenia.txt"}) require(update_path_allowed(name,cd2_updates),"Crackdown 2 package file");
  for(const auto& name:{"CrackdownLauncher.exe","crackdown.exe","crackdown2.toml","game/default.xex","tu/default.xexp","xui/Scene.xpatch","release.txt","portable.txt"}) require(!update_path_allowed(name,cd2_updates),"Crackdown 2 forbidden file");
  require(!update_path_allowed("crackdown2.exe"),"Crackdown 1 rejects Crackdown 2 files");
  auto folder=home/"package/cd2",stage=home/"cd2-staged"; fs::create_directories(folder); fs::create_directories(stage);
  require(installed_version(folder,cd2_updates).empty(),"nothing installed");
  write(folder/"crackdown2.exe","old"); write(folder/"crackdown2.toml","settings"); write(folder/"game/default.xex","disc"); write(folder/"tu/default.xexp","update");
  write(folder/"VERSION.txt","\xEF\xBB\xBF" "Crackdown 2 Recomp beta 0.4.0\r\nRepo: x\r\n"); require(installed_version(folder,cd2_updates)=="v0.4.0-beta","VERSION.txt beta version");
  write(folder/"VERSION.txt","Crackdown 2 Recomp 1.0.0\n"); require(installed_version(folder,cd2_updates)=="v1.0.0","VERSION.txt release version");
  write(folder/"VERSION.txt","1.3.0\r\n"); require(installed_version(folder,cd1_updates)=="v1.3.0","Crackdown 1 bare VERSION.txt");
  write(folder/"release.txt","v0.5.0-beta\n"); require(installed_version(folder,cd2_updates)=="v0.5.0-beta","launcher-written release tag wins");
  for(const auto& name:{"crackdown2.exe","rexruntime.dll","rexgpu-xenos.dll"}) write(stage/name,"new");
  rejects([&]{replace_staged(folder,stage,{},cd2_updates);},"Crackdown 2 package without manifest rejected");
  write(stage/"game-manifest.txt","manifest"); write(stage/"setup_game.py","script");
  rejects([&]{replace_staged(folder,stage);},"Crackdown 2 package rejected as a Crackdown 1 update");
  replace_staged(folder,stage,{},cd2_updates); require(text(folder/"crackdown2.exe")=="new"&&text(folder/"game-manifest.txt")=="manifest","Crackdown 2 installed");
  require(!fs::exists(folder/"CrackdownLauncher.old.exe"),"no launcher rename in cd2");
  recover_update(folder,cd2_updates); require(!fs::exists(folder/".launcher-update-backup"),"Crackdown 2 backup finalized");
  require(text(folder/"crackdown2.toml")=="settings"&&text(folder/"game/default.xex")=="disc"&&text(folder/"tu/default.xexp")=="update","Crackdown 2 game files and settings untouched");
  write(stage/"crackdown2.exe","newer"); rejects([&]{replace_staged(folder,stage,[](size_t i){if(i==1) throw std::runtime_error("Injected disk failure");},cd2_updates);},"Crackdown 2 failure injection");
  require(text(folder/"crackdown2.exe")=="new"&&!fs::exists(folder/".launcher-update-backup"),"Crackdown 2 rollback");
}
}
int main(int argc,char** argv) {
  auto home=fs::temp_directory_path()/("launcher-v2-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); fs::create_directory(home);
  try { std::string test=argc>1?argv[1]:""; if(test=="xzp_reader") xzp(); else if(test=="vdf_round_trip") vdf(home); else if(test=="update_version") versions(); else if(test=="sha256_verification") sha(home); else if(test=="staged_replacement") staged(home); else if(test=="cd2_updates") cd2(home); else throw std::runtime_error("Unknown test case");
    fs::remove_all(home); std::cout<<checks<<" checks passed\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<" after "<<checks<<" checks; fixture "<<utf8(home)<<'\n'; return 1; }
}

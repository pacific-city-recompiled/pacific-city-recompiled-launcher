// Manual checks of the updater and the content installers (not run by ctest, not shipped).
// Usage: launcher_update_probe <cd1|cd2> [<package folder> <tag>]   lists one game's GitHub releases and optionally
//                                                                     installs one, as the Updates dialog does
//        launcher_update_probe --title-update <cd1|cd2> <install folder> <game folder> <package>
//        launcher_update_probe --dlc <cd1|cd2> <save folder> <package>    packages, as the launcher installs them
#include "content.h"
#include "games.h"
#include <iostream>
using namespace launcher;
int main(int argc,char** argv) {
  std::atomic_bool cancel=false;
  try {
    if(argc==6&&std::string(argv[1])=="--title-update") {
      const auto& g=kGames[game_index(argv[2])];
      std::cout<<install_content_update(fs::u8path(argv[5]),fs::u8path(argv[3]),fs::u8path(argv[4]),g.title_id,g.update_xexp,cancel,{},!g.update_exe)<<" installed\n";
      return 0;
    }
    if(argc==5&&std::string(argv[1])=="--dlc") {
      const auto& g=kGames[game_index(argv[2])];
      auto pack=install_content_dlc(fs::u8path(argv[4]),fs::u8path(argv[3]),g.title_id,cancel);
      std::cout<<pack.title<<" installed as "<<pack.file_name<<'\n';
      for(const auto& p:list_content_dlc(fs::u8path(argv[3]),g.title_id)) std::cout<<"  listed: "<<p.title<<'\n';
      return 0;
    }
    if(argc!=2&&argc!=4) { std::cerr<<"usage: see update_probe.cpp\n"; return 2; }
    const auto& g=kGames[game_index(argv[1])];
    auto releases=check_releases(true,cancel,*g.updates);
    for(const auto& r:releases) std::cout<<r.tag<<(r.prerelease?" (prerelease) ":" ")<<(r.zip_url.empty()?"no package":r.zip_url)<<'\n';
    if(argc==4) {
      fs::path package=fs::absolute(fs::u8path(argv[2]));
      for(const auto& r:releases) if(r.tag==argv[3]) {
        install_release(r,package,cancel,*g.updates);
        std::cout<<"installed "<<r.tag<<"; installed_version="<<installed_version(game_home(package,g),*g.updates)<<'\n'; return 0;
      }
      std::cerr<<"no release "<<argv[3]<<'\n'; return 1;
    }
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}

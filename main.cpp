#include "content.h"
#include "core.h"
#include "games.h"
#include "platform.h"
#include "v2.h"
#include "visuals.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <memory>
#include <thread>
#include <sstream>
#include <cmath>

using namespace launcher;
namespace {
// kind: what the picked file is for (0 disc image or game folder, 1 title update package, 2 DLC package).
struct Picker { std::mutex mutex; std::optional<fs::path> picked; bool folder=false; bool pending=false; int kind=0; std::string error; };
void SDLCALL picked(void* data,const char* const* files,int) {
  auto& p=*static_cast<Picker*>(data); std::lock_guard lock(p.mutex);
  if(!files) p.error=SDL_GetError(); else if(files[0]) p.picked=fs::u8path(files[0]);
  p.pending=false;
}
struct Task {
  std::thread thread; std::atomic_bool cancel=false, done=false;
  std::atomic<uint64_t> bytes=0,total=0;
  std::mutex mutex; std::string status,hash; fs::path folder; bool success=false;
  ~Task() { cancel=true; if(thread.joinable()) thread.join(); }
  void message(std::string s) { std::lock_guard lock(mutex); status=std::move(s); }
};
template<class T> T value(const toml::table& t,const char* key,T fallback) { auto n=setting(t,key); return n?n->value<T>().value_or(fallback):fallback; }
// One theme for every page: dark glass panels over the game art, with the Xbox 360 green as the accent.
constexpr ImVec4 kAccent{.45f,.78f,.18f,1},kAccentHover{.53f,.86f,.25f,1},kAccentDeep{.31f,.60f,.10f,1};
constexpr ImVec4 kText{.93f,.95f,.94f,1},kMuted{.63f,.68f,.67f,1},kWarn{.98f,.73f,.25f,1},kError{.97f,.45f,.40f,1};
constexpr ImVec4 kPanel{.035f,.05f,.06f,.84f};
void apply_theme(ImGuiStyle& st,float s) {
  st.WindowPadding={40*s,20*s}; st.ItemSpacing={12*s,12*s}; st.ItemInnerSpacing={10*s,8*s}; st.FramePadding={14*s,9*s};
  st.CellPadding={10*s,7*s}; st.FrameRounding=6*s; st.WindowRounding=10*s; st.ChildRounding=10*s; st.PopupRounding=8*s;
  st.GrabRounding=6*s; st.ScrollbarRounding=6*s; st.ScrollbarSize=12*s; st.GrabMinSize=18*s;
  st.FrameBorderSize=0; st.ChildBorderSize=1; st.PopupBorderSize=1; st.WindowBorderSize=1;
  auto* c=st.Colors;
  c[ImGuiCol_Text]=kText; c[ImGuiCol_TextDisabled]=kMuted;
  c[ImGuiCol_WindowBg]={.05f,.07f,.08f,.97f}; c[ImGuiCol_ChildBg]={0,0,0,0}; c[ImGuiCol_PopupBg]={.06f,.08f,.09f,.99f};
  c[ImGuiCol_Border]={1,1,1,.09f}; c[ImGuiCol_ModalWindowDimBg]={0,0,0,.62f};
  c[ImGuiCol_FrameBg]={.12f,.15f,.16f,.94f}; c[ImGuiCol_FrameBgHovered]={.17f,.21f,.22f,1}; c[ImGuiCol_FrameBgActive]={.20f,.25f,.26f,1};
  c[ImGuiCol_Button]={.13f,.16f,.17f,.94f}; c[ImGuiCol_ButtonHovered]={.20f,.24f,.25f,1}; c[ImGuiCol_ButtonActive]={.25f,.30f,.31f,1};
  c[ImGuiCol_Header]={.45f,.78f,.18f,.20f}; c[ImGuiCol_HeaderHovered]={.45f,.78f,.18f,.30f}; c[ImGuiCol_HeaderActive]={.45f,.78f,.18f,.40f};
  c[ImGuiCol_CheckMark]=kAccent; c[ImGuiCol_SliderGrab]=kAccent; c[ImGuiCol_SliderGrabActive]=kAccentHover;
  c[ImGuiCol_NavCursor]=kAccentHover; c[ImGuiCol_Separator]={1,1,1,.10f}; c[ImGuiCol_TableBorderLight]={1,1,1,.06f};
  c[ImGuiCol_ScrollbarBg]={0,0,0,0}; c[ImGuiCol_ScrollbarGrab]={1,1,1,.14f}; c[ImGuiCol_ScrollbarGrabHovered]={1,1,1,.22f}; c[ImGuiCol_ScrollbarGrabActive]={1,1,1,.30f};
  c[ImGuiCol_PlotHistogram]=kAccent; c[ImGuiCol_TextSelectedBg]={.45f,.78f,.18f,.35f};
  c[ImGuiCol_TitleBg]=c[ImGuiCol_TitleBgActive]={.06f,.08f,.09f,1};
}
// Status marks drawn with the draw list (no icon font): a filled circle with a check or "!", or an empty ring.
enum class Mark { ok, warn, bad, off };
void mark(Mark m,float s) {
  auto* dl=ImGui::GetWindowDrawList(); auto p=ImGui::GetCursorScreenPos(); float r=8.5f*s, line=ImGui::GetTextLineHeight();
  ImVec2 c{p.x+r,p.y+line*.5f};
  ImU32 color=ImGui::GetColorU32(m==Mark::ok?kAccent:m==Mark::warn?kWarn:m==Mark::bad?kError:kMuted), ink=ImGui::GetColorU32(ImVec4{.03f,.05f,.04f,1});
  if(m==Mark::off) dl->AddCircle(c,r-.75f*s,color,0,1.5f*s); else dl->AddCircleFilled(c,r,color);
  if(m==Mark::ok) { ImVec2 pts[]={{c.x-r*.45f,c.y+r*.02f},{c.x-r*.1f,c.y+r*.37f},{c.x+r*.48f,c.y-r*.36f}}; dl->AddPolyline(pts,3,ink,0,2.f*s); }
  else if(m!=Mark::off) { dl->AddLine({c.x,c.y-r*.52f},{c.x,c.y+r*.12f},ink,2.2f*s); dl->AddCircleFilled({c.x,c.y+r*.46f},1.3f*s,ink); }
  ImGui::Dummy({r*2,line}); ImGui::SameLine(0,9*s);
}
}
int main(int argc,char** argv) {
  // Headless diagnostic paths exercise exactly the core used by the UI.
  if((argc==4 || (argc==5 && std::string(argv[4])=="--full")) && std::string(argv[1])=="--verify-folder") {
    try { std::cout<<verify_folder(fs::u8path(argv[2]),load_manifest(fs::u8path(argv[3])),nullptr,argc==5?Check::full:Check::quick)<<'\n'; return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
  }
  if(argc==4 && std::string(argv[1])=="--inspect-iso") {
    try { Image image(fs::u8path(argv[2])); auto hash=image.xex_hash(); if(hash!=supported_xex) throw std::runtime_error("Unsupported version: "+hash); image.validate(load_manifest(fs::u8path(argv[3]))); std::cout<<image.files().size()<<" files; XEX "<<hash<<'\n'; return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
  }
  if(argc==4 && std::string(argv[1])=="--install-iso") {
    try {
      auto destination=fs::absolute(fs::u8path(argv[3]));
      InstanceLock lock(destination);
      std::atomic_bool cancel=false;
      auto progress=[](const char* phase) {
        return [phase,last=-1](uint64_t bytes,uint64_t total) mutable {
          int percent=total?static_cast<int>(100.0*bytes/total):0;
          if(percent/10!=last) { last=percent/10; std::cout<<phase<<": "<<percent<<"%\n"; }
        };
      };
      auto manifest=load_manifest(destination/"game-manifest.txt");
      install_image(fs::u8path(argv[2]),destination,manifest,cancel,
                    progress("Extracting"),progress("Verifying"),kGames[std::max(kSingleGame,0)].xex_hash);
      std::cout<<"Installed and fully verified "<<manifest.size()<<" files in "<<utf8(destination/"game")<<'\n';
      return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
  }
  // Content packages of the single game (or Crackdown 1), as the INSTALL TITLE UPDATE / INSTALL DLC buttons do:
  // --install-update <package> <game folder (holds game\)>, --install-dlc <package> <save folder>.
  if(argc==4 && (std::string(argv[1])=="--install-update"||std::string(argv[1])=="--install-dlc")) {
    try {
      const auto& g=kGames[std::max(kSingleGame,0)]; std::atomic_bool cancel=false;
      auto target=fs::absolute(fs::u8path(argv[3]));
      if(std::string(argv[1])=="--install-update") std::cout<<install_content_update(fs::u8path(argv[2]),target,target/"game",g.title_id,g.update_xexp,cancel,{},!g.update_exe)<<" installed\n";
      else { auto pack=install_content_dlc(fs::u8path(argv[2]),target,g.title_id,cancel); std::cout<<pack.title<<" installed ("<<pack.damaged_blocks<<" damaged blocks)\n"; }
      return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
  }
  // --review-ui <dir> <width> <height> [--game <id>] [--connect <link>]: saves frames of every page and dialog.
  bool review=argc>=5 && std::string(argv[1])=="--review-ui";
  fs::path review_dir;
  int review_width=1280,review_height=720;
  if(review) {
    try {
      review_dir=fs::absolute(fs::u8path(argv[2]));
      review_width=std::stoi(argv[3]); review_height=std::stoi(argv[4]);
      if(review_width<960||review_width>3840||review_height<600||review_height>2160)
        throw std::runtime_error("Review size must be 960-3840 by 600-2160");
      fs::create_directories(review_dir);
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
  }
  if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) { std::cerr<<SDL_GetError(); return 1; }
  const char* base=SDL_GetBasePath(); fs::path home=fs::absolute(base?fs::u8path(base):fs::current_path());
  std::unique_ptr<InstanceLock> instance;
  bool updated=argc==2 && std::string(argv[1])=="--updated";
  try {
    for(unsigned attempt=0;;++attempt) {
      try { instance=std::make_unique<InstanceLock>(home); break; }
      catch(...) { if(!updated||attempt==100) throw; SDL_Delay(100); }
    }
    if(kSingleGame<0) {
      recover_update(home);
      // Games other than Crackdown 1 update in their own folder (no restart), so an interrupted one is rolled back here.
      for(const auto& g:kGames) if(*g.folder&&fs::is_directory(home/g.folder)) recover_update(home/g.folder,*g.updates);
    }
  } catch(const std::exception& e) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Crackdown Recomp",e.what(),nullptr);
    SDL_Quit(); return 1;
  }
  auto show_folder=[](const fs::path& dir) { open_folder(dir,[](const char* url){return SDL_OpenURL(url);}); };
  float initial_dpi=std::max(1.f,SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
  // --review-ui draws into an offscreen texture of a hidden window: no window shows up or takes focus.
  SDL_Window* window=SDL_CreateWindow("Crackdown Recomp",int(1280*initial_dpi),int(720*initial_dpi),SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(review?SDL_WINDOW_HIDDEN:0));
  SDL_Renderer* renderer=window?SDL_CreateRenderer(window,nullptr):nullptr;
  if(!renderer) { std::cerr<<SDL_GetError(); SDL_Quit(); return 1; }
  SDL_Texture* review_target=review?SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,review_width,review_height):nullptr;
  if(review&&!review_target) { std::cerr<<SDL_GetError(); SDL_Quit(); return 1; }
  SDL_SetWindowMinimumSize(window,960,600);
  SDL_SetRenderVSync(renderer,1);
  ImGui::CreateContext(); ImGui::StyleColorsDark();
  auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
  io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
  io.ConfigNavCursorVisibleAlways=true; io.ConfigNavEscapeClearFocusItem=false;
  io.ConfigNavEscapeClearFocusWindow=false;
  auto visuals=std::make_unique<Visuals>(renderer,home);
  ImGui_ImplSDL3_InitForSDLRenderer(window,renderer); ImGui_ImplSDLRenderer3_Init(renderer);
  // One launcher, several games (games.h): the selected game decides the folder, exe, settings file, manifest,
  // save folder and log prefix. launcher.toml stays shared.
  fs::path preferences=home/"launcher.toml";
  int game_sel=0;
  try { game_sel=game_index(value<std::string>(read_config(preferences),"selected_game","cd1")); } catch(const std::exception&) {}
  // --game=<id> or --game <id> (the Crackdown 2 apworld's client starts "CrackdownLauncher --game crackdown2 --connect <uri>").
  for(int i=1;i<argc;++i) { std::string a=argv[i]; if(a.starts_with("--game=")) game_sel=game_index(a.substr(7)); else if(a=="--game"&&i+1<argc) game_sel=game_index(argv[++i]); }
  if(kSingleGame>=0) { game_sel=kSingleGame; SDL_SetWindowTitle(window,game_sel==0?"Crackdown Recomp":"Crackdown 2 Recomp"); }
  const GameProfile* gp=&kGames[game_sel];
  fs::path ghome=game_home(home,*gp), config=ghome/gp->config;
  Task task; Picker picker; GameProcess process; std::vector<Entry> manifest;
  std::string error,report,hash,crash; fs::path game; bool verified=false,writable=true,close_on_play=false;
  Changes changes; int scale=1,window_size=0,rate=0,aa=0,aniso=2,aspect=0; [[maybe_unused]] int filter=0; bool fullscreen=true,mute=false;
  float mouse_sens=1.0f;
  float camera_speed=1.0f;
  bool skip_intro=true,mnk=false,auto_updates=false,beta_channel=false,ask_updates=false;
  int monitor=0,language=1,tab=0; bool focus_page=true; std::string device="keyboard",input_backend="sdl";
  fs::file_time_type log_since{};
  CoopSettings coop; char coop_own[64]{},coop_partner[64]{}; std::string coop_error; std::vector<std::string> coop_addresses;
  std::vector<ContentPack> dlc_packs;  // installed DLC of the selected game (gp->dlc)
  bool use_title_update=true;  // Crackdown 1: start the title update build once its update is installed
  auto update_exe_ready=[&] { return gp->update_exe&&fs::is_regular_file(ghome/gp->update_exe)&&fs::is_regular_file(ghome/"tu"/"default.xexp"); };
  fs::path user; bool portable=false; std::vector<std::string> profiles; std::string profile; char new_profile[48]{};
  try {
    auto probe=home/(".launcher-write-"+std::to_string(SDL_GetTicks()));
    if(fs::exists(probe)) throw std::runtime_error("Write probe already exists");
    std::ofstream out(probe); if(!out) throw std::runtime_error("Extract the ZIP into a writable folder, such as Downloads or Documents");
    out<<"probe"; out.close(); if(!out) throw std::runtime_error("Cannot write next to the launcher"); fs::remove(probe);
  } catch(const std::exception& e) { error=e.what(); writable=false; }
  std::string game_version;  // installed release of the selected game (combined Crackdown 1: the launcher's own version)
  // Everything read from the selected game's settings file and manifest; rerun when the game changes.
  auto read_game=[&] {
  config=ghome/gp->config; manifest.clear();
  game_version=gp->updates==&cd1_updates&&kSingleGame<0?std::string(LAUNCHER_VERSION):installed_version(ghome,*gp->updates);
  try {
    auto t=read_config(config);
    std::string configured=value<std::string>(t,"game_data_root","");
    game=configured.empty()?ghome/"game":fs::u8path(configured);
    if(game.is_relative()) game=ghome/game;
    scale=static_cast<int>(value<int64_t>(t,"resolution_scale",1));
    fullscreen=value<bool>(t,"fullscreen",true); mute=value<bool>(t,"audio_mute",false);
    auto f=value<std::string>(t,"present_effect","bilinear"); filter=f=="cas"?1:f=="fsr"?2:0;
    if(game_sel==0) { auto cap=value<int64_t>(t,"fps_cap",60); rate=value<bool>(t,"vsync",false)?1:cap==120?2:cap==144?3:0; } else { auto cap=value<int64_t>(t,"fps_cap",0); rate=cap==30?1:cap==120?2:cap==144?3:0; }
    auto ratio=value<std::string>(t,"aspect_ratio","16:9"); aspect=ratio=="16:10"?1:ratio=="21:9"?2:ratio=="32:9"?3:ratio=="4:3"?4:0;
    auto post=value<std::string>(t,"swap_post_effect","none"); aa=post=="fxaa"?1:post=="fxaa_extreme"?2:0;
    // anisotropic_override: -1 game's own, 2 = 2x, 3 = 4x (SDK default), 4 = 8x, 5 = 16x.
    switch(value<int64_t>(t,"anisotropic_override",3)) { case -1: aniso=0; break; case 2: aniso=1; break; case 4: aniso=3; break; case 5: aniso=4; break; default: aniso=2; }
    auto width=value<int64_t>(t,"window_width",0); window_size=width==1280?1:width==1920?2:width==2560?3:0;
    language=static_cast<int>(value<int64_t>(t,"user_language",1)); if(language<1||language>10) language=1;
    skip_intro=value<bool>(t,"skip_intro_videos",true); mnk=value<bool>(t,"mnk_mode",false);
    mouse_sens=std::clamp(static_cast<float>(value<double>(t,"mnk_sensitivity",1.0)),0.1f,5.0f);
    coop=load_coop(t);
    std::snprintf(coop_own,sizeof(coop_own),"%s",coop.own_address.c_str()); std::snprintf(coop_partner,sizeof(coop_partner),"%s",coop.partner_address.c_str());
    camera_speed=std::clamp(static_cast<float>(value<double>(t,"camera_sensitivity",1.0)),0.25f,4.0f);
    input_backend=value<std::string>(t,"input_backend","sdl"); monitor=std::clamp(static_cast<int>(value<int64_t>(t,"monitor",0)),0,16);
  } catch(const std::exception& e) { error=e.what(); }
  // A game whose runtime isn't downloaded yet has no manifest either; the home page says so.
  try { manifest=load_manifest(ghome/gp->manifest); } catch(const std::exception& e) { if(fs::exists(ghome/gp->exe)) error=e.what(); }
  };
  read_game();
  coop_addresses=local_ipv4_addresses();
  try {
    auto prefs=read_config(preferences);
    auto_updates=value<bool>(prefs,"auto_updates",false); beta_channel=value<bool>(prefs,"beta_updates",false);
    ask_updates=!value<bool>(prefs,"update_consent_asked",false);
    int lw=std::clamp(static_cast<int>(value<int64_t>(prefs,"launcher_width",int(1280*initial_dpi))),960,3840);
    int lh=std::clamp(static_cast<int>(value<int64_t>(prefs,"launcher_height",int(720*initial_dpi))),600,2160);
    SDL_SetWindowSize(window,lw,lh);
    close_on_play=value<bool>(prefs,"close_on_play",false);
    use_title_update=value<bool>(prefs,"cd1_title_update",true);
  } catch(const std::exception& e) { error=e.what(); }
  if(kSingleGame>=0) { auto_updates=false; ask_updates=false; }  // the app launcher that installed it updates it
  // Save root and profile of the selected game; rerun when the game changes.
  auto read_user=[&] {
    try {
      auto prefs=read_config(preferences);
      // Portable mode (portable.txt next to the game's exe): saves live in userdata/ there, and the
      // game, which sees the same marker next to its exe, uses it too.
      portable=fs::is_regular_file(ghome/"portable.txt");
      user=fs::absolute(portable?ghome/"userdata":default_user_folder(gp->user_folder)).lexically_normal();
      profiles=list_profiles(user); profile=value<std::string>(prefs,gp->profile_key,"");
      if(std::find(profiles.begin(),profiles.end(),profile)==profiles.end()) profile.clear();
    } catch(const std::exception& e) { error=e.what(); }
  };
  read_user();
  // Archipelago settings belong to the selected (named) profile: one profile per seed.
  ApConnection ap; char ap_server[128]{},ap_slot[64]{},ap_password[128]{}; std::string ap_bound;
  // --connect <archipelago:// link> (from the Archipelago Launcher). It waits on the ARCHIPELAGO tab
  // until the player applies it to the profile they choose; it then replaces server, slot and password.
  std::optional<ApConnection> ap_link;
  for(int i=1;i+1<argc;++i) if(std::string(argv[i])=="--connect") {
    ap_link=parse_ap_uri(argv[i+1]);
    if(!ap_link) error="The Archipelago room link is not valid: expected archipelago://slot:password@host:port";
  }
  const bool ap_link_given=ap_link.has_value();
  auto load_ap=[&] {
    ap={}; ap_bound.clear();
    try { if(!user.empty()&&!profile.empty()) { auto saves=profile_folder(user,profile); ap=load_ap_connection(saves); ap_bound=ap_binding(saves); } }
    catch(const std::exception& e) { error=e.what(); }
    std::snprintf(ap_server,sizeof(ap_server),"%s",ap.server.c_str()); std::snprintf(ap_slot,sizeof(ap_slot),"%s",ap.slot.c_str());
    std::snprintf(ap_password,sizeof(ap_password),"%s",ap.password.c_str());
  };
  auto save_ap=[&] { ap.server=ap_server; ap.slot=ap_slot; ap.password=ap_password; save_ap_connection(profile_folder(user,profile),ap); };
  load_ap();
  if(gp->dlc) dlc_packs=list_content_dlc(user,gp->title_id);
  // The on-screen keyboard (controller text entry) edits one buffer at a time.
  struct Keyboard { char* text; size_t size; const char* title; bool secret=false,ap=false; } keyboard{new_profile,sizeof(new_profile),"PROFILE NAME"};
  // An import or a newly selected folder gets the full hash check once; startup re-checks quickly.
  // title_update / dlc_pack install a content package (content.cpp) and keep the game's verified state.
  enum class Job { import_iso, full_check, quick_check, title_update, dlc_pack };
  Job running_job=Job::quick_check;
  auto begin=[&](fs::path path,Job job) {
    if(task.thread.joinable()) task.thread.join();
    running_job=job; bool content=job==Job::title_update||job==Job::dlc_pack;
    if(!content) { verified=false; hash.clear(); }
    task.cancel=false; task.done=false; task.success=false; task.bytes=0; task.total=0;
    task.message(content?"Checking the package...":job==Job::import_iso?"Verifying disc version...":"Verifying game files...");
    if(content) {
      task.thread=std::thread([&,path,job,g=*gp,folder=ghome,disc=game,saves=user] {
        try {
          auto progress=[&task,started=false](uint64_t n,uint64_t total) mutable { if(!started) { task.message("Unpacking and checking every block..."); started=true; } task.bytes=n; task.total=total; };
          std::string title=job==Job::title_update?install_content_update(path,folder,disc,g.title_id,g.update_xexp,task.cancel,progress,!g.update_exe)
:[&]{ auto pack=install_content_dlc(path,saves,g.title_id,task.cancel,progress);
                                                       return pack.damaged_blocks?pack.title+" ("+std::to_string(pack.damaged_blocks)+" damaged block(s) in this dump, kept as they are)":pack.title; }();
          std::lock_guard lock(task.mutex); task.status=title+" installed."; task.success=true;
        } catch(const std::exception& e) { task.message(e.what()); }
        task.done=true;
      });
      return;
    }
    task.thread=std::thread([&,path,job] {
      auto phase=[&](const char* text) {
        return [&task,text,started=false](uint64_t n,uint64_t total) mutable {
          if(!started) { task.message(text); started=true; }
          task.bytes=n; task.total=total;
        };
      };
      try {
        auto folder=path; auto check=job==Job::quick_check?Check::quick:Check::full;
        if(job==Job::import_iso) {
          install_image(path,ghome,manifest,task.cancel,phase("Extracting game files..."),phase("Checking every extracted file..."),gp->xex_hash,gp->manifest);
          folder=ghome/"game"; check=Check::quick;
        }
        auto h=verify_folder_cached(folder,manifest,ghome/"verified-files.txt",&task.cancel,check,phase(check==Check::full?"Checking every game file (one time)...":"Verifying game files..."),gp->xex_hash);
        std::lock_guard lock(task.mutex); task.folder=fs::absolute(folder); task.hash=h; task.status="Game files verified"; task.success=true;
      } catch(const std::exception& e) { task.message(e.what()); }
      task.done=true;
    });
  };
  bool busy=false;
  // First run: a short guided setup (disc, then title update, then DLC) until the player finishes or skips it.
  auto first_run=[&] { return writable&&!manifest.empty()&&fs::is_regular_file(ghome/gp->exe)&&!fs::is_directory(game); };
  bool setup=first_run(),tu_skipped=false;
  if(writable && !manifest.empty() && fs::is_directory(game)) { begin(game,Job::quick_check); busy=true; }
  auto leftovers=writable?unfinished_imports(ghome,game):std::vector<fs::path>{};
  if(review) SDL_SetWindowSize(window,review_width,review_height);
  // Home plus four settings pages; rare and debug options sit in each page's Advanced fold.
  constexpr int tab_count=5, tab_saves=3, tab_about=4;
  if(ap_link_given) tab=tab_saves;
  // Smoke/review: two frames per page, two more per settings page with Advanced open, then each dialog for four
  // frames (d = first dialog frame).
  constexpr unsigned d=2*tab_count+2*(tab_count-1);
  bool force_advanced=false;
  bool quit=false,review_failed=false; unsigned smoke=review || (argc==2 && std::string(argv[1])=="--smoke-ui")?d+22:0;
  bool is_smoke=smoke!=0;
  if(is_smoke) ask_updates=false;
  // Each game checks its own repository (games.h updates). Crackdown 1's package is the launcher itself, so its
  // version is the launcher's; another game's is the release installed in its folder (empty: not installed).
  struct Online {
    std::thread worker; std::atomic_bool done=false,cancel=false;
    bool busy=false; std::atomic_bool install=false; std::vector<Release> releases; std::string message;
    int game=-1;  // the game the releases and the running job belong to
    ~Online() { cancel=true; if(worker.joinable()) worker.join(); }
  } online;
  auto launcher_package=[&] { return gp->updates==&cd1_updates; };
  auto current_version=[&] { return game_version; };
  bool online_authorized=auto_updates;
  auto check_updates=[&] {
    online_authorized=true;
    if(online.busy) return;
    if(online.worker.joinable()) online.worker.join();
    online.busy=true; online.install=false; online.done=false; online.cancel=false; online.message="Checking releases..."; online.releases.clear();
    online.game=game_sel;
    // Crackdown 2 has only prereleases so far: they are always offered for it.
    online.worker=std::thread([&,source=gp->updates,beta=beta_channel||!launcher_package(),current=current_version()] {
      try {
        auto r=check_releases(beta,online.cancel,*source); online.releases=std::move(r);
        online.message=online.releases.empty()?"No releases found.":current.empty()?"Not installed yet: choose a release to install.":compare_versions(online.releases.front().tag,current)>0?"An update is available.":"You are up to date.";
      }
      catch(const std::exception& e) { online.message=e.what(); }
      online.done=true;
    });
  };
  auto install_update=[&](Release release) {
    if(online.worker.joinable()) online.worker.join();
    online.busy=true; online.install=true; online.done=false; online.cancel=false; online.message="Downloading, verifying and staging update...";
    online.game=game_sel;
    online.worker=std::thread([&,release,source=gp->updates,restart=launcher_package()] {
      try { install_release(release,home,online.cancel,*source); online.message=restart?"Update installed. Restarting...":release.tag+" installed."; }
      catch(const std::exception& e) { online.message=e.what(); online.install=false; }
      online.done=true;
    });
  };
  // Explicit persisted consent is required before the first startup request.
  if(auto_updates&&!ask_updates&&!is_smoke) check_updates();
  enum class Dialog { none,install,browser,keyboard,updates,quit,package,save_warning };
  Dialog dialog=Dialog::none; bool open_dialog=false,dialog_focus=false,folder_browser=false;
  int package_kind=0;  // Dialog::package and its file browser: 1 title update, 2 DLC (0: disc browser)
  fs::path browse=home; std::vector<fs::directory_entry> browse_entries; std::vector<fs::path> browse_roots;
  auto refresh_browser=[&] {
    browse_entries.clear(); std::error_code ec;
    for(fs::directory_iterator i(browse,fs::directory_options::skip_permission_denied,ec),end;!ec&&i!=end;i.increment(ec)) {
      auto& entry=*i; bool dir=entry.is_directory(ec); if(ec) { ec.clear(); continue; }
      auto extension=utf8(entry.path().extension()); std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
      // Content packages have no extension: the package browser lists every file.
      if(dir||(!folder_browser&&(package_kind?entry.is_regular_file(ec):extension==".iso"))) browse_entries.push_back(entry);
    }
    std::sort(browse_entries.begin(),browse_entries.end(),[](const auto& a,const auto& b){std::error_code ec; bool ad=a.is_directory(ec),bd=b.is_directory(ec); if(ad!=bd) return ad; return a.path().filename()<b.path().filename();});
  };
  browse_roots.push_back(home); if(!user.empty()) browse_roots.push_back(user.parent_path());
#ifdef _WIN32
  for(char drive='C';drive<='Z';++drive) { std::string root; root+=drive; root+=":/"; std::error_code ec; if(fs::is_directory(fs::u8path(root),ec)) browse_roots.push_back(fs::u8path(root)); }
#else
  browse_roots.push_back("/");
#endif
  auto show_dialog=[&](Dialog next) { dialog=next; open_dialog=true; dialog_focus=true; };
  // Choosing a disc, folder or package (kind: 0 disc, 1 title update, 2 DLC): the system picker for mouse and
  // keyboard, the built-in browser (Back returns to the dialog it came from) for controllers.
  Dialog browser_back=Dialog::none;
  bool advanced_open[5]{};  // per settings page
  auto browse_files=[&](bool folder,int kind) {
    folder_browser=folder; package_kind=kind; browse=kind?ghome:fs::is_directory(game)?game:ghome; refresh_browser();
    browser_back=dialog==Dialog::browser?browser_back:dialog;
    if(dialog==Dialog::none) show_dialog(Dialog::browser); else { dialog=Dialog::browser; dialog_focus=true; }
  };
  auto native_pick=[&](bool folder,int kind) {
    { std::lock_guard lock(picker.mutex); picker.pending=true; picker.folder=folder; picker.kind=kind; }
    static const SDL_DialogFileFilter filters[]={{"Xbox disc image","iso"}};
    if(folder) SDL_ShowOpenFolderDialog(picked,&picker,window,nullptr,false);
    else SDL_ShowOpenFileDialog(picked,&picker,window,kind?nullptr:filters,kind?0:1,nullptr,false);
  };
  auto pick=[&](bool folder,int kind) { if(device=="keyboard") native_pick(folder,kind); else browse_files(folder,kind); };
  // Selects another game: unsaved settings of the old one are dropped, everything game-specific is reloaded,
  // and the new game's files get the usual quick check.
  auto switch_game=[&](int index) {
    game_sel=index; gp=&kGames[index]; ghome=game_home(home,*gp);
    changes.clear(); error.clear(); crash.clear(); report.clear(); verified=false; hash.clear(); visuals->clear_art();
    online.releases.clear(); online.message.clear(); online.game=-1;
    read_game(); read_user(); load_ap();
    dlc_packs.clear(); if(gp->dlc) dlc_packs=list_content_dlc(user,gp->title_id);
    focus_page=true;
    try { update_config(preferences,{{"selected_game",toml_string(gp->id)}}); } catch(const std::exception& e) { error=e.what(); }
    leftovers=writable?unfinished_imports(ghome,game):std::vector<fs::path>{};
    setup=first_run(); tu_skipped=false;
    if(writable && !manifest.empty() && fs::is_directory(game)) { begin(game,Job::quick_check); busy=true; }
  };
  // Title update saves the disc build would call damaged (core.h title_update_saves); set when Play confirmed them.
  std::vector<fs::path> disc_unreadable; bool disc_confirmed=false;
  auto launch=[&] {
    try {
      for(const char* key:kApConfigKeys) changes[key]=std::nullopt;
      update_config(config,changes); changes.clear();
      hash=verify_folder_cached(game,manifest,ghome/"verified-files.txt",nullptr,Check::quick,{},gp->xex_hash); log_since=fs::file_time_type::clock::now();
      auto args=profile_args(user,profile); args.insert(args.begin(),"--game_data_root="+utf8(game));
      if(gp->title_update) {
        // Crackdown 2 runs only with its title update: default.xexp beside default.xex (the SDK applies it) and the
        // update's files as update:\ (tu/ next to the exe, or game\TU).
        if(!fs::is_regular_file(game/"default.xexp")) throw std::runtime_error(std::string(gp->title)+" needs its title update: default.xexp is missing in "+utf8(game));
        auto tu=fs::is_directory(ghome/"tu")?ghome/"tu":game/"TU";
        if(fs::is_directory(tu)) args.push_back("--update_data_root="+utf8(tu));
      }
      // DLC packs are installed once in the save root and shared by every profile (SDK patch 0025 of that game).
      if(gp->dlc) args.push_back("--dlc_root="+utf8(user));
      // The game reads the password from the profile's connection.toml itself.
      if(!profile.empty()) save_ap();
      auto extra=ap_args(profile.empty()?ApConnection{}:ap); args.insert(args.end(),extra.begin(),extra.end());
      // Crackdown 1 with Title Update #6 (Keys to the City, Street Racing): its own exe, the update from tu/.
      // Archipelago runs on the disc exe only (the bridge isn't ported to the update).
      const char* exe=gp->exe;
      if(update_exe_ready()&&use_title_update&&extra.empty()) { exe=gp->update_exe; args.push_back("--update_data_root="+utf8(ghome/"tu")); }
      // The disc build rejects a title update save and offers to overwrite it: ask first (Back is the default).
      if(gp->update_exe&&exe==gp->exe&&!disc_confirmed) {
        disc_unreadable=title_update_saves(profile_folder(user,profile));
        if(!disc_unreadable.empty()) { show_dialog(Dialog::save_warning); return; }
      }
      disc_confirmed=false;
      process.start(ghome,args,exe); error.clear(); crash.clear(); if(close_on_play) quit=true;
    } catch(const std::exception& e) { error=e.what(); verified=false; visuals->clear_art(); }
  };
  auto save_profile=[&](const std::string& name) {
    update_config(preferences,{{gp->profile_key,name.empty()?std::nullopt:std::optional<std::string>(toml_string(name))}}); profile=name; load_ap();
  };
  auto save_settings=[&] { try { update_config(config,changes); changes.clear(); error.clear(); } catch(const std::exception& e) { error=e.what(); } };
  bool auto_focus=true;
  bool modal_child_open=false;
  unsigned smoke_frame=0;
  // --play: press PLAY once the startup check has verified the game files (shortcuts, unattended tests).
  bool autoplay=false;
  for(int i=1;i<argc;++i) if(std::string(argv[i])=="--play") autoplay=true;
  while(!quit) {
    if(autoplay&&!busy) { autoplay=false; if(verified&&!process.running()) launch(); }
    if(is_smoke && (!review||!busy)) {
      force_advanced=smoke_frame>=2*tab_count&&smoke_frame<d;
      if(smoke_frame<2*tab_count) { tab=int(smoke_frame/2); focus_page=true; }
      else if(smoke_frame<d) { tab=1+int((smoke_frame-2*tab_count)/2); focus_page=true; }
      if(smoke_frame==d) show_dialog(Dialog::install);
      if(smoke_frame==d+4) { folder_browser=true; refresh_browser(); dialog=Dialog::browser; dialog_focus=true; }
      if(smoke_frame==d+8) { dialog=Dialog::keyboard; dialog_focus=true; }
      if(smoke_frame==d+12) { dialog=Dialog::updates; dialog_focus=true; }
      if(smoke_frame==d+16) { dialog=Dialog::quit; dialog_focus=true; }
      ++smoke_frame;
    }
    bool popup_before=ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    bool editing_before=io.WantTextInput,modal_child_before=modal_child_open; modal_child_open=false;
    bool back=false; int switch_tab=0;
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if(event.type==SDL_EVENT_QUIT) { if(online.busy&&online.install) online.cancel=true; else if(!is_smoke&&!changes.empty()) show_dialog(Dialog::quit); else quit=true; }
      if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        device="keyboard";
        if(event.key.key==SDLK_ESCAPE) back=true;
        if(!io.WantTextInput && dialog==Dialog::none) { if(event.key.key==SDLK_Q) --switch_tab; if(event.key.key==SDLK_E) ++switch_tab; }
      }
      if(event.type==SDL_EVENT_MOUSE_MOTION && (std::abs(event.motion.xrel)>1||std::abs(event.motion.yrel)>1)) device="keyboard";
      if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_WHEEL) device="keyboard";
      if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        auto id=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN?event.gbutton.which:event.gaxis.which;
        if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||std::abs(int(event.gaxis.value))>12000) {
          auto type=SDL_GetGamepadTypeForID(id); device=type==SDL_GAMEPAD_TYPE_PS3||type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5?"ps5":"xbox_series";
        }
        if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
          if(event.gbutton.button==SDL_GAMEPAD_BUTTON_EAST) back=true;
          if(dialog==Dialog::none) { if(event.gbutton.button==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) --switch_tab; if(event.gbutton.button==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ++switch_tab; }
        }
      }
      if(event.type==SDL_EVENT_WINDOW_RESIZED && writable && !is_smoke && !(online.busy&&online.install)) {
        try { update_config(preferences,{{"launcher_width",std::to_string(event.window.data1)},{"launcher_height",std::to_string(event.window.data2)}}); }
        catch(const std::exception& e) { error=e.what(); }
      }
    }
    if(switch_tab && !ask_updates && !popup_before && !editing_before) {
      tab=(tab+switch_tab%tab_count+tab_count)%tab_count;
      focus_page=true;
    }
    {
      std::lock_guard lock(picker.mutex);
      if(!picker.error.empty()) { error=picker.error; picker.error.clear(); }
      if(picker.picked&&picker.kind) { begin(*picker.picked,picker.kind==1?Job::title_update:Job::dlc_pack); busy=true; picker.picked.reset(); error.clear(); report.clear(); focus_page=true; }
      if(picker.picked) { visuals->clear_art(); begin(*picker.picked,picker.folder?Job::full_check:Job::import_iso); busy=true; picker.picked.reset(); error.clear(); tab=0; focus_page=true; }
    }
    if(busy && task.done && (running_job==Job::title_update||running_job==Job::dlc_pack)) {
      task.thread.join(); busy=false;
      std::lock_guard lock(task.mutex);
      if(task.success) { report=task.status; error.clear(); } else error=task.status;
      if(gp->dlc) dlc_packs=list_content_dlc(user,gp->title_id);
      focus_page=true;
    }
    if(busy && task.done) {
      task.thread.join(); busy=false;
      std::lock_guard lock(task.mutex);
      if(task.success) {
        try { update_config(config,{{"game_data_root",toml_string(utf8(task.folder))}}); game=task.folder; hash=task.hash; verified=true; leftovers=unfinished_imports(ghome,game); visuals->load_art(game,user,manifest); }
        catch(const std::exception& e) { error=e.what(); }
      } else error=task.status;
      focus_page=true;
    }
    if(online.busy&&online.done) {
      online.worker.join(); online.busy=false;
      if(online.install&&!launcher_package()) {
        // Installed into the game's folder: reload its manifest and settings and check its files again.
        online.install=false; auto message=online.message; switch_game(game_sel); online.message=message; tab=0;
      } else if(online.install) {
        // The successor waits for this process to release the package lock.
        try { restart_launcher(home); quit=true; }
        catch(const std::exception& e) { error=e.what(); try { rollback_completed_update(home); online.install=false; online.message="Update rolled back: "+error; } catch(const std::exception& rollback) { error+="; rollback requires restart: "+std::string(rollback.what()); } }
      }
    }
    if(auto code=process.poll()) {
      if(*code!=0 || process.elapsed()<10) {
        char text[96]; std::snprintf(text,sizeof(text),"Game %s (exit code 0x%08X)",*code!=0?"crashed":"exited during startup",static_cast<unsigned>(*code)); error=text; crash=newest_log_tail(ghome,log_since,gp->log_prefix);
      }
      focus_page=true;
    }
    ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame();
    if(review) { io.DisplaySize={float(review_width),float(review_height)}; io.DisplayFramebufferScale={1,1}; }
    ImGui::NewFrame();
    bool nested_popup=ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
    if(back&&!ask_updates) {
      if(dialog!=Dialog::none) {
        // ImGui handles B/Escape for a nested combo first; a modal closes at its own level below.
        if(!nested_popup) { dialog=Dialog::none; focus_page=true; }
      } else if(!nested_popup && !popup_before && !editing_before) {
        if(tab!=0) { tab=0; focus_page=true; }
        else if(busy) task.cancel=true;
        else show_dialog(Dialog::quit);
      }
    }
    ImVec2 size=io.DisplaySize; float ui_scale=std::clamp(std::min(size.x/1280.f,size.y/720.f),.75f,2.f); const float s=ui_scale;
    auto& style=ImGui::GetStyle(); style.FontScaleMain=ui_scale;
    // Window dimensions include initial display DPI; one scale keeps fonts and controls aligned.
    style.FontScaleDpi=1.f;
    apply_theme(style,s);
    visuals->backdrop(size);
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::Begin("Launcher",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    auto* draw=ImGui::GetWindowDrawList();
    const float margin=40*s, header_h=76*s, footer_h=58*s;
    // ---- Widgets shared by every page ----
    auto muted=[&](const char* text,float px=18) { ImGui::PushFont(nullptr,px); ImGui::PushStyleColor(ImGuiCol_Text,kMuted); ImGui::TextWrapped("%s",text); ImGui::PopStyleColor(); ImGui::PopFont(); };
    auto filled=[&](ImVec4 base,ImVec4 hover,ImVec4 active) { ImGui::PushStyleColor(ImGuiCol_Button,base); ImGui::PushStyleColor(ImGuiCol_ButtonHovered,hover); ImGui::PushStyleColor(ImGuiCol_ButtonActive,active); };
    bool auto_focus_now=false;  // set per page below
    auto focus_first=[&](bool enabled=true) { if(auto_focus_now&&enabled) { ImGui::SetKeyboardFocusHere(); auto_focus_now=false; } };
    // The big call to action (PLAY / INSTALL): Xbox green, condensed bold type.
    auto primary=[&](const char* label,ImVec2 sz,bool enabled=true,float px=40) {
      ImGui::BeginDisabled(!enabled); focus_first(enabled);
      filled(kAccentDeep,kAccent,kAccentHover); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,8*s); ImGui::PushFont(visuals->heading,px);
      bool clicked=ImGui::Button(label,{sz.x*s,sz.y*s});
      ImGui::PopFont(); ImGui::PopStyleVar(); ImGui::PopStyleColor(3); ImGui::EndDisabled(); return clicked;
    };
    // Everything else: dark glass with a hairline border.
    auto secondary=[&](const char* label,bool enabled=true,ImVec2 sz={0,0}) {
      ImGui::BeginDisabled(!enabled); focus_first(enabled);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.f); ImGui::PushStyleColor(ImGuiCol_Border,ImVec4{1,1,1,.14f});
      bool clicked=ImGui::Button(label,{sz.x*s,sz.y*s});
      ImGui::PopStyleColor(); ImGui::PopStyleVar(); ImGui::EndDisabled(); return clicked;
    };
    auto link=[&](const char* label) {
      filled({0,0,0,0},{1,1,1,.06f},{1,1,1,.10f}); ImGui::PushStyleColor(ImGuiCol_Text,kAccent);
      bool clicked=ImGui::Button(label); ImGui::PopStyleColor(4); return clicked;
    };
    // On/off switch (replaces checkboxes); focusable and pressable like a button.
    auto toggle=[&](const char* id,bool* v) {
      float frame=ImGui::GetFrameHeight(), h=frame*.72f, w=h*1.85f; auto p=ImGui::GetCursorScreenPos();
      bool clicked=ImGui::InvisibleButton(id,{w,frame}); if(clicked) *v=!*v;
      bool hot=ImGui::IsItemHovered()||ImGui::IsItemFocused();
      ImVec2 a{p.x,p.y+(frame-h)*.5f},b{p.x+w,a.y+h};
      auto* dl=ImGui::GetWindowDrawList();
      dl->AddRectFilled(a,b,ImGui::GetColorU32(*v?(hot?kAccent:kAccentDeep):(hot?ImVec4{.28f,.32f,.33f,1}:ImVec4{.22f,.26f,.27f,1})),h*.5f);
      dl->AddCircleFilled({*v?b.x-h*.5f:a.x+h*.5f,a.y+h*.5f},h*.5f-3*s,ImGui::GetColorU32(ImVec4{.96f,.97f,.96f,1}));
      return clicked;
    };
    auto notice=[&](Mark m,const std::string& text,float width) {
      ImVec4 tint=m==Mark::bad?ImVec4{.45f,.10f,.08f,.55f}:m==Mark::warn?ImVec4{.42f,.28f,.04f,.55f}:ImVec4{.14f,.30f,.06f,.55f};
      ImGui::PushStyleColor(ImGuiCol_ChildBg,tint); ImGui::PushStyleColor(ImGuiCol_Border,ImVec4{1,1,1,.10f});
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{14*s,10*s});
      ImGui::BeginChild(ImGui::GetID(text.c_str()),{width,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
      mark(m,s); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(text.c_str()); ImGui::PopTextWrapPos();
      ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
    };
    auto section=[&](const char* text) {
      ImGui::Dummy({0,4*s}); ImGui::PushFont(visuals->title,21); ImGui::PushStyleColor(ImGuiCol_Text,kAccent);
      ImGui::TextUnformatted(text); ImGui::PopStyleColor(); ImGui::PopFont();
      auto a=ImGui::GetCursorScreenPos(); ImGui::GetWindowDrawList()->AddLine({a.x,a.y-6*s},{a.x+ImGui::GetContentRegionAvail().x,a.y-6*s},ImGui::GetColorU32(ImVec4{1,1,1,.08f}),1);
    };
    // Settings rows: label (and a short explanation) on the left, the control on the right.
    auto rows=[&](const char* id) {
      if(!ImGui::BeginTable(id,2,ImGuiTableFlags_None)) return false;
      ImGui::TableSetupColumn("label",ImGuiTableColumnFlags_WidthFixed,330*s); ImGui::TableSetupColumn("control",ImGuiTableColumnFlags_WidthStretch);
      return true;
    };
    auto row=[&](const char* label,const char* help=nullptr) {
      ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{style.ItemSpacing.x,1*s});
      ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
      if(help) { ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+310*s); muted(help,16); ImGui::PopTextWrapPos(); }
      ImGui::PopStyleVar();
      ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x,420*s));
    };
    // The fold for rare and debug options at the bottom of each settings page (closed until opened).
    auto advanced=[&](const char* id) {
      bool& open=advanced_open[tab]; if(force_advanced) open=true;
      ImGui::Dummy({0,2*s}); ImGui::PushID(id);
      filled({0,0,0,0},{1,1,1,.05f},{1,1,1,.08f}); ImGui::PushStyleColor(ImGuiCol_Text,kMuted); ImGui::PushFont(visuals->title,20);
      auto p=ImGui::GetCursorScreenPos(); float r=5*s, arrow=2*r+8*s;
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{style.FramePadding.x+arrow,style.FramePadding.y});
      if(ImGui::Button(open?"HIDE ADVANCED SETTINGS":"SHOW ADVANCED SETTINGS")) open=!open;
      ImGui::PopStyleVar(); ImGui::PopFont(); ImGui::PopStyleColor(4); ImGui::PopID();
      ImVec2 c{p.x+style.FramePadding.x+r,p.y+ImGui::GetItemRectSize().y*.5f}; ImU32 color=ImGui::GetColorU32(kMuted); auto* dl=ImGui::GetWindowDrawList();
      if(open) dl->AddTriangleFilled({c.x-r,c.y-r*.55f},{c.x+r,c.y-r*.55f},{c.x,c.y+r*.65f},color);
      else dl->AddTriangleFilled({c.x-r*.55f,c.y-r},{c.x-r*.55f,c.y+r},{c.x+r*.65f,c.y},color);
      return open;
    };
    // ---- Header: brand, game switch (combined launcher) and page tabs ----
    bool pending; { std::lock_guard lock(picker.mutex); pending=picker.pending; }
    bool blocked=busy||pending||process.running()||!writable||online.busy;
    ImGui::SetCursorPos({margin,(header_h-ImGui::GetFrameHeight())*.5f});
    ImGui::AlignTextToFramePadding(); ImGui::PushFont(visuals->title,20); ImGui::TextColored(kMuted,"PACIFIC CITY RECOMPILED"); ImGui::PopFont();
    if(kSingleGame<0) {
      ImGui::BeginDisabled(busy||pending||process.running()||online.busy||dialog!=Dialog::none);
      ImGui::SameLine(0,22*s); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{2*s,0}); ImGui::PushFont(visuals->title,20);
      for(int i=0;i<kGameCount;++i) {
        if(i) ImGui::SameLine();
        bool active=i==game_sel;
        if(active) filled(kAccentDeep,kAccent,kAccentHover); else filled({.10f,.13f,.14f,.9f},{.18f,.22f,.23f,1},{.22f,.27f,.28f,1});
        if(ImGui::Button(kGames[i].title)&&!active) switch_game(i);
        ImGui::PopStyleColor(3);
      }
      ImGui::PopFont(); ImGui::PopStyleVar(); ImGui::EndDisabled();
    }
    {
      const char* names[]={"HOME","DISPLAY","GAMEPLAY",gp->archipelago?"SAVES & ARCHIPELAGO":"SAVES","ABOUT"};
      ImGui::PushFont(visuals->title,22);
      float width=0; for(const char* n:names) width+=ImGui::CalcTextSize(n).x+2*style.FramePadding.x+4*s;
      ImGui::SameLine(std::max(ImGui::GetCursorPosX()+20*s,size.x-margin-width));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{4*s,0});
      for(int i=0;i<tab_count;++i) {
        if(i) ImGui::SameLine();
        filled({0,0,0,0},{1,1,1,.06f},{1,1,1,.10f}); ImGui::PushStyleColor(ImGuiCol_Text,i==tab?kText:kMuted);
        if(ImGui::Button(names[i])) { tab=i; focus_page=true; }
        ImGui::PopStyleColor(4);
        if(i==tab) { auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax(); draw->AddRectFilled({a.x+style.FramePadding.x,b.y+2*s},{b.x-style.FramePadding.x,b.y+5*s},ImGui::GetColorU32(kAccent),2*s); }
      }
      ImGui::PopStyleVar(); ImGui::PopFont();
    }
    draw->AddLine({margin,header_h},{size.x-margin,header_h},ImGui::GetColorU32(ImVec4{1,1,1,.08f}),1);
    // ---- Page ----
    ImGui::SetCursorPos({0,header_h+1});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{margin,22*s});
    ImGui::BeginChild("Page",{size.x,size.y-header_h-footer_h-1},ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    auto_focus_now=focus_page; focus_page=false; auto_focus=auto_focus_now;
    const float page_w=ImGui::GetContentRegionAvail().x;
    bool exe_present=fs::is_regular_file(ghome/gp->exe);
    bool tu_missing=gp->title_update&&verified&&!fs::is_regular_file(game/"default.xexp");  // Crackdown 2: required
    bool tu_optional=gp->update_exe&&verified&&!update_exe_ready();                         // Crackdown 1: Title Update #6
    auto install_tu=[&] { package_kind=1; show_dialog(Dialog::package); };
    auto install_dlc=[&] { package_kind=2; show_dialog(Dialog::package); };
    auto messages=[&](float width) {
      if(!error.empty()) { ImGui::Spacing(); notice(Mark::bad,error,width); }
      if(!report.empty()) { ImGui::Spacing(); notice(Mark::ok,report,width); }
      if(!crash.empty()) { ImGui::BeginChild("Startup log",{width,130*s},ImGuiChildFlags_Borders); ImGui::TextUnformatted(crash.c_str()); ImGui::EndChild(); }
    };
    auto progress=[&](float width) {
      { std::lock_guard lock(task.mutex); ImGui::TextWrapped("%s",task.status.c_str()); }
      auto total=task.total.load(),bytes=task.bytes.load();
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,4*s); ImGui::ProgressBar(total?static_cast<float>(double(bytes)/total):-float(SDL_GetTicks()%10000)/1000.f,{width,10*s},""); ImGui::PopStyleVar();
      if(total) { ImGui::PushStyleColor(ImGuiCol_Text,kMuted); ImGui::Text("%.1f / %.1f GB",bytes/1e9,total/1e9); ImGui::PopStyleColor(); }
      if(secondary("Cancel")) task.cancel=true;
    };
    if(tab==0&&setup&&exe_present&&!manifest.empty()) {
      // ---- First-run setup: disc, title update, DLC ----
      bool has_tu=gp->title_update||gp->update_exe;
      int step=!verified?0:(has_tu&&(tu_missing||(tu_optional&&!tu_skipped)))?1:2;
      if(!has_tu&&step==1) step=2;
      if(!gp->dlc&&step==2&&!tu_missing&&!(tu_optional&&!tu_skipped)) setup=false;
      float card_w=std::min(page_w,720*s);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(page_w-card_w)*.5f);
      ImGui::PushStyleColor(ImGuiCol_ChildBg,kPanel); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{36*s,28*s});
      ImGui::BeginChild("Setup",{card_w,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PopStyleVar();
      visuals->logo(64*s,gp->title);
      ImGui::PushFont(visuals->title,20); ImGui::TextColored(kMuted,"SETUP"); ImGui::PopFont();
      // Step tracker.
      {
        const char* labels[]={"Game disc","Title update","DLC"};
        std::vector<int> shown; shown.push_back(0); if(has_tu) shown.push_back(1); if(gp->dlc) shown.push_back(2);
        auto origin=ImGui::GetCursorScreenPos(); float avail=ImGui::GetContentRegionAvail().x, gap=avail/float(shown.size()); float r=13*s;
        auto* dl=ImGui::GetWindowDrawList();
        for(size_t i=0;i<shown.size();++i) {
          int k=shown[i]; ImVec2 c{origin.x+gap*(float(i)+.5f),origin.y+r};
          if(i+1<shown.size()) dl->AddLine({c.x+r+6*s,c.y},{c.x+gap-r-6*s,c.y},ImGui::GetColorU32(k<step?kAccent:ImVec4{1,1,1,.15f}),2*s);
          if(k<step) { dl->AddCircleFilled(c,r,ImGui::GetColorU32(kAccent)); ImVec2 pts[]={{c.x-r*.42f,c.y},{c.x-r*.1f,c.y+r*.33f},{c.x+r*.45f,c.y-r*.33f}}; dl->AddPolyline(pts,3,ImGui::GetColorU32(ImVec4{.03f,.05f,.04f,1}),0,2.4f*s); }
          else if(k==step) { dl->AddCircle(c,r,ImGui::GetColorU32(kAccent),0,2.5f*s); dl->AddCircleFilled(c,r*.42f,ImGui::GetColorU32(kAccent)); }
          else dl->AddCircle(c,r,ImGui::GetColorU32(ImVec4{1,1,1,.25f}),0,2*s);
          ImGui::PushFont(nullptr,17); auto ts=ImGui::CalcTextSize(labels[k]);
          dl->AddText({c.x-ts.x*.5f,c.y+r+6*s},ImGui::GetColorU32(k==step?kText:kMuted),labels[k]); ImGui::PopFont();
        }
        ImGui::Dummy({avail,2*r+34*s});
      }
      ImGui::PushFont(visuals->heading,34);
      ImGui::TextUnformatted(step==0?"Install your game":step==1?(tu_missing?"Install the title update":"Add the title update (optional)"):"Add DLC (optional)");
      ImGui::PopFont();
      ImGui::PushTextWrapPos(0);
      if(busy) progress(ImGui::GetContentRegionAvail().x);
      else if(step==0) {
        ImGui::TextUnformatted(gp->disc_hint); muted("A disc image is unpacked next to the launcher; an extracted folder is used where it is. Every file is checked, and nothing in your source is changed.");
        ImGui::Spacing();
        if(primary("CHOOSE DISC IMAGE",{300,58},!blocked,30)) pick(false,0);
        ImGui::SameLine(); if(secondary("Choose extracted folder",!blocked,{0,58})) pick(true,0);
        if(device=="keyboard") { ImGui::BeginDisabled(blocked); if(link("Use the built-in file browser")) browse_files(false,0); ImGui::EndDisabled(); }
      } else if(step==1) {
        ImGui::TextUnformatted(tu_missing?"Crackdown 2 runs with title update 0.0.5.1. Choose its package (a file named like TU_16L625S_0000004000000.0000000000181, from a console's Content\\0000000000000000\\4D5308BC\\000B0000 folder)."
                                        :"Title Update #6 adds Keys to the City and Street Racing (the Free-For-All Pack needs it). Choose its package: TU_16L61US_0000004000000.00000000001G1, from a console's Content\\0000000000000000\\4D5307DC\\000B0000 folder.");
        ImGui::Spacing();
        if(primary("CHOOSE PACKAGE",{300,58},!blocked,30)) pick(false,1);
        if(!tu_missing) { ImGui::SameLine(); if(secondary("Skip",!blocked,{140,58})) tu_skipped=true; }
      } else {
        ImGui::TextUnformatted("DLC packs (from a console's Content\\0000000000000000\\<title id>\\00000002 folder) are unpacked once into your saves folder, and every profile can use them.");
        for(const auto& pack:dlc_packs) { mark(Mark::ok,s); ImGui::TextUnformatted(pack.title.c_str()); }
        ImGui::Spacing();
        if(primary(dlc_packs.empty()?"CHOOSE DLC PACKAGE":"ADD ANOTHER",{300,58},!blocked,30)) pick(false,2);
        ImGui::SameLine(); if(secondary(dlc_packs.empty()?"Skip and finish":"Finish",!blocked,{180,58})) { setup=false; focus_page=true; }
      }
      ImGui::PopTextWrapPos();
      messages(ImGui::GetContentRegionAvail().x);
      ImGui::EndChild(); ImGui::PopStyleColor();
    } else if(tab==0) {
      // ---- Home: logo, one-line status, PLAY; a status card on the right ----
      const float card_w=380*s, hero_w=std::max(300*s,page_w-card_w-48*s);
      auto top=ImGui::GetCursorPos();
      // Status card.
      {
        ImGui::SetCursorPos({top.x+page_w-card_w,top.y+28*s});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,kPanel); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{22*s,18*s});
        ImGui::BeginChild("Status",{card_w,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PushFont(visuals->title,20); ImGui::TextColored(kMuted,"STATUS"); ImGui::PopFont();
        auto line=[&](Mark m,const char* label,const std::string& value) {
          mark(m,s); ImGui::TextUnformatted(label);
          ImGui::PushFont(visuals->strong,0); auto w=ImGui::CalcTextSize(value.c_str()).x;
          ImGui::SameLine(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowWidth()-22*s-w)); ImGui::TextUnformatted(value.c_str()); ImGui::PopFont();
        };
        line(busy?Mark::warn:verified?Mark::ok:Mark::bad,"Game files",busy?"Checking":verified?"Verified":"Not installed");
        if(gp->title_update) line(!verified?Mark::off:tu_missing?Mark::bad:Mark::ok,"Title update",!verified?"-":tu_missing?"Required":"0.0.5.1");
        if(gp->update_exe) line(update_exe_ready()?Mark::ok:Mark::off,"Title update #6",update_exe_ready()?(use_title_update?"On":"Installed, off"):"Not installed");
        if(gp->dlc) line(dlc_packs.empty()?Mark::off:Mark::ok,"DLC",dlc_packs.empty()?"None":std::to_string(dlc_packs.size())+(dlc_packs.size()==1?" pack":" packs"));
        line(Mark::ok,"Profile",profile.empty()?"Default":profile);
        if(gp->archipelago) { bool on=!profile.empty()&&ap.enabled; line(on?Mark::ok:Mark::off,"Archipelago",on?(ap.slot.empty()?"On":ap.slot):"Off"); }
        if(gp->coop) line(coop.enabled?Mark::ok:Mark::off,"Co-op",coop.enabled?(coop.join?"Player 2":"Player 1 (host)"):"Off");
        ImGui::EndChild(); ImGui::PopStyleColor();
      }
      ImGui::SetCursorPos({top.x,top.y+24*s});
      ImGui::BeginGroup();
      visuals->logo(visuals->has_logo()?150*s:110*s,gp->title);
      // One-line status.
      Mark state=busy?Mark::warn:process.running()?Mark::ok:!exe_present?Mark::bad:tu_missing?Mark::warn:verified?Mark::ok:Mark::bad;
      std::string status=busy?"Checking game files":process.running()?"Playing":!exe_present?"Game program not installed":tu_missing?"Title update needed":verified?"Ready to play":"Game files not installed";
      ImGui::Dummy({0,4*s}); mark(state,s); ImGui::PushFont(visuals->strong,0); ImGui::TextUnformatted(status.c_str()); ImGui::PopFont();
      if(!game_version.empty()) { ImGui::SameLine(); ImGui::TextColored(kMuted,"  %s",game_version.c_str()); }
      ImGui::Dummy({0,8*s});
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+hero_w);
      if(busy) progress(std::min(hero_w,440*s));
      else if(kSingleGame>=0&&!exe_present) {
        primary("PLAY",{360,78},false,46);
        ImGui::TextWrapped("%s is missing from %s. Reinstall or repair the app in the launcher you installed it with.",gp->exe,utf8(ghome).c_str());
      } else if(!launcher_package()&&!exe_present) {
        // This game's runtime comes from its own repository; nothing else works until it is in its folder.
        if(primary("DOWNLOAD GAME",{360,78},!blocked,46)) { show_dialog(Dialog::updates); check_updates(); }
        ImGui::TextWrapped("%s is not in this launcher yet. Its latest release comes from %s into %s; then install your game files.",gp->title,releases_url(*gp->updates).c_str(),utf8(ghome).c_str());
      } else {
        if(tu_missing) { if(primary("INSTALL TITLE UPDATE",{360,78},!blocked,40)) install_tu(); }
        else if(primary(process.running()?"PLAYING":verified?"PLAY":"INSTALL GAME",{360,78},!blocked&&!manifest.empty(),46)) { if(verified) launch(); else show_dialog(Dialog::install); }
        if(update_exe_ready()) {
          ImGui::Dummy({0,2*s}); ImGui::BeginDisabled(blocked);
          if(toggle("##tu",&use_title_update)) {
            try { update_config(preferences,{{"cd1_title_update",use_title_update?std::nullopt:std::optional<std::string>("false")}}); } catch(const std::exception& e) { error=e.what(); }
          }
          ImGui::EndDisabled(); ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Title update #6 (Keys to the City, Street Racing)");
        }
        // A few secondary actions, only those that apply right now.
        if(verified) {
          ImGui::Dummy({0,4*s}); bool first=true;
          auto action=[&](const char* label,bool enabled) { if(!first) ImGui::SameLine(); first=false; return secondary(label,enabled); };
          if(tu_optional&&action("Install title update",!blocked)) install_tu();
          if(gp->dlc&&action("Install DLC",!blocked)) install_dlc();
          if(kSingleGame<0&&action("Check for updates",!online.busy)) { show_dialog(Dialog::updates); check_updates(); }
          if(gp->steam&&action("Add Crackdown to Steam",!blocked)) { try { report=add_to_steam(home/(kSingleGame==0?"CrackdownRecomp.exe":"CrackdownLauncher.exe")); error.clear(); } catch(const std::exception& e) { error=e.what(); } }
        }
      }
      ImGui::PopTextWrapPos();
      messages(std::min(hero_w,560*s));
      ImGui::EndGroup();
    } else {
      // ---- Settings pages: one centered panel ----
      const char* titles[]={"","Display","Gameplay",gp->archipelago?"Saves & Archipelago":"Saves","About"};
      float panel_w=std::min(page_w,960*s);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(page_w-panel_w)*.5f);
      ImGui::PushStyleColor(ImGuiCol_ChildBg,kPanel); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{32*s,22*s});
      ImGui::BeginChild("Panel",{panel_w,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
      ImGui::PopStyleVar();
      ImGui::PushFont(visuals->heading,36); ImGui::TextUnformatted(titles[tab]); ImGui::PopFont();
      if(tab<=2) muted("Changes save automatically and apply the next time you press Play.");
      if(!error.empty()) notice(Mark::bad,error,ImGui::GetContentRegionAvail().x);
      if(tab==1||tab==2) ImGui::BeginDisabled(blocked);
      if(tab==1) {
        section("SCREEN");
        if(rows("screen")) {
          row("Fullscreen","F11 switches in game."); focus_first(!blocked);
          if(toggle("##fullscreen",&fullscreen)) changes["fullscreen"]=fullscreen?std::nullopt:std::optional<std::string>("false");
          row("Resolution","Rendering resolution. Higher needs a faster GPU.");
          int resolution=scale>=1&&scale<=3?scale-1:0;
          if(ImGui::Combo("##resolution",&resolution,"720p (default)\0" "1440p\0" "4K (2160p)\0")) { scale=resolution+1; changes["resolution_scale"]=scale==1?std::nullopt:std::optional<std::string>(std::to_string(scale)); changes["draw_resolution_scale_x"]=std::nullopt; changes["draw_resolution_scale_y"]=std::nullopt; }
          // Aspect ratio: hooks_aspect.cpp in both games, same key.
          row("Aspect ratio","Match an ultrawide or 4:3 display.");
          if(ImGui::Combo("##aspect",&aspect,"16:9 (default)\0" "16:10\0" "21:9 (ultrawide)\0" "32:9 (super ultrawide)\0" "4:3\0")) { const char* values[]={nullptr,"16:10","21:9","32:9","4:3"}; changes["aspect_ratio"]=values[aspect]?std::optional<std::string>(toml_string(values[aspect])):std::nullopt; changes["video_mode_width"]=std::nullopt; changes["video_mode_height"]=std::nullopt; }
          if(game_sel==0) {
            // 120 / 144: the host paces frames above 60 (hooks_framerate.cpp PaceFrame); co-op holds 60 (host_tuning.cpp).
            row("Frame rate","F3 shows it in game.");
            if(ImGui::Combo("##rate",&rate,"60 fps (default)\0" "30 fps (stock)\0" "120 fps (experimental)\0" "144 fps (experimental)\0")) { changes["vsync"]=rate==1?std::optional<std::string>("true"):std::nullopt; changes["fps_cap"]=rate==2?std::optional<std::string>("120"):rate==3?std::optional<std::string>("144"):std::nullopt; }
          } else {
            // Crackdown 2: 60 fps by default (crackdown2.exe sets fps_cap=60 + its measured game clock); 30/120/144 = host cap, vsync stays off.
            row("Frame rate","F3 shows it in game.");
            if(ImGui::Combo("##rate",&rate,"60 fps (default)\0" "30 fps (stock)\0" "120 fps (experimental)\0" "144 fps (experimental)\0")) { const char* caps[]={nullptr,"30","120","144"}; changes["fps_cap"]=caps[rate]?std::optional<std::string>(caps[rate]):std::nullopt; changes["sim_game_clock"]=std::nullopt; changes["vsync"]=std::nullopt; }
          }
          row("Anti-aliasing");
          if(ImGui::Combo("##aa",&aa,"Off (default)\0FXAA\0FXAA (extreme)\0")) changes["swap_post_effect"]=aa==0?std::nullopt:std::optional<std::string>(toml_string(aa==1?"fxaa":"fxaa_extreme"));
          ImGui::EndTable();
        }
        if(advanced("display")&&rows("display-advanced")) {
          row("Window size","When not fullscreen.");
          if(ImGui::Combo("##window",&window_size,"Automatic\0" "1280 x 720\0" "1920 x 1080\0" "2560 x 1440\0")) {
            const int widths[]={0,1280,1920,2560}, heights[]={0,720,1080,1440}; changes["window_width"]=window_size?std::optional<std::string>(std::to_string(widths[window_size])):std::nullopt; changes["window_height"]=window_size?std::optional<std::string>(std::to_string(heights[window_size])):std::nullopt;
          }
          row("Monitor","0 uses the system default.");
          if(ImGui::SliderInt("##monitor",&monitor,0,16)) changes["monitor"]=monitor?std::optional<std::string>(std::to_string(monitor)):std::nullopt;
          row("Texture filtering");
          if(ImGui::Combo("##aniso",&aniso,"Game's own\0Anisotropic 2x\0Anisotropic 4x (default)\0Anisotropic 8x\0Anisotropic 16x\0")) { const char* values[]={"-1","2",nullptr,"4","5"}; changes["anisotropic_override"]=values[aniso]?std::optional<std::string>(values[aniso]):std::nullopt; }
          row("Upscale filter");
#if LAUNCHER_FIDELITYFX
          if(ImGui::Combo("##filter",&filter,"Bilinear\0CAS\0FSR\0")) changes["present_effect"]=filter==0?std::nullopt:std::optional<std::string>(toml_string(filter==1?"cas":"fsr"));
#else
          ImGui::AlignTextToFramePadding(); ImGui::TextColored(kMuted,"Bilinear (this build)");
#endif
          ImGui::EndTable();
        }
      }
      if(tab==2) {
        section("GAME");
        if(rows("game")) {
          row("Language","The disc may not include every language."); focus_first(!blocked);
          int language_index=language-1;
          if(ImGui::Combo("##language",&language_index,"English\0Japanese\0German\0French\0Spanish\0Italian\0Korean\0Chinese (Traditional)\0Portuguese\0Chinese (Simplified)\0")) { language=language_index+1; changes["user_language"]=language==1?std::nullopt:std::optional<std::string>(std::to_string(language)); }
          row("Skip intro logos");
          if(toggle("##intro",&skip_intro)) changes["skip_intro_videos"]=skip_intro?std::nullopt:std::optional<std::string>("false");
          ImGui::EndTable();
        }
        section("CONTROLS");
        if(rows("controls")) {
          row("Camera turn speed","Right stick or mouse. 1.00x is the original speed.");
          if(ImGui::SliderFloat("##camera",&camera_speed,0.25f,4.0f,"%.2fx")) {
            char text[16]; std::snprintf(text,sizeof(text),"%.2f",camera_speed);
            changes["camera_sensitivity"]=std::abs(camera_speed-1.0f)<0.005f?std::nullopt:std::optional<std::string>(text);
          }
          row("Mouse and keyboard","Play with WASD and the mouse instead of a controller.");
          if(toggle("##mnk",&mnk)) changes["mnk_mode"]=mnk?std::optional<std::string>("true"):std::nullopt;
          if(mnk) {
            row("Mouse sensitivity");
            if(ImGui::SliderFloat("##mouse",&mouse_sens,0.1f,5.0f,"%.2f")) {
              char text[16]; std::snprintf(text,sizeof(text),"%.2f",mouse_sens);
              changes["mnk_sensitivity"]=std::abs(mouse_sens-1.0f)<0.005f?std::nullopt:std::optional<std::string>(text);
            }
          }
          ImGui::EndTable();
        }
        if(mnk) muted("WASD moves, the mouse aims, left click fires, right click locks on, Space jumps (A), Backspace is B, L is X, P is Y, 1 / 3 are the bumpers, Enter or X is Start, Tab is Back, Shift+arrows is the D-pad.");
        if(gp->coop) {
          section("CO-OP (SYSTEM LINK, EXPERIMENTAL)");
          bool coop_edit=false;
          if(rows("coop")) {
            row("Play co-op with another PC","Both players need the same game version. Co-op runs at 30 fps.");
            coop_edit=toggle("##coop",&coop.enabled);
            if(coop_edit&&coop.enabled&&!coop_own[0]&&!coop_addresses.empty()) std::snprintf(coop_own,sizeof(coop_own),"%s",coop_addresses.front().c_str());
            if(coop.enabled) {
              int role=coop.join?1:0;
              row("Role",coop.join?"Player 2 has a separate save, which starts fresh (see SETUP.md to copy yours).":"Player 1 keeps their normal save.");
              if(ImGui::Combo("##role",&role,"Player 1 (hosts)\0Player 2 (joins)\0")) { coop.join=role==1; coop_edit=true; }
              row("Your address","Over the internet, use Tailscale addresses (100.x.y.z).");
              coop_edit|=ImGui::InputTextWithHint("##own",coop_addresses.empty()?"e.g. 192.168.1.20":coop_addresses.front().c_str(),coop_own,sizeof(coop_own));
              for(const auto& address:coop_addresses) { if(ImGui::SmallButton(address.c_str())) { std::snprintf(coop_own,sizeof(coop_own),"%s",address.c_str()); coop_edit=true; } ImGui::SameLine(); }
              ImGui::NewLine();
              row("Other player's address");
              coop_edit|=ImGui::InputTextWithHint("##partner","e.g. 192.168.1.21",coop_partner,sizeof(coop_partner));
            }
            ImGui::EndTable();
          }
          if(coop.enabled) muted("In the game: Campaign > Play Now - Co-op.");
          if(coop_edit) {
            coop.own_address=coop_own; coop.partner_address=coop_partner;
            try { for(auto& [key,v]:coop_changes(coop)) changes[key]=v; coop_error.clear(); } catch(const std::exception& e) { coop_error=e.what(); }
          }
          if(coop.enabled&&!coop_error.empty()) notice(Mark::bad,coop_error,ImGui::GetContentRegionAvail().x);
        }
        if(advanced("gameplay")) {
          if(rows("gameplay-advanced")) {
            row("Mute audio");
            if(toggle("##mute",&mute)) changes["audio_mute"]=mute?std::optional<std::string>("true"):std::nullopt;
            row("Close the launcher after Play");
            if(toggle("##close",&close_on_play)) {
              try { update_config(preferences,{{"close_on_play",close_on_play?std::optional<std::string>("true"):std::nullopt}}); } catch(const std::exception& e) { error=e.what(); }
            }
            row("Input backend"); ImGui::AlignTextToFramePadding(); ImGui::TextColored(kMuted,"%s",input_backend.c_str());
            ImGui::EndTable();
          }
          muted("Launcher controls: D-pad or left stick moves, the confirm button selects, the back button returns, and the shoulder buttons switch pages. Keyboard: arrows, Enter, Esc, Q / E.");
        }
      }
      if(tab==1||tab==2) ImGui::EndDisabled();
      if(tab==tab_saves) {
        // A room link from the Archipelago Launcher comes first: it only needs a profile to go to.
        if(gp->archipelago&&ap_link) {
          bool named=!user.empty()&&!profile.empty();
          notice(named?Mark::ok:Mark::warn,"Archipelago room link: slot \""+ap_link->slot+"\" on "+ap_link->server+"."+(named?" It will be saved to the profile \""+profile+"\".":" Choose or create the save profile for this seed below, then use the link."),ImGui::GetContentRegionAvail().x);
          ImGui::BeginDisabled(blocked||!named);
          if(primary("USE THIS ROOM LINK",{300,52},!blocked&&named,26)) {
            if(ap_link->server.size()>=sizeof(ap_server)||ap_link->slot.size()>=sizeof(ap_slot)||ap_link->password.size()>=sizeof(ap_password))
              error="The room link's server, slot name or password is too long";
            else {
              ap.enabled=true;
              std::snprintf(ap_server,sizeof(ap_server),"%s",ap_link->server.c_str()); std::snprintf(ap_slot,sizeof(ap_slot),"%s",ap_link->slot.c_str());
              std::snprintf(ap_password,sizeof(ap_password),"%s",ap_link->password.c_str());
              try { save_ap(); ap_link.reset(); error.clear(); } catch(const std::exception& e) { error=e.what(); }
            }
          }
          ImGui::EndDisabled();
        }
        ImGui::BeginDisabled(blocked||user.empty());
        section("SAVE PROFILE");
        if(rows("profile")) {
          row("Profile","Each profile has its own save. A new one starts with New Game."); focus_first(!blocked&&!user.empty());
          if(ImGui::BeginCombo("##profile",profile.empty()?"Default (existing saves)":profile.c_str())) {
            try { if(ImGui::Selectable("Default (existing saves)",profile.empty())) save_profile(""); for(const auto& name:profiles) if(ImGui::Selectable(name.c_str(),name==profile)) save_profile(name); }
            catch(const std::exception& e) { error=e.what(); } ImGui::EndCombo();
          }
          row("New profile");
          ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x,420*s)-ImGui::CalcTextSize("Create").x-2*style.FramePadding.x-style.ItemSpacing.x);
          ImGui::InputTextWithHint("##newprofile","Name",new_profile,sizeof(new_profile)); ImGui::SameLine();
          if(secondary("Create",new_profile[0]!=0)) {
            try { create_profile(user,new_profile); profiles=list_profiles(user); save_profile(new_profile); new_profile[0]=0; error.clear(); } catch(const std::exception& e) { error=e.what(); }
          }
          if(link("Type with the controller")) { keyboard={new_profile,sizeof(new_profile),"PROFILE NAME"}; show_dialog(Dialog::keyboard); }
          row("Saves folder");
          if(secondary("Open saves folder")) {
            try { (void)profile_args(user,profile); auto dir=profile.empty()?user:user/"profiles"/fs::u8path(profile); fs::create_directories(dir); show_folder(dir); } catch(const std::exception& e) { error=e.what(); }
          }
          ImGui::EndTable();
        }
        ImGui::EndDisabled();
        if(gp->archipelago) {
          section("ARCHIPELAGO");
          bool named=!user.empty()&&!profile.empty();
          if(!named&&!ap_link) notice(Mark::warn,"Archipelago needs its own save profile, one per seed. Create or choose one above.",ImGui::GetContentRegionAvail().x);
          ImGui::BeginDisabled(blocked||!named);
          bool edited=false;
          if(rows("archipelago")) {
            row("Connect when this profile plays");
            edited|=toggle("##ap",&ap.enabled);
            auto field=[&](const char* label,const char* hint,char* text,size_t cap,bool secret,const char* id) {
              row(label); ImGui::PushID(id);
              ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x,420*s)-ImGui::CalcTextSize("Keyboard").x-2*style.FramePadding.x-style.ItemSpacing.x);
              ImGui::InputTextWithHint("##field",hint,text,cap,secret?ImGuiInputTextFlags_Password:0); edited|=ImGui::IsItemDeactivatedAfterEdit();
              ImGui::SameLine(); if(secondary("Keyboard")) { keyboard={text,cap,label,secret,true}; show_dialog(Dialog::keyboard); }
              ImGui::PopID();
            };
            field("Server","archipelago.gg:38281",ap_server,sizeof(ap_server),false,"server");
            field("Slot name","Your player name in the seed",ap_slot,sizeof(ap_slot),false,"slot");
            field("Password","Leave empty if the room has none",ap_password,sizeof(ap_password),true,"password");
            ImGui::EndTable();
          }
          if(edited) { try { save_ap(); error.clear(); } catch(const std::exception& e) { error=e.what(); } }
          ImGui::EndDisabled();
          if(!ap_link&&named) {
            if(!ap_bound.empty()) muted(("This profile is bound to "+ap_bound+". Use a new profile for a different seed.").c_str());
            else muted(game_sel==0?"Not bound to a seed yet. Press Play, then choose Campaign > Play Now (New Game) while connected: that binds this profile to the seed."
                                  :"Not bound to a seed yet. Press Play, then start a New Game while connected: that binds this profile's save to the seed.");
          }
          if(game_sel==0) muted("Preview: the orbs around Pacific City Bank are checks, and Progressive Agility is the only item.");
          muted("The password is stored only in this profile's folder and is not included in system info.");
        }
        if(gp->dlc) {
          section("DOWNLOADABLE CONTENT");
          if(dlc_packs.empty()) muted("No DLC packs installed.");
          for(const auto& pack:dlc_packs) { mark(Mark::ok,s); ImGui::TextUnformatted(pack.title.c_str()); }
          if(secondary("Install DLC pack...",!blocked)) install_dlc();
        }
        if(advanced("saves")) {
          ImGui::BeginDisabled(blocked||user.empty());
          if(rows("saves-advanced")) {
            row("Keep saves with the game (portable)","Copies your saves next to the game and uses them from there. Nothing is deleted.");
            if(toggle("##portable",&portable)) {
              // Copies, never moves: the other location keeps its saves, and switching back finds them.
              const bool want=portable; bool ok=true;
              auto documents=fs::absolute(default_user_folder(gp->user_folder)).lexically_normal(), local=ghome/"userdata";
              try {
                if(want) {
                  if(!fs::exists(local)) {
                    // The first copy goes to a staging folder that becomes userdata only once complete, so a
                    // failed copy never passes for a finished one on the next try.
                    auto staging=ghome/"userdata.partial";
                    fs::remove_all(staging); fs::create_directories(staging);
                    if(fs::is_directory(documents)) for(const auto& item:fs::directory_iterator(documents)) {
                      auto name=item.path().filename();
                      if(name=="cache"||name=="logs") continue;  // rebuilt on their own
                      fs::copy(item.path(),staging/name,fs::copy_options::recursive|fs::copy_options::copy_symlinks);
                    }
                    fs::rename(staging,local);
                  }
                  std::ofstream marker(ghome/"portable.txt",std::ios::binary|std::ios::trunc);
                  marker<<"Saves are in userdata\\ next to this file. Delete this file to use Documents\\"<<gp->user_folder<<" again.\n";
                  marker.close(); if(!marker) throw std::runtime_error("Cannot write portable.txt next to the launcher");
                } else fs::remove(ghome/"portable.txt");
              } catch(const std::exception& e) { ok=false; error=e.what(); }
              // Mode, save root, profile and its Archipelago settings all follow the marker on disk, so
              // they stay consistent whichever step failed; the profile state is reloaded from the new root
              // even when a profile of the same name exists in both.
              std::error_code marker_ec; portable=fs::is_regular_file(ghome/"portable.txt",marker_ec);
              user=fs::absolute(portable?local:documents).lexically_normal();
              try { profiles=list_profiles(user); } catch(const std::exception& e) { profiles.clear(); ok=false; error=e.what(); }
              if(std::find(profiles.begin(),profiles.end(),profile)==profiles.end()) {
                profile.clear();
                try { update_config(preferences,{{gp->profile_key,std::nullopt}}); } catch(const std::exception& e) { ok=false; error=e.what(); }
              }
              load_ap();
              if(gp->dlc) dlc_packs=list_content_dlc(user,gp->title_id);
              if(ok) error.clear();
            }
            ImGui::EndTable();
          }
          ImGui::EndDisabled();
          muted((std::string("Saves are in ")+utf8(user)+(portable?". Turning portable off goes back to Documents\\"+std::string(gp->user_folder)+".":". Profiles share the shader cache.")).c_str());
        }
      }
      if(tab==tab_about) {
        section("VERSION");
        if(rows("version")) {
          row("Game release"); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(game_version.empty()?"not installed":game_version.c_str());
          row("Launcher"); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(LAUNCHER_VERSION);
          if(kSingleGame<0) { row("Updates from"); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(releases_url(*gp->updates).c_str()); }
          ImGui::EndTable();
        }
        if(kSingleGame<0) {
          section("UPDATES");
          ImGui::BeginDisabled(online.busy||!writable);
          if(rows("updates")) {
            row("Check when the launcher opens"); focus_first(!online.busy&&writable);
            bool proposed=auto_updates;
            if(toggle("##auto",&proposed)) {
              try { update_config(preferences,{{"auto_updates",proposed?"true":"false"},{"update_consent_asked","true"}}); auto_updates=proposed; if(auto_updates) check_updates(); } catch(const std::exception& e) { error=e.what(); }
            }
            row("Include prereleases",launcher_package()?nullptr:"Crackdown 2 always lists them.");
            proposed=beta_channel;
            if(toggle("##beta",&proposed)) { try { update_config(preferences,{{"beta_updates",proposed?"true":"false"}}); beta_channel=proposed; } catch(const std::exception& e) { error=e.what(); } }
            ImGui::EndTable();
          }
          ImGui::EndDisabled();
          if(secondary("Version history and updates")) show_dialog(Dialog::updates);
        }
        section("GAME FILES");
        ImGui::BeginDisabled(blocked||manifest.empty());
        if(secondary("Verify game files",fs::is_directory(game))) { visuals->clear_art(); begin(game,Job::full_check); busy=true; error.clear(); tab=0; focus_page=true; }
        ImGui::SameLine(); if(secondary("Change game source...")) show_dialog(Dialog::install);
        if(!leftovers.empty()) {
          ImGui::SameLine();
          if(secondary("Delete unfinished import")) {
            leftovers=unfinished_imports(ghome,game);
            for(const auto& dir:leftovers) { std::error_code ec; fs::remove_all(dir,ec); if(ec) { error="Cannot delete "+utf8(dir)+": "+ec.message(); break; } }
            leftovers=unfinished_imports(ghome,game);
          }
        }
        ImGui::EndDisabled();
        if(!leftovers.empty()) muted((std::to_string(leftovers.size())+" unfinished import(s) can be removed.").c_str());
        section("SUPPORT");
        if(secondary("Open logs folder")) { try { fs::create_directories(ghome/"logs"); show_folder(ghome/"logs"); } catch(const std::exception& e) { error=e.what(); } }
        ImGui::SameLine(); if(secondary("Copy system info")) {
          try { report="Launcher: " LAUNCHER_VERSION "\n"+system_info()+"Game XEX SHA256: "+hash+"\nSave profile: "+(profile.empty()?"default":profile)+"\ncrackdown.toml:\n"+redacted_config(config); if(!SDL_SetClipboardText(report.c_str())) error=SDL_GetError(); else report="System info copied to the clipboard."; }
          catch(const std::exception& e) { error=e.what(); }
        }
        ImGui::SameLine(); if(secondary("Licenses")) { try { show_folder(home/"LICENSES"); } catch(const std::exception& e) { error=e.what(); } }
        ImGui::SameLine(); if(secondary("Project page",online_authorized||kSingleGame>=0)) { auto url=project_url(*gp->updates); if(!SDL_OpenURL(url.c_str())) error=SDL_GetError(); }
        muted("Built with SDL3, Dear ImGui, toml++ and stb_image. Barlow fonts: SIL OFL 1.1. Controller prompts: CC0. Game art comes from your own verified files.");
        if(advanced("about")) muted(visuals->art_status.empty()?"Game art: loaded from your verified files.":("Game art fallback: "+visuals->art_status).c_str());
      }
      ImGui::EndChild(); ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    // ---- Footer: status on the left, controller / keyboard hints on the right ----
    draw->AddLine({margin,size.y-footer_h},{size.x-margin,size.y-footer_h},ImGui::GetColorU32(ImVec4{1,1,1,.08f}),1);
    ImGui::SetCursorPos({margin,size.y-footer_h+(footer_h-ImGui::GetTextLineHeight())*.5f});
    if(online.busy) { ImGui::TextColored(kMuted,"%s",online.install?"Installing update...":"Checking for updates..."); }
    else ImGui::TextColored(kMuted,"Launcher %s",LAUNCHER_VERSION);
    ImGui::SameLine(); ImGui::SetCursorPosY(size.y-footer_h+(footer_h-28*s)*.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{margin,0}); visuals->hints(device,ui_scale); ImGui::PopStyleVar();
    ImGui::End();
    // Settings save on their own once no control is being dragged or typed in.
    if(!changes.empty()&&writable&&!ImGui::IsAnyItemActive()&&!process.running()) save_settings();
    auto dialog_title=[&](const char* text) { ImGui::PushFont(visuals->heading,32); ImGui::TextUnformatted(text); ImGui::PopFont(); };
    if(ask_updates) ImGui::OpenPopup("Update preferences");
    ImGui::SetNextWindowSize({620*s,0},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{30*s,24*s});
    ImGui::SetNextWindowPos({size.x*.5f,size.y*.5f},ImGuiCond_Always,{.5f,.5f});
    if(ImGui::BeginPopupModal("Update preferences",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoTitleBar)) {
      dialog_title("Check for updates?");
      ImGui::TextWrapped("The launcher can look for new releases on GitHub when it opens. Nothing is requested until you allow it, and you can always check by hand.");
      muted(("Sources: "+releases_url(cd1_updates)+" (Crackdown and this launcher) and "+releases_url(cd2_updates)+" (Crackdown 2).").c_str());
      auto consent=[&](bool enabled) {
        try { update_config(preferences,{{"auto_updates",enabled?"true":"false"},{"update_consent_asked","true"}}); auto_updates=enabled; ask_updates=false; ImGui::CloseCurrentPopup(); focus_page=true; if(enabled) check_updates(); }
        catch(const std::exception& e) { error=e.what(); }
      };
      ImGui::Spacing();
      if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::BeginDisabled(!writable);
      bool yes=primary("CHECK FOR UPDATES",{260,50},writable,24); ImGui::SameLine(); bool no=secondary("Not now",writable,{0,50});
      if(yes) consent(true); if(no||back) consent(false);
      ImGui::EndDisabled();
      if(!writable && secondary("Continue with checks off")) { ask_updates=false; auto_updates=false; ImGui::CloseCurrentPopup(); focus_page=true; }
      if(!error.empty()) ImGui::TextWrapped("%s",error.c_str()); ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    if(open_dialog) { ImGui::OpenPopup("Launcher action"); open_dialog=false; }
    ImGui::SetNextWindowSize({std::min(size.x-48*s,780*s),dialog==Dialog::browser||dialog==Dialog::updates?std::min(size.y-80*s,580*s):0.f},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{30*s,24*s});
    ImGui::SetNextWindowPos({size.x*.5f,size.y*.5f},ImGuiCond_Always,{.5f,.5f});
    if(ImGui::BeginPopupModal("Launcher action",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize)) {
      bool child_popup=ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId);
      auto close_dialog=[&] { ImGui::CloseCurrentPopup(); dialog=Dialog::none; focus_page=true; };
      auto modal_focus=[&] { if(dialog_focus) { ImGui::SetKeyboardFocusHere(); dialog_focus=false; } };
      if(dialog==Dialog::install) {
        dialog_title("Install game"); ImGui::TextWrapped("%s",gp->disc_hint); muted("Your source files are kept in place.");
        ImGui::Spacing(); modal_focus();
        auto choose=[&](bool folder) { if(device=="keyboard") { native_pick(folder,0); close_dialog(); } else browse_files(folder,0); };
        if(primary("DISC IMAGE (ISO)",{260,52},true,26)) choose(false);
        ImGui::SameLine(); if(secondary("Extracted folder",true,{0,52})) choose(true);
        if(device=="keyboard"&&link("Use the built-in file browser")) browse_files(false,0);
      } else if(dialog==Dialog::browser) {
        dialog_title(package_kind==1?"Select title update package":package_kind==2?"Select DLC package":folder_browser?"Select game folder":"Select disc image");
        muted(utf8(browse).c_str()); modal_focus();
        ImGui::SetNextItemWidth(220*s);
        if(ImGui::BeginCombo("##location","Go to drive")) {
          for(const auto& root:browse_roots) if(ImGui::Selectable(utf8(root).c_str())) { browse=root; refresh_browser(); dialog_focus=true; } ImGui::EndCombo();
        }
        ImGui::SameLine(); if(secondary("Up one folder")) { if(browse.has_parent_path()) { browse=browse.parent_path(); refresh_browser(); dialog_focus=true; } }
        if(folder_browser) { ImGui::SameLine(); if(primary("USE THIS FOLDER",{220,0},true,22)) { visuals->clear_art(); begin(browse,Job::full_check); busy=true; tab=0; close_dialog(); } }
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4{0,0,0,.25f});
        ImGui::BeginChild("Files",{0,-64*s},ImGuiChildFlags_Borders);
        std::optional<fs::path> chosen; bool directory=false;
        for(size_t i=0;i<browse_entries.size();++i) {
          const auto& e=browse_entries[i]; std::error_code ec; bool dir=e.is_directory(ec); auto name=utf8(e.path().filename()); ImGui::PushID(static_cast<int>(i));
          if(dir) ImGui::PushStyleColor(ImGuiCol_Text,kAccent);
          if(ImGui::Selectable((dir?name+"\\":name).c_str())) { chosen=e.path(); directory=dir; }
          if(dir) ImGui::PopStyleColor();
          ImGui::PopID();
        }
        if(browse_entries.empty()) muted(folder_browser?"No folders here.":package_kind?"No files here.":"No disc images (.iso) here.");
        ImGui::EndChild(); ImGui::PopStyleColor();
        if(chosen) {
          if(directory) { browse=*chosen; refresh_browser(); dialog_focus=true; }
          else if(package_kind) { begin(*chosen,package_kind==1?Job::title_update:Job::dlc_pack); busy=true; error.clear(); report.clear(); if(!setup) tab=0; close_dialog(); }
          else { visuals->clear_art(); begin(*chosen,Job::import_iso); busy=true; tab=0; close_dialog(); }
        }
      } else if(dialog==Dialog::keyboard) {
        auto& k=keyboard; auto n=std::char_traits<char>::length(k.text);
        dialog_title(k.title);
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4{0,0,0,.3f}); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{14*s,10*s});
        ImGui::BeginChild("Typed",{0,ImGui::GetTextLineHeight()+20*s},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("%s_",k.secret?std::string(n,'*').c_str():k.text); ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
        modal_focus();
        auto type=[&](char c) { if(n<k.size-1) { k.text[n]=c; k.text[++n]=0; } };
        const char* keys=k.ap?"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.:/@!#$%&*+=?~":"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.";
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{6*s,6*s});
        for(size_t i=0;keys[i];++i) { if(i%13) ImGui::SameLine(); char label[]={keys[i],0}; ImGui::PushID(static_cast<int>(i)); if(ImGui::Button(label,{46*s,40*s})) type(keys[i]); ImGui::PopID(); }
        ImGui::PopStyleVar();
        if(secondary("Space",true,{120,0})) type(' '); ImGui::SameLine();
        if(secondary("Backspace",true,{140,0})) { if(n) k.text[--n]=0; } ImGui::SameLine();
        if(primary("DONE",{140,0},true,22)) {
          if(k.ap) { try { save_ap(); error.clear(); } catch(const std::exception& e) { error=e.what(); } }
          close_dialog();
        }
      } else if(dialog==Dialog::updates) {
        auto installed=current_version();
        dialog_title((std::string(gp->title)+" updates").c_str()); muted(("From "+releases_url(*gp->updates)+". Installed: "+(installed.empty()?"none":installed)).c_str());
        modal_focus();
        if(online.busy) {
          ImGui::TextUnformatted(online.install?"Downloading, verifying and staging the update...":"Checking releases..."); ImGui::ProgressBar(-float(SDL_GetTicks()%10000)/1000.f,{-FLT_MIN,10*s},"");
          if(secondary("Cancel")) online.cancel=true;
        } else {
          if(secondary("Check again")) check_updates(); ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(online.message.c_str());
          ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4{0,0,0,.25f});
          ImGui::BeginChild("Release history",{0,-64*s},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
          for(size_t i=0;i<online.releases.size();++i) {
            const auto& r=online.releases[i]; ImGui::PushID(static_cast<int>(i));
            ImGui::PushFont(visuals->title,24); ImGui::TextUnformatted(r.tag.c_str()); ImGui::PopFont();
            if(r.prerelease) { ImGui::SameLine(); ImGui::TextColored(kWarn,"prerelease"); }
            bool newer=installed.empty()||compare_versions(r.tag,installed)>0;
            if(newer) {
              ImGui::SameLine(std::max(ImGui::GetCursorPosX(),ImGui::GetContentRegionAvail().x+ImGui::GetCursorPosX()-200*s));
              if(primary(installed.empty()?"INSTALL":"UPDATE",{180,0},!(r.zip_url.empty()||r.sha_url.empty()||blocked||online.game!=game_sel),20)) install_update(r);
            }
            muted(r.notes.c_str()); ImGui::Separator(); ImGui::PopID();
            if(online.busy) break;
          }
          ImGui::EndChild(); ImGui::PopStyleColor();
        }
      } else if(dialog==Dialog::package) {
        dialog_title(package_kind==1?"Install title update":"Install DLC");
        ImGui::TextWrapped(package_kind==1
          ?(gp->update_exe
              ?"Select %s's Title Update #6 package (TU_16L61US_0000004000000.00000000001G1, from a console's Content\\0000000000000000\\4D5307DC\\000B0000 folder). The launcher checks it and unpacks it into tu\\; Play then starts the title update build, which the Free-For-All Pack (Keys to the City, Street Racing) needs."
              :"Select %s's title update package (a file named like TU_16L625S_0000004000000.0000000000181, from a console's Content\\0000000000000000\\<title id>\\000B0000 folder). The launcher checks it, puts default.xexp next to the game's default.xex and unpacks the rest into tu\\.")
          :"Select a %s DLC package (from a console's Content\\0000000000000000\\<title id>\\00000002 folder; keep its original file name). It is unpacked once into your save folder and every profile can use it.",gp->title);
        muted("The package itself is kept.");
        ImGui::Spacing(); modal_focus();
        if(primary("CHOOSE PACKAGE",{260,52},true,26)) { if(device=="keyboard") { native_pick(false,package_kind); close_dialog(); } else browse_files(false,package_kind); }
        if(device=="keyboard") { ImGui::SameLine(); if(secondary("Built-in browser",true,{0,52})) browse_files(false,package_kind); }
      } else if(dialog==Dialog::save_warning) {
        dialog_title("Title update save");
        ImGui::TextWrapped("This profile's save was made by the title update build, and Play is about to start the disc build (%s). The disc build can't read it: it will say \"Save Game Damaged\" and ask whether to overwrite it. Answering Yes there replaces your title update progress with a new game.",
          !update_exe_ready()?"the title update isn't installed":!ap_args(profile.empty()?ApConnection{}:ap).empty()?"Archipelago runs on the disc build":"the title update is turned off");
        for(const auto& save:disc_unreadable) muted(utf8(save).c_str());
        ImGui::TextWrapped("To keep it, go back and turn the title update on, or play another profile.");
        if(secondary("Play the disc build anyway")) { close_dialog(); disc_confirmed=true; launch(); }
      } else if(dialog==Dialog::quit) {
        dialog_title("Quit the launcher?"); if(!changes.empty()) ImGui::TextWrapped("Some game settings are not saved yet."); modal_focus();
        if(primary("QUIT",{180,50},true,24)) { quit=true; close_dialog(); }
        if(!changes.empty()) { ImGui::SameLine(); if(secondary("Save settings and quit",true,{0,50})) { save_settings(); if(changes.empty()) { quit=true; close_dialog(); } } }
      }
      if(dialog!=Dialog::none) {
        if(dialog==Dialog::save_warning) modal_focus();
        if(dialog==Dialog::quit||dialog==Dialog::keyboard) ImGui::SameLine(); else ImGui::Spacing();
        if(secondary("Back",true,{0,dialog==Dialog::quit?50.f:0.f}) || (back&&!child_popup&&!modal_child_before&&!editing_before)) {
          if(dialog==Dialog::browser&&browser_back!=Dialog::none) { dialog=browser_back; dialog_focus=true; }
          else close_dialog();
        }
      }
      if(!error.empty()) { ImGui::Spacing(); notice(Mark::bad,error,ImGui::GetContentRegionAvail().x); }
      modal_child_open=ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId); ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::Render(); if(review_target) SDL_SetRenderTarget(renderer,review_target);
    SDL_SetRenderDrawColor(renderer,8,14,20,255); SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
    if(review&&!busy && ((smoke_frame<=d&&smoke_frame%2==0)||(smoke_frame>d&&smoke_frame<=d+20&&(smoke_frame-d)%4==0))) {
      auto* surface=SDL_RenderReadPixels(renderer,nullptr);
      auto file=review_dir/("frame-"+std::to_string(smoke_frame)+".bmp");
      if(!surface||!SDL_SaveBMP(surface,utf8(file).c_str())) { std::cerr<<SDL_GetError()<<'\n'; review_failed=true; quit=true; }
      if(surface) SDL_DestroySurface(surface);
    }
    SDL_RenderPresent(renderer);
    if(smoke&&(!review||!busy) && --smoke==0) quit=true;
  }
  task.cancel=true; if(task.thread.joinable()) task.thread.join();
  online.cancel=true; if(online.worker.joinable()) online.worker.join();
  for(;;) { { std::lock_guard lock(picker.mutex); if(!picker.pending) break; } SDL_PumpEvents(); SDL_Delay(10); }
  visuals.reset();
  ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return review_failed?1:0;
}

#pragma once
#include "v2.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <map>
namespace launcher {
class Visuals {
 public:
  Visuals(SDL_Renderer* renderer,const fs::path& home);
  ~Visuals();
  Visuals(const Visuals&)=delete;
  void load_art(const fs::path& game,const fs::path& user,const std::vector<Entry>& manifest);
  void clear_art();
  void backdrop(ImVec2 size);
  // The game's logo from its verified files, height in pixels; without art, the title in the heading font.
  void logo(float height,const char* fallback);
  bool has_logo() const { return logo_!=nullptr; }
  void hints(const std::string& device,float scale);
  ImFont* heading=nullptr;
  ImFont* body=nullptr;
  ImFont* title=nullptr;   // section titles and page tabs (Barlow Condensed SemiBold)
  ImFont* strong=nullptr;  // emphasis in body text (Barlow SemiBold)
  std::string art_status;
 private:
  SDL_Renderer* renderer_;
  fs::path home_;
  SDL_Texture* background_=nullptr; SDL_Texture* logo_=nullptr; SDL_Texture* agency_=nullptr;
  std::map<std::string,SDL_Texture*> glyphs_;
  SDL_Texture* decode(std::span<const unsigned char> png,const fs::path& cache={},bool clear_black=false);
  SDL_Texture* rgba(std::span<const unsigned char> bytes,int w,int h);
};
}

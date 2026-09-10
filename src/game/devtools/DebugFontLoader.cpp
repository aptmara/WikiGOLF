#include "DebugFontLoader.h"

#include "imgui.h"
#include <filesystem>

namespace game::debug {

ImFont *LoadJapaneseFont(ImFontAtlas &atlas, const char *fontPath) {
  if (!fontPath || !std::filesystem::is_regular_file(fontPath)) {
    return nullptr;
  }
  return atlas.AddFontFromFileTTF(fontPath, kJapaneseFontSize);
}

} // namespace game::debug

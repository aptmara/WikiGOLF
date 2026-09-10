#include "src/game/devtools/DebugFontLoader.h"
#include "imgui.h"
#include <iostream>

#ifndef WIKIGOLF_TEST_FONT_PATH
#error WIKIGOLF_TEST_FONT_PATH must be defined
#endif

int main() {
  ImGui::CreateContext();
  ImFont *font = game::debug::LoadJapaneseFont(
      *ImGui::GetIO().Fonts, WIKIGOLF_TEST_FONT_PATH);
  if (!font) {
    std::cerr << "Failed to load the configured Japanese font\n";
    ImGui::DestroyContext();
    return 1;
  }
  if (!font->IsGlyphInFont(0x65E5) || !font->IsGlyphInFont(0x672C) ||
      !font->IsGlyphInFont(0x8A9E)) {
    std::cerr << "The configured font does not contain Japanese glyphs\n";
    ImGui::DestroyContext();
    return 1;
  }
  ImGui::DestroyContext();
  return 0;
}

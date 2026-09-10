#pragma once

struct ImFont;
struct ImFontAtlas;

namespace game::debug {

inline constexpr char kJapaneseFontPath[] =
    "Assets/Fonts/Mamelon-5-Hi-Regular.otf";
inline constexpr float kJapaneseFontSize = 18.0f;

ImFont *LoadJapaneseFont(ImFontAtlas &atlas, const char *fontPath);

} // namespace game::debug

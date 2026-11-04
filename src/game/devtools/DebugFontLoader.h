#pragma once

struct ImFont;
struct ImFontAtlas;

namespace game::debug {

inline constexpr char kJapaneseFontPath[] =
    "Assets/Fonts/Kiwi_Maru/KiwiMaru-Medium.ttf";
inline constexpr float kJapaneseFontSize = 18.0f;

ImFont *LoadJapaneseFont(ImFontAtlas &atlas, const char *fontPath);

} // namespace game::debug

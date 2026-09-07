/**
 * @file HudStyles.cpp
 * @brief WikiGolf HUDで共有する外観規則の実装
*/

#include "HudStyles.h"
#include "../../../graphics/TextStyle.h"
#include "../../utils/UIConstants.h"

namespace game::controllers::hud {

void ApplySurfaceStyle(graphics::TextStyle &style) {
  ApplySurfaceStyle(style, game::ui::kRadiusPanel);
}

void ApplySurfaceStyle(graphics::TextStyle &style, float radius) {
  style.bgColor = game::ui::kColorBgDark;
  style.useGradient = false;
  style.bgGradientEnd = {0.0f, 0.0f, 0.0f, 0.0f};
  style.cornerRadius = radius;
  style.borderWidth = game::ui::kBorderWidthThin;
  style.borderColor = game::ui::kColorBorder;
  style.hasShadow = true;
  style.shadowColor = game::ui::kShadowColor;
  style.shadowOffsetX = 0.0f;
  style.shadowOffsetY = game::ui::kShadowOffsetY;
}

void ApplyActiveRowStyle(graphics::TextStyle &style) {
  style.bgColor = game::ui::kColorSurfaceRaised;
  style.useGradient = false;
  style.bgGradientEnd = {0.0f, 0.0f, 0.0f, 0.0f};
  style.cornerRadius = game::ui::kRadiusChip;
  style.borderWidth = game::ui::kBorderWidthThin;
  style.borderColor = game::ui::kColorAccent;
  style.borderColor.w = 0.75f;
  style.hasShadow = true;
  style.shadowColor = game::ui::kShadowColor;
  style.shadowOffsetX = 0.0f;
  style.shadowOffsetY = game::ui::kShadowOffsetY;
}

void ApplyRowStyle(graphics::TextStyle &style) {
  style.bgColor = game::ui::kColorBgDark;
  style.useGradient = false;
  style.bgGradientEnd = {0.0f, 0.0f, 0.0f, 0.0f};
  style.cornerRadius = game::ui::kRadiusChip;
  style.borderWidth = game::ui::kBorderWidthThin;
  style.borderColor = game::ui::kColorBorder;
  style.hasShadow = true;
  style.shadowColor = game::ui::kShadowColor;
  style.shadowOffsetX = 0.0f;
  style.shadowOffsetY = game::ui::kShadowOffsetY;
}

} // namespace game::controllers::hud

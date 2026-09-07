/**
 * @file LiePanel.cpp
 * @brief ライ表示パネルの実装
 */

#include "LiePanel.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"
#include <string>

namespace game::controllers::hud {
namespace {

void SetTextVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  auto *text = world.Get<game::components::UIText>(entity);
  if (!text) {
    return;
  }
  text->visible = visible;
}

} // namespace

void LiePanel::Update(core::GameContext &ctx,
                      game::components::TerrainMaterial material) {
  if (m_background == UINT32_MAX) {
    Initialize(ctx);
  }

  std::wstring value = L"フェアウェイ";
  std::wstring condition = L"コンディション良好";
  DirectX::XMFLOAT4 color = game::ui::kColorSuccess;

  switch (material) {
  case game::components::TerrainMaterial::Rough:
    value = L"ラフ";
    condition = L"抵抗 やや大";
    color = game::ui::kColorWarning;
    break;
  case game::components::TerrainMaterial::Bunker:
    value = L"バンカー";
    condition = L"抵抗 大";
    color = game::ui::kColorWarning;
    break;
  case game::components::TerrainMaterial::Green:
    value = L"グリーン";
    condition = L"高速な転がり";
    color = game::ui::kColorSuccess;
    break;
  case game::components::TerrainMaterial::Ice:
    value = L"アイス";
    condition = L"非常に滑る";
    color = game::ui::kColorAccent;
    break;
  case game::components::TerrainMaterial::Water:
    value = L"ウォーター";
    condition = L"OUT OF BOUNDS";
    color = game::ui::kColorError;
    break;
  case game::components::TerrainMaterial::Lava:
    value = L"溶岩";
    condition = L"OUT OF BOUNDS";
    color = game::ui::kColorError;
    break;
  case game::components::TerrainMaterial::Stone:
    value = L"ストーン";
    condition = L"強くバウンド";
    color = game::ui::kColorTextSub;
    break;
  default:
    break;
  }

  auto *valueText = ctx.world.Get<game::components::UIText>(m_value);
  if (valueText) {
    valueText->text = value;
    valueText->style.color = color;
  }
  auto *conditionText =
      ctx.world.Get<game::components::UIText>(m_condition);
  if (conditionText) {
    conditionText->text = condition;
  }
}

void LiePanel::SetShotPhaseVisible(core::GameContext &ctx, bool shotPhase) {
  SetTextVisible(ctx.world, m_background, !shotPhase);
  SetTextVisible(ctx.world, m_label, !shotPhase);
  SetTextVisible(ctx.world, m_value, !shotPhase);
  SetTextVisible(ctx.world, m_condition, !shotPhase);
}

void LiePanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_background, visible);
  SetTextVisible(ctx.world, m_label, visible);
  SetTextVisible(ctx.world, m_value, visible);
  SetTextVisible(ctx.world, m_condition, visible);
}

void LiePanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_background = UINT32_MAX;
  m_label = UINT32_MAX;
  m_value = UINT32_MAX;
  m_condition = UINT32_MAX;
}

void LiePanel::Initialize(core::GameContext &ctx) {
  constexpr float kPanelWidth = 240.0f;
  constexpr float kPanelHeight = 84.0f;
  constexpr float kPanelX = (1280.0f - kPanelWidth) * 0.5f;
  constexpr float kPanelY = 580.0f;
  constexpr int kLayer = game::ui::kLayerShotPanel;

  m_background = m_entityOwner.Create(ctx.world);
  auto &background = ctx.world.Add<game::components::UIText>(m_background);
  background.x = kPanelX;
  background.y = kPanelY;
  background.width = kPanelWidth;
  background.height = kPanelHeight;
  ApplySurfaceStyle(background.style);
  background.layer = kLayer;
  background.visible = true;

  const auto createText = [&](ecs::Entity &destination, float x, float y,
                              float height, float fontSize,
                              const DirectX::XMFLOAT4 &color) {
    destination = m_entityOwner.Create(ctx.world);
    auto &text = ctx.world.Add<game::components::UIText>(destination);
    text.x = x;
    text.y = y;
    text.width = kPanelWidth - 36.0f;
    text.height = height;
    text.style = graphics::TextStyle::BrowserSub();
    text.style.fontSize = fontSize;
    text.style.color = color;
    text.style.align = graphics::TextAlign::Left;
    text.layer = kLayer + 1;
    text.visible = true;
  };

  createText(m_label, kPanelX + 18.0f, kPanelY + 12.0f, 18.0f, 11.0f,
             game::ui::kColorTextSub);
  ctx.world.Get<game::components::UIText>(m_label)->text = L"BALL LIE";

  createText(m_value, kPanelX + 18.0f, kPanelY + 32.0f, 25.0f, 20.0f,
             game::ui::kColorSuccess);
  ctx.world.Get<game::components::UIText>(m_value)->style.fontFamily =
      "Kiwi Maru Medium";

  createText(m_condition, kPanelX + 18.0f, kPanelY + 60.0f, 16.0f, 11.0f,
             game::ui::kColorTextSub);
  ctx.world.Get<game::components::UIText>(m_condition)->style.fontFamily =
      "Kiwi Maru Medium";
}

} // namespace game::controllers::hud

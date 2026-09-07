/**
 * @file WindPanel.cpp
 * @brief 風情報パネルの実装
 */

#include "WindPanel.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace game::controllers::hud {
namespace {

void SetTextIfChanged(game::components::UIText &text,
                      const std::wstring &value) {
  if (text.text == value) {
    return;
  }
  text.text = value;
}

void SetColorIfChanged(DirectX::XMFLOAT4 &color,
                       const DirectX::XMFLOAT4 &value) {
  if (color.x == value.x && color.y == value.y && color.z == value.z &&
      color.w == value.w) {
    return;
  }
  color = value;
}

void SetTextVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  auto *text = world.Get<game::components::UIText>(entity);
  if (!text) {
    return;
  }
  text->visible = visible;
}

std::wstring ResolveDirection(float angle) {
  if (angle > DirectX::XM_PI / 8.0f &&
      angle <= 3.0f * DirectX::XM_PI / 8.0f) {
    return L"↗";
  }
  if (angle > 3.0f * DirectX::XM_PI / 8.0f &&
      angle <= 5.0f * DirectX::XM_PI / 8.0f) {
    return L"→";
  }
  if (angle > 5.0f * DirectX::XM_PI / 8.0f &&
      angle <= 7.0f * DirectX::XM_PI / 8.0f) {
    return L"↘";
  }
  if (angle > 7.0f * DirectX::XM_PI / 8.0f ||
      angle <= -7.0f * DirectX::XM_PI / 8.0f) {
    return L"↓";
  }
  if (angle < -5.0f * DirectX::XM_PI / 8.0f) {
    return L"↙";
  }
  if (angle < -3.0f * DirectX::XM_PI / 8.0f) {
    return L"←";
  }
  if (angle < -DirectX::XM_PI / 8.0f) {
    return L"↖";
  }
  return L"↑";
}

} // namespace

void WindPanel::Initialize(core::GameContext &ctx) {
  constexpr float kPanelWidth = game::ui::kWindCardWidth;
  constexpr float kPanelX = game::ui::kWindCardX;
  constexpr float kPanelY = game::ui::kWindCardY;

  m_background = m_entityOwner.Create(ctx.world);
  auto &background = ctx.world.Add<game::components::UIText>(m_background);
  background.x = kPanelX;
  background.y = kPanelY;
  background.width = kPanelWidth;
  background.height = 92.0f;
  ApplySurfaceStyle(background.style);
  background.visible = true;
  background.layer = game::ui::kLayerWind - 1;

  m_label = m_entityOwner.Create(ctx.world);
  auto &label = ctx.world.Add<game::components::UIText>(m_label);
  label.text = L"WIND";
  label.x = kPanelX + 14.0f;
  label.y = kPanelY + 10.0f;
  label.width = kPanelWidth - 28.0f;
  label.height = 18.0f;
  label.style = graphics::TextStyle::CardLabel();
  label.style.fontSize = game::ui::kWindLabelFontSize;
  label.style.align = graphics::TextAlign::Left;
  label.visible = true;
  label.layer = game::ui::kLayerWind;

  m_value = m_entityOwner.Create(ctx.world);
  auto &value = ctx.world.Add<game::components::UIText>(m_value);
  value.text = L"--";
  value.x = kPanelX + 14.0f;
  value.y = kPanelY + 30.0f;
  value.width = 70.0f;
  value.height = 38.0f;
  value.style = graphics::TextStyle::CardValue();
  value.style.fontSize = game::ui::kWindValueFontSize;
  value.style.align = graphics::TextAlign::Left;
  value.visible = true;
  value.layer = game::ui::kLayerWind + 1;

  m_directionAndUnit = m_entityOwner.Create(ctx.world);
  auto &directionAndUnit =
      ctx.world.Add<game::components::UIText>(m_directionAndUnit);
  directionAndUnit.text = L"↑  m/s";
  directionAndUnit.x = kPanelX + 76.0f;
  directionAndUnit.y = kPanelY + 38.0f;
  directionAndUnit.width = 58.0f;
  directionAndUnit.height = 32.0f;
  directionAndUnit.style = graphics::TextStyle::CardValue();
  directionAndUnit.style.color = game::ui::kColorTextSub;
  directionAndUnit.style.fontSize = 14.0f;
  directionAndUnit.style.align = graphics::TextAlign::Right;
  directionAndUnit.visible = true;
  directionAndUnit.layer = game::ui::kLayerWind + 1;
}

void WindPanel::Update(core::GameContext &ctx, float elapsedTime,
                       float windSpeed,
                       const DirectX::XMFLOAT2 &windDirection,
                       float cameraYaw) {
  auto *value = ctx.world.Get<game::components::UIText>(m_value);
  if (value) {
    wchar_t buffer[32];
    swprintf(buffer, 32, L"%.1f", windSpeed);
    SetTextIfChanged(*value, buffer);

    if (windSpeed >= 10.0f) {
      SetColorIfChanged(value->style.color, game::ui::kColorError);
    } else if (windSpeed >= 5.0f) {
      SetColorIfChanged(value->style.color, game::ui::kColorWarning);
    } else {
      SetColorIfChanged(value->style.color, game::ui::kColorTextPrimary);
    }
  }

  auto *directionAndUnit =
      ctx.world.Get<game::components::UIText>(m_directionAndUnit);
  if (!directionAndUnit) {
    return;
  }

  const DirectX::XMVECTOR worldDirection =
      DirectX::XMVectorSet(windDirection.x, 0.0f, windDirection.y, 0.0f);
  const DirectX::XMVECTOR cameraForward =
      DirectX::XMVectorSet(std::sin(cameraYaw), 0.0f, std::cos(cameraYaw),
                           0.0f);
  DirectX::XMFLOAT3 crossProduct;
  DirectX::XMStoreFloat3(
      &crossProduct,
      DirectX::XMVector3Cross(cameraForward, worldDirection));
  const float dotProduct = DirectX::XMVectorGetX(
      DirectX::XMVector3Dot(cameraForward, worldDirection));
  const float angle = std::atan2(crossProduct.y, dotProduct);
  SetTextIfChanged(*directionAndUnit, ResolveDirection(angle) + L" m/s");

  constexpr float kCycleSeconds = 0.55f;
  const float phase = std::fmod(elapsedTime, kCycleSeconds) / kCycleSeconds;
  const float rise = std::sin(phase * DirectX::XM_PI);
  const float amplitude = 5.0f + std::min(windSpeed, 12.0f);
  directionAndUnit->x = game::ui::kWindCardX + 76.0f;
  directionAndUnit->y = game::ui::kWindCardY + 38.0f - rise * amplitude;
}

void WindPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_background, visible);
  SetTextVisible(ctx.world, m_label, visible);
  SetTextVisible(ctx.world, m_value, visible);
  SetTextVisible(ctx.world, m_directionAndUnit, visible);
}

void WindPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_background = UINT32_MAX;
  m_label = UINT32_MAX;
  m_value = UINT32_MAX;
  m_directionAndUnit = UINT32_MAX;
}

} // namespace game::controllers::hud

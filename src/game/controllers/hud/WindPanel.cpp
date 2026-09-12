/**
 * @file WindPanel.cpp
 * @brief 参考HUDに合わせた風・地形情報表示
 */

#include "WindPanel.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIImage.h"
#include "../../components/UIText.h"
#include "../../components/WikiComponents.h"
#include "../../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace game::controllers::hud {
namespace {

void SetTextVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  if (auto *text = world.Get<game::components::UIText>(entity)) {
    text->visible = visible;
  }
}

void SetImageVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  if (auto *image = world.Get<game::components::UIImage>(entity)) {
    image->visible = visible;
  }
}

const char *SurfaceTexture(game::components::TerrainMaterial terrain) {
  using game::components::TerrainMaterial;
  switch (terrain) {
  case TerrainMaterial::Fairway:
    return "Assets/textures/ui_terrain_fairway.png";
  case TerrainMaterial::Green:
    return "Assets/textures/ui_terrain_green.png";
  case TerrainMaterial::Bunker:
    return "Assets/textures/ui_terrain_bunker.png";
  case TerrainMaterial::Water:
  case TerrainMaterial::Lava:
    return "Assets/textures/ui_terrain_ob.png";
  default:
    return "Assets/textures/ui_terrain_rough.png";
  }
}

std::wstring ResolveDirection(float angle) {
  constexpr float step = DirectX::XM_PI / 8.0f;
  if (angle > step && angle <= step * 3.0f) return L"↗";
  if (angle > step * 3.0f && angle <= step * 5.0f) return L"→";
  if (angle > step * 5.0f && angle <= step * 7.0f) return L"↘";
  if (angle > step * 7.0f || angle <= -step * 7.0f) return L"↓";
  if (angle < -step * 5.0f) return L"↙";
  if (angle < -step * 3.0f) return L"←";
  if (angle < -step) return L"↖";
  return L"↑";
}

} // namespace

void WindPanel::Initialize(core::GameContext &ctx) {
  const float x = game::ui::kWindCardX;
  const float y = game::ui::kWindCardY;

  m_background = m_entityOwner.Create(ctx.world);
  auto &background = ctx.world.Add<game::components::UIText>(m_background);
  background.x = x + 8.0f;
  background.y = y + 32.0f;
  background.width = 142.0f;
  background.height = 112.0f;
  background.style.bgColor = {0.05f, 0.08f, 0.07f, 0.68f};
  background.style.borderColor = {0.75f, 0.80f, 0.76f, 0.75f};
  background.style.borderWidth = 3.0f;
  background.style.cornerRadius = 56.0f;
  background.style.hasShadow = true;
  background.style.shadowColor = {0.0f, 0.0f, 0.0f, 0.6f};
  background.style.shadowOffsetY = 3.0f;
  background.layer = game::ui::kLayerWind - 1;

  m_surfaceTexture = m_entityOwner.Create(ctx.world);
  auto &surface = ctx.world.Add<game::components::UIImage>(m_surfaceTexture);
  surface = game::components::UIImage::Create(
      "Assets/textures/ui_terrain_fairway.png", x, y);
  surface.width = 160.0f;
  surface.height = 72.0f;
  surface.layer = game::ui::kLayerWind + 1;

  m_label = m_entityOwner.Create(ctx.world);
  auto &label = ctx.world.Add<game::components::UIText>(m_label);
  label.text = L"Wind";
  label.x = x + 18.0f;
  label.y = y + 77.0f;
  label.width = 122.0f;
  label.height = 34.0f;
  label.style = graphics::TextStyle::Guide();
  label.style.fontFamily = "Barlow Condensed Black";
  label.style.fontSize = 25.0f;
  label.layer = game::ui::kLayerWind + 2;

  m_value = m_entityOwner.Create(ctx.world);
  auto &value = ctx.world.Add<game::components::UIText>(m_value);
  value.text = L"--";
  value.x = x + 168.0f;
  value.y = y + 55.0f;
  value.width = 80.0f;
  value.height = 40.0f;
  value.style = graphics::TextStyle::Guide();
  value.style.fontFamily = "Share Tech Mono";
  value.style.fontSize = 29.0f;
  value.style.align = graphics::TextAlign::Right;
  value.layer = game::ui::kLayerWind + 1;

  m_directionAndUnit = m_entityOwner.Create(ctx.world);
  auto &direction =
      ctx.world.Add<game::components::UIText>(m_directionAndUnit);
  direction.text = L"↑ m/s";
  direction.x = x + 250.0f;
  direction.y = y + 61.0f;
  direction.width = 80.0f;
  direction.height = 32.0f;
  direction.style = graphics::TextStyle::Guide();
  direction.style.fontFamily = "Barlow Condensed SemiBold";
  direction.style.fontSize = 18.0f;
  direction.style.align = graphics::TextAlign::Left;
  direction.layer = game::ui::kLayerWind + 1;

  m_rule = m_entityOwner.Create(ctx.world);
  auto &rule = ctx.world.Add<game::components::UIText>(m_rule);
  rule.x = x + 158.0f;
  rule.y = y + 96.0f;
  rule.width = game::ui::kWindCardWidth - 158.0f;
  rule.height = 2.0f;
  rule.style.bgColor = {1.0f, 1.0f, 1.0f, 0.9f};
  rule.layer = game::ui::kLayerWind;
}

void WindPanel::Update(core::GameContext &ctx, float elapsedTime,
                       float windSpeed,
                       const DirectX::XMFLOAT2 &windDirection,
                       float cameraYaw,
                       game::components::TerrainMaterial terrain) {
  if (auto *surface =
          ctx.world.Get<game::components::UIImage>(m_surfaceTexture)) {
    surface->texturePath = SurfaceTexture(terrain);
  }

  if (auto *value = ctx.world.Get<game::components::UIText>(m_value)) {
    wchar_t buffer[32];
    swprintf(buffer, 32, L"%.1f", windSpeed);
    value->text = buffer;
  }

  auto *direction =
      ctx.world.Get<game::components::UIText>(m_directionAndUnit);
  if (!direction) return;

  const DirectX::XMVECTOR worldDirection =
      DirectX::XMVectorSet(windDirection.x, 0.0f, windDirection.y, 0.0f);
  const DirectX::XMVECTOR cameraForward =
      DirectX::XMVectorSet(std::sin(cameraYaw), 0.0f, std::cos(cameraYaw),
                           0.0f);
  DirectX::XMFLOAT3 cross;
  DirectX::XMStoreFloat3(
      &cross, DirectX::XMVector3Cross(cameraForward, worldDirection));
  const float dot = DirectX::XMVectorGetX(
      DirectX::XMVector3Dot(cameraForward, worldDirection));
  direction->text = ResolveDirection(std::atan2(cross.y, dot)) + L" m/s";

  const float speedFactor = std::clamp(windSpeed / 12.0f, 0.0f, 1.0f);
  const float cycle = 0.55f * 1.9f / (1.9f + speedFactor * 4.6f);
  const float phase = std::fmod(elapsedTime, cycle) / cycle;
  const float rise = std::sin(phase * DirectX::XM_PI);
  direction->y = game::ui::kWindCardY + 61.0f -
                 rise * (3.0f + std::min(windSpeed, 12.0f) * 0.5f);
}

void WindPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_background, visible);
  SetImageVisible(ctx.world, m_surfaceTexture, visible);
  SetTextVisible(ctx.world, m_label, visible);
  SetTextVisible(ctx.world, m_value, visible);
  SetTextVisible(ctx.world, m_directionAndUnit, visible);
  SetTextVisible(ctx.world, m_rule, visible);
}

void WindPanel::SetOpacity(core::GameContext &ctx, float opacity) {
  ctx.world.Query<game::components::UIText>().Each(
      [&](ecs::Entity entity, game::components::UIText &text) {
        if (m_entityOwner.Owns(entity)) text.opacity = opacity;
      });
  ctx.world.Query<game::components::UIImage>().Each(
      [&](ecs::Entity entity, game::components::UIImage &image) {
        if (m_entityOwner.Owns(entity)) image.opacity = opacity;
      });
}

void WindPanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_background = UINT32_MAX;
  m_surfaceTexture = UINT32_MAX;
  m_label = UINT32_MAX;
  m_value = UINT32_MAX;
  m_directionAndUnit = UINT32_MAX;
  m_rule = UINT32_MAX;
}

} // namespace game::controllers::hud

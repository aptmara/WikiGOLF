#include "src/game/utils/MapViewState.h"
#include "src/game/controllers/MinimapControllerInternals.h"
#include "src/game/components/MeshRenderer.h"
#include "src/game/systems/ShadowCulling.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      return 1;                                                                \
    } else {                                                                   \
      std::cout << "[PASS] " << message << "\n";                               \
    }                                                                          \
  } while (0)

int main() {
  using game::utils::ClampMapCenter;
  using game::utils::ClampMapZoom;

  // 中心が範囲外の場合、フィールド端＋パディング内に収まる
  DirectX::XMFLOAT2 center{200.0f, -300.0f};
  auto clamped = ClampMapCenter(center, 100.0f, 150.0f, 5.0f);
  CHECK(std::abs(clamped.x - 45.0f) < 1e-4f,
        "ClampMapCenter clamps X to width/2 - padding");
  CHECK(std::abs(clamped.y + 70.0f) < 1e-4f,
        "ClampMapCenter clamps Y(Z) to depth/2 - padding");

  // ズームは最小・最大でクランプされる
  CHECK(std::abs(ClampMapZoom(0.1f, 0.3f, 2.0f) - 0.3f) < 1e-4f,
        "ClampMapZoom clamps to min");
  CHECK(std::abs(ClampMapZoom(3.5f, 0.3f, 2.0f) - 2.0f) < 1e-4f,
        "ClampMapZoom clamps to max");
  CHECK(std::abs(ClampMapZoom(1.2f, 0.3f, 2.0f) - 1.2f) < 1e-4f,
        "ClampMapZoom leaves in-range values");

  // フィールドが小さい場合はデフォルト上限をそのまま返す
  CHECK(std::abs(game::utils::CalculateMaxMapZoom(50.0f, 5.0f, 15.0f) - 15.0f) <
            1e-4f,
        "CalculateMaxMapZoom keeps base cap for small maps");

  // 大きいフィールドでも最小ビュー幅5mまで寄れる上限を返す
  float expectedZoom = 500.0f / 5.0f; // extent / minViewSpan
  CHECK(std::abs(game::utils::CalculateMaxMapZoom(500.0f, 5.0f, 15.0f) -
                 expectedZoom) < 1e-4f,
        "CalculateMaxMapZoom scales with field extent for deep zoom");

  const auto fixedMap =
      game::controllers::minimap_detail::BuildHudMinimapParams(80.0f, 120.0f);
  CHECK(fixedMap.center.x == 0.0f && fixedMap.center.z == 0.0f &&
            fixedMap.visibleWidth >= 80.0f && fixedMap.visibleDepth >= 120.0f,
        "HUD minimap keeps the whole course in a fixed centered view");
  CHECK(game::systems::ComputeMinimapFarPlane(1200.0f, 2000.0f) > 4400.0f,
        "minimap far plane expands beyond a large course camera height");
  game::controllers::minimap_detail::MarkerBounds bounds{922.0f, 266.0f,
                                                          316.0f, 386.0f};
  CHECK(game::controllers::minimap_detail::ContainsScreenPoint(
            {100.0f, 200.0f, 20.0f, 20.0f}, 97.0f, 210.0f, 3.0f),
        "hole icon hit test includes hover padding");
  CHECK(!game::controllers::minimap_detail::ContainsScreenPoint(
             {100.0f, 200.0f, 20.0f, 20.0f}, 96.9f, 210.0f, 3.0f),
        "hole icon hit test rejects points outside hover padding");
  float worldX = 0.0f;
  float worldZ = 0.0f;
  CHECK(game::controllers::minimap_detail::UnprojectHudMinimap(
            bounds.x + bounds.width * 0.5f,
            bounds.y + bounds.height * 0.5f, bounds, fixedMap, worldX,
            worldZ) &&
            std::abs(worldX) < 1e-4f && std::abs(worldZ) < 1e-4f,
        "HUD minimap center unprojects to the course origin");

  const std::array<DirectX::XMFLOAT3, 4> viewportCorners = {{
      {-20.0f, 0.0f, 30.0f},
      {20.0f, 0.0f, 30.0f},
      {-20.0f, 0.0f, -30.0f},
      {20.0f, 0.0f, -30.0f},
  }};
  game::controllers::minimap_detail::MarkerBounds viewportBounds;
  CHECK(game::controllers::minimap_detail::ProjectWorldCornersToHudBounds(
            viewportCorners, bounds, fixedMap, viewportBounds) &&
            viewportBounds.x >= bounds.x && viewportBounds.y >= bounds.y &&
            viewportBounds.x + viewportBounds.width <= bounds.x + bounds.width &&
            viewportBounds.y + viewportBounds.height <= bounds.y + bounds.height,
        "map-view screen footprint stays inside the HUD minimap");

  using game::controllers::minimap_detail::ClassifyFlag;
  using game::controllers::minimap_detail::FlagKind;
  CHECK(ClassifyFlag(true, 0) == FlagKind::Target,
        "target hole uses the red flag filter");
  CHECK(ClassifyFlag(false, 1) == FlagKind::OneHop,
        "one-hop hole uses the yellow flag filter");
  CHECK(ClassifyFlag(false, 2) == FlagKind::TwoHops,
        "two-hop hole uses the orange flag filter");
  CHECK(ClassifyFlag(false, 3) == FlagKind::ThreeToFiveHops &&
            ClassifyFlag(false, 5) == FlagKind::ThreeToFiveHops,
        "three-to-five-hop holes use the white flag filter");
  CHECK(ClassifyFlag(false, 6) == FlagKind::SixOrMoreHops,
        "six-or-more-hop holes use the gray flag filter");
  CHECK(ClassifyFlag(false, -1) == FlagKind::Unknown,
        "unevaluated holes use the blue flag filter");
  CHECK(game::controllers::minimap_detail::kFlagKindCount == 6,
        "all six flag colors have a filter");

  using game::components::IsMinimapRenderable;
  using game::components::MinimapRenderMode;
  CHECK(!IsMinimapRenderable(MinimapRenderMode::None) &&
            IsMinimapRenderable(MinimapRenderMode::VertexColor) &&
            IsMinimapRenderable(MinimapRenderMode::Textured),
        "minimap renders terrain colors and HTML texture overlays");

  DirectX::BoundingSphere nearCaster({49.0f, 0.0f, 0.0f}, 0.0f);
  DirectX::BoundingSphere farCaster({51.0f, 0.0f, 0.0f}, 0.0f);
  DirectX::BoundingSphere overlappingCaster({51.0f, 0.0f, 0.0f}, 2.0f);
  CHECK(game::systems::shadow_detail::ShouldRenderFrame(false) &&
            !game::systems::shadow_detail::ShouldRenderFrame(true),
        "map view skips the shadow pass");
  CHECK(game::systems::shadow_detail::IsNearFocus(
            nearCaster, {0.0f, 0.0f, 0.0f}) &&
            !game::systems::shadow_detail::IsNearFocus(
                farCaster, {0.0f, 0.0f, 0.0f}) &&
            game::systems::shadow_detail::IsNearFocus(
                overlappingCaster, {0.0f, 0.0f, 0.0f}),
        "shadow casters are limited to the nearby 50 meter area");

  using game::controllers::minimap_detail::ResolveFlagVisibility;
  constexpr size_t flagCount =
      game::controllers::minimap_detail::kFlagKindCount;
  std::array<bool, flagCount> selected = {true, true, false,
                                          false, false, false};
  std::array<bool, flagCount> available = {false, false, true,
                                           true, false, true};
  auto visible = ResolveFlagVisibility(selected, available);
  CHECK(!visible[0] && !visible[1] && visible[2] && !visible[3] &&
            !visible[4] && !visible[5],
        "best available flag kind is shown when selected kinds are absent");
  CHECK(selected[0] && selected[1] && !selected[2],
        "temporary fallback does not alter the user selection");

  selected = {false, true, false, false, false, false};
  available = {true, true, true, true, true, true};
  visible = ResolveFlagVisibility(selected, available);
  CHECK(!visible[0] && visible[1] && !visible[2],
        "an available selected kind prevents a better unselected fallback");

  selected.fill(false);
  available.fill(false);
  visible = ResolveFlagVisibility(selected, available);
  bool anyVisible = false;
  for (bool flagVisible : visible) {
    anyVisible = anyVisible || flagVisible;
  }
  CHECK(!anyVisible, "no fallback is shown when the course has no flags");

  std::cout << "All MapView utils tests passed.\n";
  return 0;
}

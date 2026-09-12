/**
 * @file ClubSelectionPanel.cpp
 * @brief 参考HUDに合わせた単一クラブ選択表示
 */

#include "ClubSelectionPanel.h"
#include "../../../core/GameContext.h"
#include "../../../core/StringUtils.h"
#include "../../../ecs/World.h"
#include "../../components/UIImage.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"

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

} // namespace

void ClubSelectionPanel::Initialize(core::GameContext &ctx) {
  const float x = game::ui::kClubPanelX;
  const float y = game::ui::kClubPanelY;

  m_pinIcon = m_staticEntityOwner.Create(ctx.world);
  auto &pinIcon = ctx.world.Add<game::components::UIImage>(m_pinIcon);
  pinIcon = game::components::UIImage::Create("Assets/ui/mouse_scroll.png",
                                               x + 12.0f, y - 52.0f);
  pinIcon.width = 38.0f;
  pinIcon.height = 38.0f;
  pinIcon.layer = game::ui::kLayerClubSelect + 2;

  m_pinHint = m_staticEntityOwner.Create(ctx.world);
  auto &pinHint = ctx.world.Add<game::components::UIText>(m_pinHint);
  pinHint.text = L"中クリック  照準ピン設置";
  pinHint.x = x + 58.0f;
  pinHint.y = y - 47.0f;
  pinHint.width = 270.0f;
  pinHint.height = 30.0f;
  pinHint.style = graphics::TextStyle::Guide();
  pinHint.style.fontFamily = "Kiwi Maru Medium";
  pinHint.style.fontSize = 20.0f;
  pinHint.style.align = graphics::TextAlign::Left;
  pinHint.layer = game::ui::kLayerClubSelect + 2;

  m_background = m_staticEntityOwner.Create(ctx.world);
  auto &background = ctx.world.Add<game::components::UIText>(m_background);
  background.x = 0.0f;
  background.y = y + 8.0f;
  background.width = game::ui::kClubItemW;
  background.height = 64.0f;
  background.style.bgColor = {0.03f, 0.67f, 0.78f, 0.72f};
  background.layer = game::ui::kLayerClubSelect;

  const auto createKey = [&](ecs::Entity &entity, const char *path,
                             float keyX) {
    entity = m_staticEntityOwner.Create(ctx.world);
    auto &key = ctx.world.Add<game::components::UIImage>(entity);
    key = game::components::UIImage::Create(path, keyX, y + 20.0f);
    key.width = 42.0f;
    key.height = 42.0f;
    key.layer = game::ui::kLayerClubSelect + 2;
  };
  createKey(m_qKey, "Assets/ui/keyboard_q.png", x + 12.0f);
  createKey(m_eKey, "Assets/ui/keyboard_e.png", x + 58.0f);
}

void ClubSelectionPanel::Update(core::GameContext &ctx, float,
                                const std::vector<ClubUIData> &clubs,
                                int currentClubIndex) {
  if (currentClubIndex < 0 ||
      currentClubIndex >= static_cast<int>(clubs.size())) {
    m_selectedEntityOwner.DestroyAll(ctx.world);
    m_selectedIndex = -1;
    return;
  }
  if (m_selectedIndex == currentClubIndex) return;

  m_selectedIndex = currentClubIndex;
  RebuildSelectedClub(ctx, clubs[currentClubIndex]);
}

void ClubSelectionPanel::RebuildSelectedClub(core::GameContext &ctx,
                                              const ClubUIData &club) {
  m_selectedEntityOwner.DestroyAll(ctx.world);
  const float x = game::ui::kClubPanelX;
  const float y = game::ui::kClubPanelY;

  m_clubIcon = m_selectedEntityOwner.Create(ctx.world);
  auto &icon = ctx.world.Add<game::components::UIImage>(m_clubIcon);
  icon = game::components::UIImage::Create(club.iconTexture, x + 106.0f,
                                            y - 4.0f);
  icon.width = 92.0f;
  icon.height = 92.0f;
  icon.layer = game::ui::kLayerClubSelect + 2;

  m_clubName = m_selectedEntityOwner.Create(ctx.world);
  auto &name = ctx.world.Add<game::components::UIText>(m_clubName);
  name.text = core::ToWString(club.name);
  name.x = x + 200.0f;
  name.y = y + 14.0f;
  name.width = 205.0f;
  name.height = 32.0f;
  name.style = graphics::TextStyle::Guide();
  name.style.fontFamily = "Mamelon 5 Hi";
  name.style.fontSize = 27.0f;
  name.style.align = graphics::TextAlign::Left;
  name.layer = game::ui::kLayerClubSelect + 2;

  m_clubSubName = m_selectedEntityOwner.Create(ctx.world);
  auto &subName = ctx.world.Add<game::components::UIText>(m_clubSubName);
  subName.text = core::ToWString(club.shortName + "  " + club.categoryEN);
  subName.x = x + 202.0f;
  subName.y = y + 48.0f;
  subName.width = 200.0f;
  subName.height = 24.0f;
  subName.style = graphics::TextStyle::Guide();
  subName.style.fontFamily = "Barlow Condensed Black";
  subName.style.fontSize = 14.0f;
  subName.style.align = graphics::TextAlign::Left;
  subName.layer = game::ui::kLayerClubSelect + 2;
}

void ClubSelectionPanel::SetShotPhaseVisible(core::GameContext &ctx,
                                             bool shotPhase) {
  SetVisible(ctx, !shotPhase);
}

void ClubSelectionPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_background, visible);
  SetImageVisible(ctx.world, m_qKey, visible);
  SetImageVisible(ctx.world, m_eKey, visible);
  SetImageVisible(ctx.world, m_pinIcon, visible);
  SetTextVisible(ctx.world, m_pinHint, visible);
  SetImageVisible(ctx.world, m_clubIcon, visible);
  SetTextVisible(ctx.world, m_clubName, visible);
  SetTextVisible(ctx.world, m_clubSubName, visible);
}

void ClubSelectionPanel::SetOpacity(core::GameContext &ctx, float opacity) {
  const auto owns = [&](ecs::Entity entity) {
    return m_staticEntityOwner.Owns(entity) ||
           m_selectedEntityOwner.Owns(entity);
  };
  ctx.world.Query<game::components::UIText>().Each(
      [&](ecs::Entity entity, game::components::UIText &text) {
        if (owns(entity)) text.opacity = opacity;
      });
  ctx.world.Query<game::components::UIImage>().Each(
      [&](ecs::Entity entity, game::components::UIImage &image) {
        if (owns(entity)) image.opacity = opacity;
      });
}

void ClubSelectionPanel::Shutdown(core::GameContext &ctx) {
  m_selectedEntityOwner.DestroyAll(ctx.world);
  m_staticEntityOwner.DestroyAll(ctx.world);
  m_background = UINT32_MAX;
  m_qKey = UINT32_MAX;
  m_eKey = UINT32_MAX;
  m_pinIcon = UINT32_MAX;
  m_pinHint = UINT32_MAX;
  m_clubIcon = UINT32_MAX;
  m_clubName = UINT32_MAX;
  m_clubSubName = UINT32_MAX;
  m_selectedIndex = -1;
}

} // namespace game::controllers::hud

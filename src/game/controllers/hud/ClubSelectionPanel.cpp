/**
 * @file ClubSelectionPanel.cpp
 * @brief クラブ選択パネルの実装
 */

#include "ClubSelectionPanel.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../core/StringUtils.h"
#include "../../../ecs/World.h"
#include "../../components/UIImage.h"
#include "../../components/UIText.h"
#include "../../utils/UIConstants.h"
#include <cmath>

namespace game::controllers::hud {
namespace {

void SetTextVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  auto *text = world.Get<game::components::UIText>(entity);
  if (!text) {
    return;
  }
  text->visible = visible;
}

void SetImageVisible(ecs::World &world, ecs::Entity entity, bool visible) {
  auto *image = world.Get<game::components::UIImage>(entity);
  if (!image) {
    return;
  }
  image->visible = visible;
}

} // namespace

void ClubSelectionPanel::Initialize(core::GameContext &ctx) {
  m_header = m_staticEntityOwner.Create(ctx.world);
  auto &header = ctx.world.Add<game::components::UIText>(m_header);
  header.text = L"CLUB SELECT   Q / E";
  header.x = game::ui::kClubPanelX;
  header.y = game::ui::kClubPanelY - 26.0f;
  header.width = game::ui::kClubItemW;
  header.height = 18.0f;
  header.style = graphics::TextStyle::CardLabel();
  header.style.color = game::ui::kColorAccent;
  header.visible = true;
  header.layer = game::ui::kLayerClubSelect;

  m_landingPreviewBackground = m_staticEntityOwner.Create(ctx.world);
  auto &background =
      ctx.world.Add<game::components::UIText>(m_landingPreviewBackground);
  background.x = game::ui::kLandingPreviewBtnX;
  background.y = game::ui::kLandingPreviewBtnY;
  background.width = game::ui::kLandingPreviewBtnW;
  background.height = game::ui::kLandingPreviewBtnH;
  ApplyRowStyle(background.style);
  background.visible = true;
  background.layer = game::ui::kLayerClubSelect;

  m_landingPreviewText = m_staticEntityOwner.Create(ctx.world);
  auto &buttonText =
      ctx.world.Add<game::components::UIText>(m_landingPreviewText);
  buttonText.text = L"⛳ 着弾予測";
  buttonText.x = game::ui::kLandingPreviewBtnX;
  buttonText.y = game::ui::kLandingPreviewBtnY;
  buttonText.width = game::ui::kLandingPreviewBtnW;
  buttonText.height = game::ui::kLandingPreviewBtnH;
  buttonText.style = graphics::TextStyle::BrowserSub();
  buttonText.style.fontSize = game::ui::kLandingPreviewBtnFontSize;
  buttonText.style.align = graphics::TextAlign::Center;
  buttonText.style.color = game::ui::kColorTextPrimary;
  buttonText.style.bgColor = {0.0f, 0.0f, 0.0f, 0.0f};
  buttonText.style.borderWidth = 0.0f;
  buttonText.visible = true;
  buttonText.layer = game::ui::kLayerClubSelect + 1;
}

void ClubSelectionPanel::Update(core::GameContext &ctx, float elapsedTime,
                                const std::vector<ClubUIData> &clubs,
                                int currentClubIndex) {
  const int clubCount = static_cast<int>(clubs.size());
  if (clubCount == 0) {
    return;
  }

  if (m_rows.size() != clubs.size()) {
    RebuildRows(ctx, clubs);
  }

  if (clubCount > 1) {
    const float bob = std::sin(elapsedTime * 1.3f) * 3.0f;
    const float pulse =
        0.55f + 0.45f * (0.5f + 0.5f * std::sin(elapsedTime * 1.3f));
    const float firstSlotY = game::ui::kClubPanelY;
    const float thirdSlotY =
        game::ui::kClubPanelY +
        2.0f * (game::ui::kClubItemH + game::ui::kClubItemSpacing);
    auto *up = ctx.world.Get<game::components::UIText>(m_scrollUp);
    if (up) {
      up->y = firstSlotY + game::ui::kClubItemH * 0.25f - bob;
      up->style.color.w = pulse;
    }
    auto *down = ctx.world.Get<game::components::UIText>(m_scrollDown);
    if (down) {
      down->y = thirdSlotY + game::ui::kClubItemH * 0.25f + bob;
      down->style.color.w = pulse;
    }
  }

  const int previousIndex = (currentClubIndex - 1 + clubCount) % clubCount;
  const int nextIndex = (currentClubIndex + 1) % clubCount;
  for (int index = 0; index < clubCount; ++index) {
    const bool selected = index == currentClubIndex;
    int slot = -1;
    if (index == previousIndex) {
      slot = 0;
    }
    if (selected) {
      slot = 1;
    }
    if (index == nextIndex) {
      slot = 2;
    }
    UpdateRow(ctx, m_rows[index], slot, selected, elapsedTime);
  }
}

void ClubSelectionPanel::UpdateLandingPreviewButton(core::GameContext &ctx,
                                                     bool hovered, bool active,
                                                     bool enabled) {
  auto *background =
      ctx.world.Get<game::components::UIText>(m_landingPreviewBackground);
  auto *text = ctx.world.Get<game::components::UIText>(m_landingPreviewText);
  if (!background || !text) {
    return;
  }

  if (active) {
    background->style.bgColor = game::ui::kColorAccent;
    background->style.borderColor = game::ui::kColorAccent;
    text->style.color = game::ui::kColorWhite;
    return;
  }

  if (hovered && enabled) {
    background->style.bgColor = game::ui::kColorBgDark;
    background->style.bgColor.w = 0.95f;
    background->style.borderColor = game::ui::kColorBorder;
    text->style.color = game::ui::kColorTextPrimary;
    return;
  }

  background->style.bgColor = game::ui::kColorBgDark;
  background->style.borderColor = game::ui::kColorBorder;
  if (enabled) {
    text->style.color = game::ui::kColorTextPrimary;
  } else {
    text->style.color = game::ui::kColorTextSub;
  }
}

void ClubSelectionPanel::SetShotPhaseVisible(core::GameContext &ctx,
                                             bool shotPhase) {
  SetTextVisible(ctx.world, m_header, !shotPhase);
  SetTextVisible(ctx.world, m_landingPreviewBackground, !shotPhase);
  SetTextVisible(ctx.world, m_landingPreviewText, !shotPhase);

  for (ClubRowEntities &row : m_rows) {
    bool visible = row.visibleInWindow;
    if (shotPhase) {
      visible = false;
    }
    SetTextVisible(ctx.world, row.background, visible);
    SetImageVisible(ctx.world, row.icon, visible);
    SetTextVisible(ctx.world, row.name, visible);
    SetTextVisible(ctx.world, row.subName, visible);

    auto *name = ctx.world.Get<game::components::UIText>(row.name);
    auto *subName = ctx.world.Get<game::components::UIText>(row.subName);
    auto *icon = ctx.world.Get<game::components::UIImage>(row.icon);
    auto *background =
        ctx.world.Get<game::components::UIText>(row.background);
    if (shotPhase) {
      if (name) {
        name->style.color.w = 0.35f;
      }
      if (subName) {
        subName->style.color.w = 0.315f;
      }
      if (icon) {
        icon->alpha = 0.35f;
      }
      if (background) {
        background->style.bgColor.w = 0.35f;
      }
      continue;
    }

    if (name) {
      name->style.color.w = 1.0f;
    }
    if (subName) {
      subName->style.color.w = 0.9f;
    }
    if (icon) {
      if (icon->alpha > 0.5f) {
        icon->alpha = 1.0f;
      } else {
        icon->alpha = 0.5f;
      }
    }
    if (background) {
      if (row.selected) {
        background->style.bgColor.w = game::ui::kColorSurfaceRaised.w;
      } else {
        background->style.bgColor.w = 0.55f;
      }
    }
  }

  SetTextVisible(ctx.world, m_scrollUp, !shotPhase);
  SetTextVisible(ctx.world, m_scrollDown, !shotPhase);
}

void ClubSelectionPanel::SetVisible(core::GameContext &ctx, bool visible) {
  SetTextVisible(ctx.world, m_header, visible);
  SetTextVisible(ctx.world, m_landingPreviewBackground, visible);
  SetTextVisible(ctx.world, m_landingPreviewText, visible);
  for (ClubRowEntities &row : m_rows) {
    SetTextVisible(ctx.world, row.background, visible);
    SetImageVisible(ctx.world, row.icon, visible);
    SetTextVisible(ctx.world, row.name, visible);
    SetTextVisible(ctx.world, row.subName, visible);
  }
  SetTextVisible(ctx.world, m_scrollUp, visible);
  SetTextVisible(ctx.world, m_scrollDown, visible);
}

void ClubSelectionPanel::Shutdown(core::GameContext &ctx) {
  m_rowEntityOwner.DestroyAll(ctx.world);
  m_staticEntityOwner.DestroyAll(ctx.world);
  m_rows.clear();
  m_header = UINT32_MAX;
  m_scrollUp = UINT32_MAX;
  m_scrollDown = UINT32_MAX;
  m_landingPreviewBackground = UINT32_MAX;
  m_landingPreviewText = UINT32_MAX;
}

void ClubSelectionPanel::RebuildRows(
    core::GameContext &ctx, const std::vector<ClubUIData> &clubs) {
  m_rowEntityOwner.DestroyAll(ctx.world);
  m_rows.clear();
  m_rows.reserve(clubs.size());

  const float x = game::ui::kClubPanelX;
  float y = game::ui::kClubPanelY;
  const float width = game::ui::kClubItemW;
  const float height = game::ui::kClubItemH;

  for (const ClubUIData &club : clubs) {
    ClubRowEntities row;

    row.background = m_rowEntityOwner.Create(ctx.world);
    auto &background =
        ctx.world.Add<game::components::UIText>(row.background);
    background.x = x;
    background.y = y;
    background.width = width;
    background.height = height;
    ApplyRowStyle(background.style);
    background.visible = true;
    background.layer = game::ui::kLayerClubSelect;

    row.icon = m_rowEntityOwner.Create(ctx.world);
    auto &icon = ctx.world.Add<game::components::UIImage>(row.icon);
    icon = game::components::UIImage::Create(club.iconTexture, x + 8.0f,
                                              y + 6.0f);
    icon.width = height - 12.0f;
    icon.height = height - 12.0f;
    icon.visible = true;
    icon.layer = game::ui::kLayerClubSelect + 1;

    row.name = m_rowEntityOwner.Create(ctx.world);
    auto &name = ctx.world.Add<game::components::UIText>(row.name);
    name.text = core::ToWString(club.name);
    name.x = x + height + 10.0f;
    name.y = y + 7.0f;
    name.width = width - height - 30.0f;
    name.height = 18.0f;
    name.style = graphics::TextStyle::BrowserSub();
    name.style.fontFamily = "Meiryo";
    name.style.fontSize = game::ui::kClubNameFontSize;
    name.style.color = game::ui::kColorTextPrimary;
    name.style.align = graphics::TextAlign::Left;
    name.visible = true;
    name.layer = game::ui::kLayerClubSelect + 1;

    row.subName = m_rowEntityOwner.Create(ctx.world);
    auto &subName = ctx.world.Add<game::components::UIText>(row.subName);
    subName.text = core::ToWString(club.shortName + " " + club.categoryEN);
    subName.x = x + height + 10.0f;
    subName.y = y + 29.0f;
    subName.width = width - height - 30.0f;
    subName.height = 16.0f;
    subName.style = graphics::TextStyle::BrowserSub();
    subName.style.fontSize = 11.0f;
    subName.style.align = graphics::TextAlign::Left;
    subName.visible = true;
    subName.layer = game::ui::kLayerClubSelect + 1;

    m_rows.push_back(row);
    y += height + game::ui::kClubItemSpacing;
  }

  auto createHint = [&](const wchar_t *glyph, float rowY) {
    const ecs::Entity entity = m_rowEntityOwner.Create(ctx.world);
    auto &text = ctx.world.Add<game::components::UIText>(entity);
    text.text = glyph;
    text.x = x + width - 24.0f;
    text.y = rowY + height * 0.25f;
    text.width = 20.0f;
    text.height = 20.0f;
    text.style = graphics::TextStyle::Guide();
    text.style.fontSize = game::ui::kClubNameFontSize;
    text.style.color = game::ui::kColorAccent;
    text.style.align = graphics::TextAlign::Center;
    text.style.bgColor = {0.0f, 0.0f, 0.0f, 0.0f};
    text.style.borderWidth = 0.0f;
    text.visible = true;
    text.layer = game::ui::kLayerClubSelect + 2;
    return entity;
  };
  m_scrollUp = createHint(L"▲", game::ui::kClubPanelY);
  const float thirdSlotY =
      game::ui::kClubPanelY +
      2.0f * (game::ui::kClubItemH + game::ui::kClubItemSpacing);
  m_scrollDown = createHint(L"▼", thirdSlotY);
}

void ClubSelectionPanel::UpdateRow(core::GameContext &ctx,
                                   ClubRowEntities &row, int slot,
                                   bool selected, float elapsedTime) {
  row.visibleInWindow = slot >= 0;
  row.selected = selected;
  float verticalOffset = 0.0f;
  if (selected) {
    verticalOffset = std::sin(elapsedTime * 0.9f) * 2.2f;
  }
  const float rowY =
      game::ui::kClubPanelY +
      static_cast<float>(slot) *
          (game::ui::kClubItemH + game::ui::kClubItemSpacing);
  const float iconX = game::ui::kClubPanelX + 8.0f;
  const float textX =
      game::ui::kClubPanelX + game::ui::kClubItemH + 10.0f;

  auto *background = ctx.world.Get<game::components::UIText>(row.background);
  if (background) {
    background->visible = row.visibleInWindow;
    if (row.visibleInWindow) {
      background->x = game::ui::kClubPanelX;
      background->y = rowY + verticalOffset;
    }
    if (selected) {
      ApplyActiveRowStyle(background->style);
    } else {
      ApplyRowStyle(background->style);
      background->style.bgColor.w = 0.55f;
    }
  }

  auto *icon = ctx.world.Get<game::components::UIImage>(row.icon);
  if (icon) {
    icon->visible = row.visibleInWindow;
    if (row.visibleInWindow) {
      icon->x = iconX;
      icon->y = rowY + 6.0f + verticalOffset;
    }
    if (selected) {
      icon->alpha = 1.0f;
    } else {
      icon->alpha = 0.5f;
    }
  }

  auto *name = ctx.world.Get<game::components::UIText>(row.name);
  if (name) {
    name->visible = row.visibleInWindow;
    if (row.visibleInWindow) {
      name->x = textX;
      name->y = rowY + 7.0f + verticalOffset;
    }
    if (selected) {
      name->style.color.w = 1.0f;
    } else {
      name->style.color.w = 0.5f;
    }
  }

  auto *subName = ctx.world.Get<game::components::UIText>(row.subName);
  if (subName) {
    subName->visible = row.visibleInWindow;
    if (row.visibleInWindow) {
      subName->x = textX;
      subName->y = rowY + 29.0f + verticalOffset;
    }
    subName->style.color = game::ui::kColorTextSub;
    if (selected) {
      subName->style.color.w = 1.0f;
    } else {
      subName->style.color.w = 0.5f;
    }
  }
}

} // namespace game::controllers::hud

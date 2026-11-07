/**
 * @file GoalNoticePanel.cpp
 * @brief GoalNoticePanel の実装
*/

#include "GoalNoticePanel.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/UIText.h"
#include "../utils/UIConstants.h"

namespace game::controllers {

namespace {

constexpr int kLayerVeil = 26;

} // namespace

void GoalNoticePanel::Initialize(core::GameContext &ctx,
                                 const Content &content) {
  Shutdown(ctx);

  // ゲーム中のHUDと同じ表示。帰属表示も含むので、記事が見えている間ずっと出す
  m_courseInfo.Initialize(ctx);
  m_courseInfo.SetPages(ctx, content.currentTitle, content.goalTitle);
  m_courseInfo.SetVisible(ctx, true);
  // 紙が明るくなるのに合わせて出す（Update）
  m_courseInfoOpacity = 0.0f;
  m_courseInfo.SetOpacity(ctx, m_courseInfoOpacity);

  if (content.useVeil) {
    m_veil = m_entityOwner.Create(ctx.world);
    auto &text = ctx.world.Add<components::UIText>(m_veil);
    text.fullScreenCover = true;
    text.style.bgColor = game::ui::kColorBgDark;
    text.layer = kLayerVeil;
  }
}

void GoalNoticePanel::Update(core::GameContext &ctx, float elapsed,
                             const utils::ArticleArrivalTiming &timing) {
  if (auto *text = ctx.world.Get<components::UIText>(m_veil)) {
    text->opacity = utils::CalculatePaperOpacity(elapsed, timing);
    text->visible = text->opacity > 0.001f;
  }
  const float courseInfoOpacity =
      utils::CalculatePaperBrightness(elapsed, timing);
  if (courseInfoOpacity != m_courseInfoOpacity) {
    m_courseInfoOpacity = courseInfoOpacity;
    m_courseInfo.SetOpacity(ctx, m_courseInfoOpacity);
  }
}

void GoalNoticePanel::Shutdown(core::GameContext &ctx) {
  m_courseInfo.Shutdown(ctx);
  m_entityOwner.DestroyAll(ctx.world);
  m_veil = UINT32_MAX;
}

} // namespace game::controllers

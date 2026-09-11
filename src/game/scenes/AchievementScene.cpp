#include "AchievementScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../audio/AudioSystem.h"
#include "../components/UIButton.h"
#include "../components/UIText.h"
#include "../systems/AchievementManager.h"
#include "ModalSceneRender.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace game::scenes {

namespace {
constexpr int kLayer = 900;
constexpr int kVisibleRows = 6; // 1カラムに同時表示する実績数（スクロールで切替）
constexpr float kScrollStepPerNotch = 1.0f;

/** @brief 進捗表示が必要な実績について、現在値を取り出します。0なら進捗表示なし。*/
int CurrentProgressValue(game::systems::AchievementId id,
                         const game::systems::AchievementProgress &progress) {
  using game::systems::AchievementId;
  switch (id) {
  case AchievementId::TenClears:
  case AchievementId::HundredClears:
    return progress.totalClears;
  case AchievementId::DailyStreak3:
  case AchievementId::DailyStreak7:
    return progress.dailyStreak;
  case AchievementId::Explorer100Pages:
    return progress.totalPagesVisited;
  default:
    return 0;
  }
}

std::wstring FormatAchievementLine(
    const game::systems::AchievementDef &def,
    const game::systems::AchievementProgress &progress) {
  const bool unlocked = progress.IsUnlocked(def.id);
  std::wstring line = unlocked ? L"● " : L"○ ";
  line += def.name;
  if (def.targetValue > 0) {
    const int current =
        std::min(CurrentProgressValue(def.id, progress), def.targetValue);
    line += L" (" + std::to_wstring(current) + L"/" +
            std::to_wstring(def.targetValue) + L")";
  }
  line += L"\n   ";
  line += unlocked ? def.description : L"???";
  return line;
}

} // namespace

void AchievementScene::OnEnter(core::GameContext &ctx) {
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  auto backgroundEntity = CreateEntity(ctx.world);
  auto &background = ctx.world.Add<components::UIText>(backgroundEntity);
  background.x = 170.0f;
  background.y = 45.0f;
  background.width = 940.0f;
  background.height = 630.0f;
  background.style.bgColor = {0.025f, 0.06f, 0.12f, 0.98f};
  background.style.borderColor = {0.8f, 0.68f, 0.28f, 1.0f};
  background.style.borderWidth = 2.0f;
  background.style.cornerRadius = 18.0f;
  background.visible = true;
  background.layer = kLayer;

  auto titleEntity = CreateEntity(ctx.world);
  auto &title = ctx.world.Add<components::UIText>(titleEntity);
  title.text = L"実績";
  title.x = 200.0f;
  title.y = 65.0f;
  title.width = 880.0f;
  title.height = 50.0f;
  title.style.fontFamily = "Times New Roman";
  title.style.fontSize = 34.0f;
  title.style.align = graphics::TextAlign::Center;
  title.style.color = {1.0f, 0.9f, 0.55f, 1.0f};
  title.visible = true;
  title.layer = kLayer + 1;

  game::systems::AchievementProgress progress;
  int unlockedCount = 0;
  if (ctx.achievements) {
    progress = ctx.achievements->GetProgress();
    unlockedCount = static_cast<int>(progress.unlocked.size());
  }

  const auto &defs = game::systems::kAchievementDefs;
  const std::size_t leftCount = (defs.size() + 1) / 2;
  m_leftLines.clear();
  m_rightLines.clear();
  for (std::size_t i = 0; i < defs.size(); ++i) {
    std::vector<std::wstring> &target =
        (i < leftCount) ? m_leftLines : m_rightLines;
    target.push_back(FormatAchievementLine(defs[i], progress));
  }
  const int longestColumn = static_cast<int>(
      std::max(m_leftLines.size(), m_rightLines.size()));
  m_maxScrollOffset = std::max(0, longestColumn - kVisibleRows);
  m_scrollOffset = 0;

  auto summaryEntity = CreateEntity(ctx.world);
  auto &summary = ctx.world.Add<components::UIText>(summaryEntity);
  summary.text = std::to_wstring(unlockedCount) + L" / " +
                std::to_wstring(static_cast<int>(
                    game::systems::AchievementId::Count)) +
                L" 解除済み";
  if (m_maxScrollOffset > 0) {
    summary.text += L"　（マウスホイールでスクロール）";
  }
  summary.x = 200.0f;
  summary.y = 118.0f;
  summary.width = 880.0f;
  summary.height = 30.0f;
  summary.style.fontSize = 18.0f;
  summary.style.align = graphics::TextAlign::Center;
  summary.style.color = {0.85f, 0.9f, 0.95f, 1.0f};
  summary.visible = true;
  summary.layer = kLayer + 1;

  m_leftText = CreateEntity(ctx.world);
  auto &leftText = ctx.world.Add<components::UIText>(m_leftText);
  leftText.x = 200.0f;
  leftText.y = 160.0f;
  leftText.width = 420.0f;
  leftText.height = 490.0f;
  leftText.style.fontFamily = "Kiwi Maru Medium";
  leftText.style.fontSize = 15.0f;
  leftText.style.color = {0.92f, 0.94f, 0.97f, 1.0f};
  leftText.visible = true;
  leftText.layer = kLayer + 1;

  m_rightText = CreateEntity(ctx.world);
  auto &rightText = ctx.world.Add<components::UIText>(m_rightText);
  rightText.x = 660.0f;
  rightText.y = 160.0f;
  rightText.width = 420.0f;
  rightText.height = 490.0f;
  rightText.style.fontFamily = "Kiwi Maru Medium";
  rightText.style.fontSize = 15.0f;
  rightText.style.color = {0.92f, 0.94f, 0.97f, 1.0f};
  rightText.visible = true;
  rightText.layer = kLayer + 1;

  m_closeButton = CreateEntity(ctx.world);
  auto &closeButton = ctx.world.Add<components::UIButton>(m_closeButton);
  closeButton = components::UIButton::Create(L"戻る", "achievement_close",
                                             575.0f, 615.0f, 130.0f, 44.0f);
  closeButton.normalColor = {0.12f, 0.19f, 0.30f, 1.0f};
  closeButton.hoverColor = {0.75f, 0.58f, 0.18f, 1.0f};
  closeButton.pressedColor = {0.92f, 0.75f, 0.28f, 1.0f};
  closeButton.textStyle.fontSize = 20.0f;
  closeButton.textStyle.color = {1.0f, 1.0f, 1.0f, 1.0f};
  closeButton.visible = true;

  ctx.world.Query<components::UIButton>().Each(
      [&](ecs::Entity entity, components::UIButton &button) {
        if (!OwnsEntity(entity) && button.visible) {
          m_hiddenUnderlyingButtons.push_back(entity);
          button.visible = false;
        }
      });

  RefreshColumns(ctx);
}

void AchievementScene::RefreshColumns(core::GameContext &ctx) {
  const auto buildWindow = [&](const std::vector<std::wstring> &lines) {
    std::wstring text;
    const std::size_t begin = static_cast<std::size_t>(m_scrollOffset);
    const std::size_t end =
        std::min(lines.size(), begin + static_cast<std::size_t>(kVisibleRows));
    for (std::size_t i = begin; i < end; ++i) {
      if (!text.empty()) {
        text += L"\n\n";
      }
      text += lines[i];
    }
    return text;
  };

  if (auto *leftText = ctx.world.Get<components::UIText>(m_leftText)) {
    leftText->text = buildWindow(m_leftLines);
  }
  if (auto *rightText = ctx.world.Get<components::UIText>(m_rightText)) {
    rightText->text = buildWindow(m_rightLines);
  }
}

void AchievementScene::OnUpdate(core::GameContext &ctx) {
  ctx.world.Query<components::UIButton>().Each(
      [&](ecs::Entity entity, components::UIButton &button) {
        if (!OwnsEntity(entity) || !button.visible ||
            button.state != components::ButtonState::Pressed ||
            !ctx.input.GetMouseButtonDown(0)) {
          return;
        }
        if (button.action == "achievement_close") {
          if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
          ctx.sceneManager->PopScene();
        }
      });

  if (m_maxScrollOffset > 0) {
    const float wheel = ctx.input.GetMouseScrollDelta();
    if (wheel != 0.0f) {
      const int steps =
          static_cast<int>(std::round(wheel / kScrollStepPerNotch));
      if (steps != 0) {
        const int newOffset =
            std::clamp(m_scrollOffset - steps, 0, m_maxScrollOffset);
        if (newOffset != m_scrollOffset) {
          m_scrollOffset = newOffset;
          RefreshColumns(ctx);
        }
      }
    }
  }
}

void AchievementScene::Render(core::GameContext &ctx) {
  // タイトル画面のロゴ画像（UIImage）は通常描画パスでUIText/UIButtonより
  // 後に描かれるため、ここで自シーンの要素だけを直接・最後に描画して
  // 常に手前に表示する（RankingSceneと同じ共通処理）。
  RenderModalScene(ctx, *this, {0.18f, 0.18f, 0.18f, 0.62f});
}

void AchievementScene::OnExit(core::GameContext &ctx) {
  for (const ecs::Entity entity : m_hiddenUnderlyingButtons) {
    if (auto *button = ctx.world.Get<components::UIButton>(entity)) {
      button->visible = true;
    }
  }
  m_hiddenUnderlyingButtons.clear();
  DestroyAllEntities(ctx);
}

} // namespace game::scenes

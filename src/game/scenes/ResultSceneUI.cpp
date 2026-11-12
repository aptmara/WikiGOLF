/**
 * @file ResultSceneUI.cpp
 * @brief ResultSceneの責務別実装です。
*/

#define NOMINMAX
#include "ResultScene.h"
#include "../../core/BuildEdition.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/TextRenderer.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/PlayFabRules.h"
#include "TitleScene.h"
#include "WikiGolfScene.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace game::scenes {

using namespace game::components;
using namespace DirectX;

void ResultScene::CreateLuxuryUI(core::GameContext &ctx) {
  // スタイルパラメータを設定します。
  auto titleStyle = graphics::TextStyle::LuxuryTitle();
  auto statStyle = graphics::TextStyle::Status();
  auto btnStyle = graphics::TextStyle::LuxuryButton();

  // UI追加用のローカルヘルパー関数です。
  auto addUI = [&](const std::wstring &text, float y,
                   const graphics::TextStyle &style, bool isBtn = false,
                   const std::string &btnId = "") {
    LOG_DEBUG("ResultScene", "addUI: Adding {}", core::ToString(text));
    auto e = CreateEntity(ctx.world);

    if (isBtn) {
      auto &btn = ctx.world.Add<UIButton>(e);
      float btnW = 260.0f;
      float buttonCenterX = 780.0f;
      if (text == L"Play Again (R)") {
        buttonCenterX = 420.0f;
      }
      if constexpr (core::kTrialBuild) {
        // 体験版は「もう一度」が無いので、タイトルへ戻るボタンを中央に置く。
        buttonCenterX = 640.0f;
      }
      float btnX = buttonCenterX - btnW / 2.0f;
      if (btnId == "center")
        btnX = 640.0f - btnW / 2.0f;

      btn = UIButton::Create(text, btnId, btnX, y, btnW, 60.0f);
      if (btnId == "route") {
        btn.x = 480.0f;
        btn.width = 320.0f;
        btn.height = 48.0f;
      }
      btn.textStyle = style;
      btn.textStyle.fontSize = 26.0f;
      btn.normalColor = {0.1f, 0.1f, 0.1f, 0.8f};
      btn.hoverColor = {0.2f, 0.2f, 0.3f, 0.9f};
      btn.visible = true;
    } else {
      auto &txt = ctx.world.Add<UIText>(e);
      txt.text = text;
      txt.style = style;
      txt.x = 40.0f;
      txt.y = y;
      txt.width = 1200.0f;
      txt.visible = true;
      txt.layer = 10;
    }

    UIElement elem;
    elem.entity = e;
    elem.baseX = 0.0f;
    elem.baseY = y;
    elem.currentScale = 1.0f;
    elem.targetScale = 1.0f;
    elem.text = text;
    elem.isHovered = false;
    elem.baseColor = {1, 1, 1, 1};
    m_uiElements.push_back(elem);
    LOG_DEBUG("ResultScene", "addUI: Added {} successfully", core::ToString(text));
  };

  // ステージクリアのメインタイトルを追加します。
  addUI(L"STAGE CLEAR", 120.0f, titleStyle);

  // 打数に基づきクリア評価ランクを決定します。
  std::wstring grade = L"EXPLORER";
  DirectX::XMFLOAT4 gradeColor = {0.9f, 0.9f, 0.95f, 1.0f};
  int diff = m_data.shotCount - m_data.par;
  // パーとの差をゴルフの呼び名に合わせる(ラウンド通算の打数とラウンドパー)。
  if (m_data.par > 0) {
    if (m_data.shotCount == 1) {
      grade = L"HOLE IN ONE";
      gradeColor = {1.0f, 0.92f, 0.55f, 1.0f};
    } else if (diff <= -3) {
      grade = L"ALBATROSS";
      gradeColor = {1.0f, 0.92f, 0.55f, 1.0f};
    } else if (diff == -2) {
      grade = L"EAGLE";
      gradeColor = {1.0f, 0.8f, 0.8f, 1.0f};
    } else if (diff == -1) {
      grade = L"BIRDIE";
      gradeColor = {0.7f, 1.0f, 0.7f, 1.0f};
    } else if (diff == 0) {
      grade = L"PAR";
      gradeColor = {0.6f, 0.85f, 1.0f, 1.0f};
    } else if (diff == 1) {
      grade = L"BOGEY";
      gradeColor = {1.0f, 0.85f, 0.6f, 1.0f};
    } else if (diff == 2) {
      grade = L"DOUBLE BOGEY";
      gradeColor = {1.0f, 0.75f, 0.6f, 1.0f};
    } else {
      grade = L"+" + std::to_wstring(diff);
      gradeColor = {1.0f, 0.65f, 0.65f, 1.0f};
    }
  }

  // 評価ランクのバッジUIを生成します。
  auto badgeE = CreateEntity(ctx.world);
  auto &badge = ctx.world.Add<UIText>(badgeE);
  badge.text = grade;
  badge.style = statStyle;
  badge.style.fontSize = 48.0f;
  badge.style.align = graphics::TextAlign::Center;
  badge.style.color = gradeColor;
  badge.style.hasOutline = true;
  badge.style.outlineColor = {0.0f, 0.0f, 0.0f, 1.0f};
  badge.style.outlineWidth = 3.0f;
  badge.x = 0.0f;
  badge.width = 1280.0f;
  badge.y = 220.0f;
  badge.visible = true;
  badge.layer = 11;

  UIElement bElem;
  bElem.entity = badgeE;
  bElem.baseX = 0.0f;
  bElem.baseY = 220.0f;
  bElem.currentScale = 0.0f;
  bElem.targetScale = 1.0f;
  bElem.text = grade;
  bElem.isHovered = false;
  bElem.baseColor = gradeColor;
  m_uiElements.push_back(bElem);

  // 目的地ページ名を表示します。日本語の記事名を含むため丸ゴシックにする。
  auto subStyle = graphics::TextStyle::ModernBlack();
  subStyle.fontFamily = "Kiwi Maru Medium";
  subStyle.color = {0.8f, 0.8f, 0.9f, 1.0f};
  subStyle.hasShadow = true;
  subStyle.align = graphics::TextAlign::Center;
  addUI(L"Target: " + core::ToWString(m_data.targetPage), 290.0f, subStyle);

  size_t hops = 0;
  if (!m_data.pathHistory.empty()) {
    hops = m_data.pathHistory.size() - 1;
  }

  // スコア統計値テキストを追加します。
  statStyle.align = graphics::TextAlign::Center;
  std::wstring stats = L"Shots: " + std::to_wstring(m_data.shotCount);
  if (m_data.par > 0) {
    stats += L" (Par " + std::to_wstring(m_data.par) + L")";
  }
  stats += L"  |  Hops: " + std::to_wstring(hops);
  if (m_data.isDailyChallenge) {
    stats += L"  |  Time: " +
             game::systems::FormatClearTime(m_data.clearTimeMs);
  }
  addUI(stats, 340.0f, statStyle);

  if constexpr (!core::kTrialBuild) {
    if (m_data.isDailyChallenge) {
      m_rankingStatusEntity = CreateEntity(ctx.world);
      auto &rankingStatus =
          ctx.world.Add<components::UIText>(m_rankingStatusEntity);
      switch (m_data.dailyPracticeReason) {
      case game::systems::DailyPracticeReason::None:
        rankingStatus.text = L"オンラインランキングへ送信中...";
        break;
      case game::systems::DailyPracticeReason::AlreadyPlayedToday:
        rankingStatus.text =
            L"今日の2回目以降は練習です。ランキングには送信しません";
        break;
      case game::systems::DailyPracticeReason::SafeMode:
        rankingStatus.text =
            L"セーフモード中は条件が違うため、ランキングには送信しません";
        break;
      case game::systems::DailyPracticeReason::UnknownDate:
        rankingStatus.text =
            L"日付を確認できなかったため、ランキングには送信しません";
        break;
      }
      rankingStatus.x = 0.0f;
      rankingStatus.y = 445.0f;
      rankingStatus.width = 1280.0f;
      rankingStatus.height = 30.0f;
      rankingStatus.style = statStyle;
      rankingStatus.style.fontSize = 18.0f;
      rankingStatus.style.align = graphics::TextAlign::Center;
      rankingStatus.visible = true;
      rankingStatus.layer = 10;
    }
  }

  // 遷移経路文字列のテキストを追加します。日本語の記事名を含むため丸ゴシックにする。
  auto routeStyle = statStyle;
  routeStyle.fontFamily = "Kiwi Maru Medium";
  routeStyle.align = graphics::TextAlign::Center;
  routeStyle.fontSize = 22.0f;
  routeStyle.fontSize = 24.0f;
  routeStyle.color = {1.0f, 0.9f, 0.55f, 1.0f};
  addUI(L"最短記事を見る", 388.0f, routeStyle, true, "route");

  auto attributionStyle = routeStyle;
  attributionStyle.fontSize = 15.0f;
  addUI(L"記事本文: Wikipedia (CC BY-SA 4.0)。レイアウト変更や一部省略をしています。",
        630.0f, attributionStyle);

  // 操作ボタンを追加します。
  if constexpr (core::kTrialBuild) {
    // 体験版はチュートリアル後の1プレイで終了する。
    auto trialStyle = statStyle;
    trialStyle.fontFamily = "Kiwi Maru Medium";
    trialStyle.align = graphics::TextAlign::Center;
    trialStyle.color = {1.0f, 0.9f, 0.3f, 1.0f};
    addUI(L"体験版はここまでです。遊んでいただきありがとうございました！",
          490.0f, trialStyle);
  } else {
    addUI(L"Play Again (R)", 550.0f, btnStyle, true, "retry");
  }
  addUI(L"Title Screen", 550.0f, btnStyle, true, "title");
}

/**
 * @brief シーン終了時のクリーンアップ処理を行います。
*/

} // namespace game::scenes

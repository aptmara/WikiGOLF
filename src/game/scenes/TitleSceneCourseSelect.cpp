#include "ResultScene.h"
#include "TitleScene.h"
#include "TitleSceneSupport.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../graphics/VideoPlayer.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/SkyboxTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIButton.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/SkyboxRenderSystem.h"
#include "../systems/TerrainGenerator.h"
#include "../systems/WikiClient.h"
#include "../../core/StringUtils.h"
#include "LoadingScene.h"
#include "SettingsScene.h"
#include "WikiGolfScene.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <shellapi.h> // ShellExecuteA用


namespace game::scenes {

using namespace DirectX;

/**
 * @brief コース選択用UIを生成します。
 */
void TitleScene::CreateCourseSelectUI(core::GameContext& ctx) {
  // 背景半透明パネル
  m_csBgEntity = CreateEntity(ctx.world);
  auto& bg = ctx.world.Add<components::UIText>(m_csBgEntity);
  bg.text = L""; bg.x = 240.0f; bg.y = 100.0f; bg.width = 800.0f; bg.height = 520.0f;
  bg.style.bgColor = {0.05f, 0.1f, 0.15f, 0.95f}; bg.style.cornerRadius = 16.0f;
  bg.style.borderWidth = 2.0f; bg.style.borderColor = {0.8f, 0.7f, 0.3f, 1.0f};
  bg.layer = 200; bg.visible = false;

  // タイトル
  m_csTitleEntity = CreateEntity(ctx.world);
  auto& title = ctx.world.Add<components::UIText>(m_csTitleEntity);
  title.text = L"コース選択"; title.x = 240.0f; title.y = 120.0f; title.width = 800.0f;
  title.style.fontSize = 32.0f; title.style.color = {1.0f, 0.95f, 0.7f, 1.0f};
  title.style.align = graphics::TextAlign::Center;
  title.layer = 201; title.visible = false;

  // スタート入力枠
  m_startInputBg = CreateEntity(ctx.world);
  auto& stBg = ctx.world.Add<components::UIText>(m_startInputBg);
  stBg.text = L"Start Page:"; stBg.x = 280.0f; stBg.y = 180.0f; stBg.width = 650.0f; stBg.height = 40.0f;
  stBg.style.fontSize = 20.0f; stBg.style.color = {0.7f, 0.7f, 0.7f, 1.0f};
  stBg.style.bgColor = {0.0f, 0.0f, 0.0f, 0.8f}; stBg.style.cornerRadius = 8.0f;
  stBg.style.borderWidth = 1.0f; stBg.style.borderColor = {0.5f, 0.5f, 0.5f, 1.0f};
  stBg.layer = 201; stBg.visible = false;

  m_startInputText = CreateEntity(ctx.world);
  auto& stTxt = ctx.world.Add<components::UIText>(m_startInputText);
  stTxt.text = L""; stTxt.x = 420.0f; stTxt.y = 188.0f; stTxt.width = 500.0f;
  stTxt.style.fontSize = 20.0f; stTxt.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  stTxt.layer = 202; stTxt.visible = false;

  // スタート貼り付けボタン
  m_startPasteBtn = CreateEntity(ctx.world);
  auto& stPaste = ctx.world.Add<components::UIButton>(m_startPasteBtn);
  stPaste.label = L" 貼付"; stPaste.action = "cs_paste_start";
  stPaste.x = 940.0f; stPaste.y = 180.0f; stPaste.width = 80.0f; stPaste.height = 40.0f;
  stPaste.textStyle.fontSize = 18.0f; stPaste.textStyle.align = graphics::TextAlign::Center;
  stPaste.normalColor = {0.2f, 0.2f, 0.3f, 1.0f}; stPaste.hoverColor = {0.3f, 0.3f, 0.5f, 1.0f};
  stPaste.pressedColor = {0.1f, 0.1f, 0.2f, 1.0f};
  stPaste.visible = false;

  // ゴール入力枠
  m_goalInputBg = CreateEntity(ctx.world);
  auto& glBg = ctx.world.Add<components::UIText>(m_goalInputBg);
  glBg.text = L"Goal Page:"; glBg.x = 280.0f; glBg.y = 240.0f; glBg.width = 650.0f; glBg.height = 40.0f;
  glBg.style.fontSize = 20.0f; glBg.style.color = {0.7f, 0.7f, 0.7f, 1.0f};
  glBg.style.bgColor = {0.0f, 0.0f, 0.0f, 0.8f}; glBg.style.cornerRadius = 8.0f;
  glBg.style.borderWidth = 1.0f; glBg.style.borderColor = {0.5f, 0.5f, 0.5f, 1.0f};
  glBg.layer = 201; glBg.visible = false;

  m_goalInputText = CreateEntity(ctx.world);
  auto& glTxt = ctx.world.Add<components::UIText>(m_goalInputText);
  glTxt.text = L""; glTxt.x = 420.0f; glTxt.y = 248.0f; glTxt.width = 500.0f;
  glTxt.style.fontSize = 20.0f; glTxt.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  glTxt.layer = 202; glTxt.visible = false;

  // ゴール貼り付けボタン
  m_goalPasteBtn = CreateEntity(ctx.world);
  auto& glPaste = ctx.world.Add<components::UIButton>(m_goalPasteBtn);
  glPaste.label = L" 貼付"; glPaste.action = "cs_paste_goal";
  glPaste.x = 940.0f; glPaste.y = 240.0f; glPaste.width = 80.0f; glPaste.height = 40.0f;
  glPaste.textStyle.fontSize = 18.0f; glPaste.textStyle.align = graphics::TextAlign::Center;
  glPaste.normalColor = {0.2f, 0.2f, 0.3f, 1.0f}; glPaste.hoverColor = {0.3f, 0.3f, 0.5f, 1.0f};
  glPaste.pressedColor = {0.1f, 0.1f, 0.2f, 1.0f};
  glPaste.visible = false;

  // プレビュー領域
  m_previewBg = CreateEntity(ctx.world);
  auto& prBg = ctx.world.Add<components::UIText>(m_previewBg);
  prBg.text = L""; prBg.x = 280.0f; prBg.y = 300.0f; prBg.width = 740.0f; prBg.height = 220.0f;
  prBg.style.bgColor = {0.0f, 0.0f, 0.0f, 0.6f}; prBg.style.cornerRadius = 8.0f;
  prBg.layer = 201; prBg.visible = false;

  m_previewText = CreateEntity(ctx.world);
  auto& prTxt = ctx.world.Add<components::UIText>(m_previewText);
  prTxt.text = L"ここにプレビューが表示されます\n（未確認）"; prTxt.x = 300.0f; prTxt.y = 320.0f; prTxt.width = 700.0f;
  prTxt.style.fontSize = 16.0f; prTxt.style.color = {0.8f, 0.8f, 0.8f, 1.0f};
  prTxt.layer = 202; prTxt.visible = false;

  // ボタン類
  m_checkBtn = CreateEntity(ctx.world);
  auto& chk = ctx.world.Add<components::UIButton>(m_checkBtn);
  chk.label = L"疎通確認"; chk.action = "cs_check";
  chk.x = 280.0f; chk.y = 510.0f; chk.width = 200.0f; chk.height = 50.0f;
  chk.textStyle.fontSize = 24.0f; chk.textStyle.align = graphics::TextAlign::Center;
  chk.normalColor = {0.1f, 0.3f, 0.6f, 1.0f}; chk.hoverColor = {0.2f, 0.5f, 0.9f, 1.0f}; chk.pressedColor = {0.1f, 0.2f, 0.5f, 1.0f};
  chk.visible = false;

  m_startBtn = CreateEntity(ctx.world);
  auto& st = ctx.world.Add<components::UIButton>(m_startBtn);
  st.label = L"スタート (確認未)"; st.action = "cs_start";
  st.x = 540.0f; st.y = 510.0f; st.width = 200.0f; st.height = 50.0f;
  st.textStyle.fontSize = 20.0f; st.textStyle.align = graphics::TextAlign::Center;
  st.normalColor = {0.3f, 0.3f, 0.3f, 1.0f}; st.hoverColor = {0.3f, 0.3f, 0.3f, 1.0f}; st.pressedColor = {0.3f, 0.3f, 0.3f, 1.0f};
  st.state = components::ButtonState::Disabled;
  st.visible = false;

  m_closeBtn = CreateEntity(ctx.world);
  auto& cls = ctx.world.Add<components::UIButton>(m_closeBtn);
  cls.label = L"閉じる"; cls.action = "cs_close";
  cls.x = 800.0f; cls.y = 510.0f; cls.width = 200.0f; cls.height = 50.0f;
  cls.textStyle.fontSize = 24.0f; cls.textStyle.align = graphics::TextAlign::Center;
  cls.normalColor = {0.6f, 0.2f, 0.2f, 1.0f}; cls.hoverColor = {0.8f, 0.3f, 0.3f, 1.0f}; cls.pressedColor = {0.5f, 0.1f, 0.1f, 1.0f};
  cls.visible = false;
}

/**
 * @brief メインメニューの表示状態を切り替えます。
 */
void TitleScene::SetMainMenuVisible(core::GameContext& ctx, bool visible) {
  ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (btn.action != "cs_check" && btn.action != "cs_start" && btn.action != "cs_close" && btn.action != "cs_paste_start" && btn.action != "cs_paste_goal") {
      btn.visible = visible;
    }
  });
}

/**
 * @brief コース選択UIの表示状態を切り替えます。
 */
void TitleScene::SetCourseSelectVisible(core::GameContext& ctx, bool visible) {
  if (auto* bg = ctx.world.Get<components::UIText>(m_csBgEntity)) bg->visible = visible;
  if (auto* title = ctx.world.Get<components::UIText>(m_csTitleEntity)) title->visible = visible;
  if (auto* stBg = ctx.world.Get<components::UIText>(m_startInputBg)) stBg->visible = visible;
  if (auto* stTxt = ctx.world.Get<components::UIText>(m_startInputText)) stTxt->visible = visible;
  if (auto* glBg = ctx.world.Get<components::UIText>(m_goalInputBg)) glBg->visible = visible;
  if (auto* glTxt = ctx.world.Get<components::UIText>(m_goalInputText)) glTxt->visible = visible;
  if (auto* prevBg = ctx.world.Get<components::UIText>(m_previewBg)) prevBg->visible = visible;
  if (auto* prevTxt = ctx.world.Get<components::UIText>(m_previewText)) prevTxt->visible = visible;

  ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (btn.action == "cs_check" || btn.action == "cs_start" || btn.action == "cs_close" || btn.action == "cs_paste_start" || btn.action == "cs_paste_goal") {
      btn.visible = visible;
    }
  });
}

/**
 * @brief コース選択UIの毎フレーム更新処理を行います。
 */
void TitleScene::UpdateCourseSelect(core::GameContext& ctx) {
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
    }
    SetCourseSelectVisible(ctx, false);
    SetMainMenuVisible(ctx, true);
    m_state = TitleState::MainMenu;
    m_focusIndex = 0;
    return;
  }

  // フォーカス切り替え（マウスクリック）
  if (ctx.input.GetMouseButtonDown(0)) {
    auto mousePos = ctx.input.GetMousePosition();
    float mx = (float)mousePos.x;
    float my = (float)mousePos.y;

    if (mx >= 280 && mx <= 930 && my >= 180 && my <= 220) m_focusIndex = 1;
    else if (mx >= 280 && mx <= 930 && my >= 240 && my <= 280) m_focusIndex = 2;
    else {
      // UIButton 以外の場所をクリックしたらフォーカス外す処理
      bool onButton = false;
      ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
        if (btn.visible && mx >= btn.x && mx <= btn.x + btn.width && my >= btn.y && my <= btn.y + btn.height) {
          onButton = true;
        }
      });
      if (!onButton) m_focusIndex = 0;
    }
  }

  // 入力反映
  if (m_focusIndex == 1 || m_focusIndex == 2) {
    std::wstring* targetStr = &m_goalString;
    if (m_focusIndex == 1) {
      targetStr = &m_startString;
    }
    const std::wstring& inChars = ctx.input.GetInputChars();
    if (ctx.input.GetBackspacePressed() && !targetStr->empty()) {
      targetStr->pop_back();
      m_readyToStart = false; // 変更があったら再確認
    }
    if (!inChars.empty()) {
      *targetStr += inChars;
      m_readyToStart = false;
    }

    if (auto* bg1 = ctx.world.Get<components::UIText>(m_startInputBg)) {
      if (m_focusIndex == 1) {
        bg1->style.borderColor = DirectX::XMFLOAT4{1, 1, 1, 1};
      } else {
        bg1->style.borderColor = DirectX::XMFLOAT4{0.5f, 0.5f, 0.5f, 1};
      }
    }
    if (auto* bg2 = ctx.world.Get<components::UIText>(m_goalInputBg)) {
      if (m_focusIndex == 2) {
        bg2->style.borderColor = DirectX::XMFLOAT4{1, 1, 1, 1};
      } else {
        bg2->style.borderColor = DirectX::XMFLOAT4{0.5f, 0.5f, 0.5f, 1};
      }
    }
  }

  // カーソル点滅表示
  std::wstring cursor;
  if (static_cast<int>(ctx.time * 2.0f) % 2 == 0) {
    cursor = L"_";
  }
  if (auto* t1 = ctx.world.Get<components::UIText>(m_startInputText)) {
    t1->text = m_startString;
    if (m_focusIndex == 1) {
      t1->text += cursor;
    }
  }
  if (auto* t2 = ctx.world.Get<components::UIText>(m_goalInputText)) {
    t2->text = m_goalString;
    if (m_focusIndex == 2) {
      t2->text += cursor;
    }
  }

  // 非同期確認完了のチェック
  if (m_checking && m_checkTask.valid() && m_checkTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    m_checking = false;
    std::string result = m_checkTask.get();
    if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) {
      if (!result.empty() && result != "ERROR" && result != "(Failed to fetch extract)") {
        LOG_INFO("TitleScene", "Course check success.");
        p->text = core::ToWString(result);
        m_readyToStart = true;
      } else {
        LOG_ERROR("TitleScene", "Course check failed.");
        p->text = L"ページが見つからないか、通信エラーが発生しました。";
        m_readyToStart = false;
      }
    }
    if (auto* stBtn = ctx.world.Get<components::UIButton>(m_startBtn)) {
      if (m_readyToStart) {
        stBtn->label = L"スタート！";
        stBtn->state = components::ButtonState::Normal;
        stBtn->normalColor = {0.2f, 0.7f, 0.2f, 1.0f};
        stBtn->hoverColor = {0.3f, 0.9f, 0.3f, 1.0f};
      } else {
        stBtn->label = L"スタート (確認未)";
        stBtn->state = components::ButtonState::Disabled;
        stBtn->normalColor = {0.3f, 0.3f, 0.3f, 1.0f};
      }
    }
  }

  // ボタン処理
  bool doClose = false;
  bool doStart = false;
  ctx.world.Query<components::UIButton>().Each([&](ecs::Entity, components::UIButton &btn) {
    if (!btn.visible || btn.state == components::ButtonState::Disabled) return;
    if (btn.state == components::ButtonState::Pressed && ctx.input.GetMouseButtonDown(0)) {
      if (btn.action == "cs_close") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
        doClose = true;
      } else if (btn.action == "cs_paste_start") {
        std::wstring cb = ctx.input.GetClipboardText();
        cb.erase(std::remove(cb.begin(), cb.end(), L'\r'), cb.end());
        cb.erase(std::remove(cb.begin(), cb.end(), L'\n'), cb.end());
        m_startString = cb;
        m_focusIndex = 1;
        m_readyToStart = false;
      } else if (btn.action == "cs_paste_goal") {
        std::wstring cb = ctx.input.GetClipboardText();
        cb.erase(std::remove(cb.begin(), cb.end(), L'\r'), cb.end());
        cb.erase(std::remove(cb.begin(), cb.end(), L'\n'), cb.end());
        m_goalString = cb;
        m_focusIndex = 2;
        m_readyToStart = false;
      } else if (btn.action == "cs_check") {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        if (!m_checking && !m_startString.empty() && !m_goalString.empty()) {
          std::string startUtf8 = title_scene_detail::ExtractWikiTitle(core::ToString(m_startString));
          std::string goalUtf8 = title_scene_detail::ExtractWikiTitle(core::ToString(m_goalString));

          if (startUtf8 == goalUtf8) {
            if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) p->text = L"スタートとゴールに同じ記事は指定できません。";
            m_readyToStart = false;
          } else {
            m_checking = true;
            if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) p->text = L"確認中...";
            m_checkTask = std::async(std::launch::async, [startUtf8, goalUtf8]() {
              auto trimW = [](const std::string& str, size_t maxLen) {
                std::wstring w = core::ToWString(str);
                if (w.length() <= maxLen) return core::ToString(w);
                return core::ToString(w.substr(0, maxLen) + L"...");
              };
              game::systems::WikiClient wiki;
              std::string extStart = wiki.FetchPageExtract(startUtf8, 100);
              if (extStart.empty() || extStart == "ERROR" || extStart == "(Failed to fetch extract)") return std::string("ERROR");
              std::string extGoal = wiki.FetchPageExtract(goalUtf8, 100);
              if (extGoal.empty() || extGoal == "ERROR" || extGoal == "(Failed to fetch extract)") return std::string("ERROR");
              return "【" + startUtf8 + "】\n" + trimW(extStart, 80) + "\n\n【" + goalUtf8 + "】\n" + trimW(extGoal, 80);
            });
          }
        } else if (m_startString.empty() || m_goalString.empty()) {
            if (auto* p = ctx.world.Get<components::UIText>(m_previewText)) p->text = L"スタートとゴールの両方を入力してください。";
        }
      } else if (btn.action == "cs_start" && m_readyToStart) {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_hard.mp3", 0.5f);
        doStart = true;
      }
    }
  });

  if (doClose) {
    SetCourseSelectVisible(ctx, false);
    SetMainMenuVisible(ctx, true);
    m_state = TitleState::MainMenu;
    m_focusIndex = 0;
  }
  if (doStart) {
    StopIntroAudio(ctx);
    game::components::WikiGlobalData data;
    data.startPage = title_scene_detail::ExtractWikiTitle(core::ToString(m_startString));
    data.targetPage = title_scene_detail::ExtractWikiTitle(core::ToString(m_goalString));
    data.targetPageId = -1;
    data.isUserOverride = true;
    ctx.world.SetGlobal(std::move(data));

    LOG_INFO("TitleScene", "Starting game with Start: '{}', Goal: '{}'", title_scene_detail::ExtractWikiTitle(core::ToString(m_startString)), title_scene_detail::ExtractWikiTitle(core::ToString(m_goalString)));

    auto loadingScene = std::make_unique<LoadingScene>([]() { return std::make_unique<WikiGolfScene>(); });
    ctx.sceneManager->ChangeScene(std::move(loadingScene));
  }
}

} // namespace game::scenes


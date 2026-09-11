#include "RankingScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../core/StringUtils.h"
#include "../../graphics/TextRenderer.h"
#include "../components/UIButton.h"
#include "../components/UIText.h"
#include "../systems/AchievementEvent.h"
#include "../systems/AchievementEventBus.h"
#include "../systems/PlayFabRules.h"
#include "ModalSceneRender.h"
#include <algorithm>
#include <thread>

namespace game::scenes {

namespace {
constexpr int kLayer = 900;

std::wstring EntryName(const game::systems::PlayFabLeaderboardEntry &entry) {
  if (!entry.displayName.empty()) {
    return core::ToWString(entry.displayName);
  }
  const std::string suffix =
      entry.playFabId.size() > 6
          ? entry.playFabId.substr(entry.playFabId.size() - 6)
          : entry.playFabId;
  return L"Player-" + core::ToWString(suffix);
}
} // namespace

void RankingScene::OnEnter(core::GameContext &ctx) {
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
  title.text = L"ONLINE RANKING  |  DAILY CHALLENGE";
  title.x = 200.0f;
  title.y = 75.0f;
  title.width = 880.0f;
  title.height = 50.0f;
  title.style.fontFamily = "Times New Roman";
  title.style.fontSize = 34.0f;
  title.style.align = graphics::TextAlign::Center;
  title.style.color = {1.0f, 0.9f, 0.55f, 1.0f};
  title.visible = true;
  title.layer = kLayer + 1;

  const auto createButton = [&](const std::wstring &label,
                                const std::string &action, float x, float y,
                                float width) {
    const auto entity = CreateEntity(ctx.world);
    auto &button = ctx.world.Add<components::UIButton>(entity);
    button = components::UIButton::Create(label, action, x, y, width, 44.0f);
    button.normalColor = {0.12f, 0.19f, 0.30f, 1.0f};
    button.hoverColor = {0.75f, 0.58f, 0.18f, 1.0f};
    button.pressedColor = {0.92f, 0.75f, 0.28f, 1.0f};
    button.textStyle.fontSize = 20.0f;
    button.textStyle.color = {1.0f, 1.0f, 1.0f, 1.0f};
    button.visible = true;
    return entity;
  };

  m_strokesButton = createButton(L"最少打数", "ranking_strokes", 315.0f,
                                 140.0f, 220.0f);
  m_timeButton = createButton(L"最短クリア時間", "ranking_time", 545.0f,
                              140.0f, 260.0f);
  createButton(L"戻る", "ranking_close", 835.0f, 140.0f, 130.0f);

  m_statusText = CreateEntity(ctx.world);
  auto &status = ctx.world.Add<components::UIText>(m_statusText);
  status.x = 220.0f;
  status.y = 202.0f;
  status.width = 840.0f;
  status.height = 38.0f;
  status.style.fontSize = 18.0f;
  status.style.align = graphics::TextAlign::Center;
  status.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  status.visible = true;
  status.layer = kLayer + 1;

  m_rowsText = CreateEntity(ctx.world);
  auto &rows = ctx.world.Add<components::UIText>(m_rowsText);
  rows.x = 265.0f;
  rows.y = 250.0f;
  rows.width = 750.0f;
  rows.height = 350.0f;
  rows.style.fontFamily = "Kiwi Maru Medium";
  rows.style.fontSize = 22.0f;
  rows.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  rows.visible = true;
  rows.layer = kLayer + 1;

  m_nameInputText = CreateEntity(ctx.world);
  auto &nameInput = ctx.world.Add<components::UIText>(m_nameInputText);
  nameInput.x = 355.0f;
  nameInput.y = 330.0f;
  nameInput.width = 570.0f;
  nameInput.height = 50.0f;
  nameInput.style.bgColor = {0.08f, 0.12f, 0.20f, 1.0f};
  nameInput.style.borderColor = {0.65f, 0.70f, 0.82f, 1.0f};
  nameInput.style.borderWidth = 2.0f;
  nameInput.style.cornerRadius = 8.0f;
  nameInput.style.fontSize = 24.0f;
  nameInput.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  nameInput.style.valign = graphics::TextVAlign::Middle;
  nameInput.visible = false;
  nameInput.layer = kLayer + 2;

  m_nameSubmitButton = createButton(L"この名前で開始", "ranking_name_submit",
                                    490.0f, 405.0f, 300.0f);
  ctx.world.Get<components::UIButton>(m_nameSubmitButton)->visible = false;

  m_profile = game::systems::PlayFabClient::LoadOrCreateProfile();
  m_enteringName = m_profile.displayName.empty();
  if (m_enteringName) {
    if (auto *currentStatus =
            ctx.world.Get<components::UIText>(m_statusText)) {
      currentStatus->text =
          L"初回登録: ランキングに表示する名前を入力してください（3～25文字）";
    }
    nameInput.visible = true;
    ctx.world.Get<components::UIButton>(m_nameSubmitButton)->visible = true;
  } else {
    StartLoad();
  }
  RefreshUI(ctx);
}

void RankingScene::StartLoad(const std::string &displayName) {
  m_loading = true;
  m_wasRegisteringName = !displayName.empty();
  m_pendingDisplayName = displayName;
  m_asyncState = std::make_shared<AsyncState>();
  const auto state = m_asyncState;
  std::thread([state, displayName]() {
    game::systems::PlayFabClient client;
    if (!displayName.empty()) {
      state->data.nameUpdate = client.UpdateDisplayName(displayName);
      if (!state->data.nameUpdate.success) {
        state->completed.store(true, std::memory_order_release);
        return;
      }
    } else {
      state->data.nameUpdate.success = true;
    }
    state->data.strokes = client.FetchLeaderboard(
        game::systems::PlayFabClient::kStrokesStatistic);
    state->data.clearTime = client.FetchLeaderboard(
        game::systems::PlayFabClient::kClearTimeStatistic);
    state->completed.store(true, std::memory_order_release);
  }).detach();
}

void RankingScene::RefreshUI(core::GameContext &ctx) {
  auto *status = ctx.world.Get<components::UIText>(m_statusText);
  auto *rows = ctx.world.Get<components::UIText>(m_rowsText);
  if (!status || !rows) {
    return;
  }
  if (m_enteringName) {
    rows->text = L"";
    return;
  }
  if (m_loading) {
    status->text = L"PlayFabからランキングを取得中...";
    rows->text = L"";
    return;
  }

  const auto &result =
      m_view == View::Strokes ? m_data.strokes : m_data.clearTime;
  if (!result.success) {
    status->text = L"ランキングを取得できませんでした";
    rows->text = core::ToWString(result.errorMessage);
    return;
  }

  status->text = m_view == View::Strokes
                     ? L"本日の最少打数ランキング"
                     : L"本日の最短クリア時間ランキング";
  std::wstring text;
  for (const auto &entry : result.entries) {
    text += std::to_wstring(entry.position) + L".  " + EntryName(entry) +
            L"    ";
    if (m_view == View::Strokes) {
      text += std::to_wstring(entry.value) + L" 打";
    } else {
      text += game::systems::FormatClearTime(entry.value);
    }
    text += L"\n";
  }
  if (text.empty()) {
    text = L"まだ記録がありません。";
  }
  rows->text = std::move(text);
}

void RankingScene::OnUpdate(core::GameContext &ctx) {
  if (m_enteringName) {
    if (ctx.input.GetBackspacePressed() && !m_nameInput.empty()) {
      m_nameInput.pop_back();
    }
    const std::wstring &input = ctx.input.GetInputChars();
    for (const wchar_t character : input) {
      if (m_nameInput.size() >= 25 || character == L'\r' ||
          character == L'\n' || character == L'\t') {
        continue;
      }
      m_nameInput += character;
    }
    if (auto *text = ctx.world.Get<components::UIText>(m_nameInputText)) {
      text->text = m_nameInput + L"_";
    }
  }

  if (m_loading && m_asyncState &&
      m_asyncState->completed.load(std::memory_order_acquire)) {
    m_data = std::move(m_asyncState->data);
    m_asyncState.reset();
    m_loading = false;
    if (!m_data.nameUpdate.success) {
      m_enteringName = true;
      if (auto *input =
              ctx.world.Get<components::UIText>(m_nameInputText)) {
        input->visible = true;
      }
      if (auto *submit =
              ctx.world.Get<components::UIButton>(m_nameSubmitButton)) {
        submit->visible = true;
      }
      if (auto *status = ctx.world.Get<components::UIText>(m_statusText)) {
        status->text = L"名前を登録できませんでした: " +
                       core::ToWString(m_data.nameUpdate.errorMessage);
      }
    } else {
      if (m_wasRegisteringName) {
        m_profile.displayName = m_pendingDisplayName;
        if (ctx.achievementEvents) {
          game::systems::AchievementEvent nameRegistered;
          nameRegistered.type =
              game::systems::AchievementEventType::DisplayNameRegistered;
          ctx.achievementEvents->Publish(nameRegistered);
        }
      }
    }
    m_wasRegisteringName = false;

    if (ctx.achievementEvents && !m_profile.displayName.empty()) {
      const auto isSelfTop = [&](const game::systems::PlayFabLeaderboardResult
                                     &result) {
        return result.success && !result.entries.empty() &&
               result.entries.front().displayName == m_profile.displayName;
      };
      if (isSelfTop(m_data.strokes) || isSelfTop(m_data.clearTime)) {
        game::systems::AchievementEvent topRank;
        topRank.type = game::systems::AchievementEventType::DailyRankingFetched;
        topRank.isTopRank = true;
        ctx.achievementEvents->Publish(topRank);
      }
    }

    RefreshUI(ctx);
  }

  ctx.world.Query<components::UIButton>().Each(
      [&](ecs::Entity entity, components::UIButton &button) {
        if (!OwnsEntity(entity) || !button.visible ||
            button.state != components::ButtonState::Pressed ||
            !ctx.input.GetMouseButtonDown(0)) {
          return;
        }
        if (button.action == "ranking_close") {
          if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.5f);
          ctx.sceneManager->PopScene();
        } else if (button.action == "ranking_strokes" && !m_enteringName) {
          m_view = View::Strokes;
          RefreshUI(ctx);
        } else if (button.action == "ranking_time" && !m_enteringName) {
          m_view = View::ClearTime;
          RefreshUI(ctx);
        } else if (button.action == "ranking_name_submit" &&
                   game::systems::IsValidPlayFabDisplayName(m_nameInput)) {
          m_enteringName = false;
          ctx.world.Get<components::UIText>(m_nameInputText)->visible = false;
          ctx.world.Get<components::UIButton>(m_nameSubmitButton)->visible = false;
          StartLoad(core::ToString(m_nameInput));
          RefreshUI(ctx);
        } else if (button.action == "ranking_name_submit") {
          if (auto *status = ctx.world.Get<components::UIText>(m_statusText)) {
            status->text = L"名前は3～25文字で入力してください";
          }
        }
      });
}

void RankingScene::Render(core::GameContext &ctx) {
  RenderModalScene(ctx, *this, {0.18f, 0.18f, 0.18f, 0.62f});
}

void RankingScene::OnExit(core::GameContext &ctx) {
  m_asyncState.reset();
  DestroyAllEntities(ctx);
}

} // namespace game::scenes

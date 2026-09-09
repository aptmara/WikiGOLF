#include "src/core/GameContext.h"
#include "src/core/Input.h"
#include "src/ecs/World.h"
#include "src/game/components/UIImage.h"
#include "src/game/components/UIText.h"
#include "src/game/controllers/WikiGolfHUD.h"
#include "src/game/utils/UIConstants.h"
#include "src/graphics/GraphicsDevice.h"
#include "src/resources/ResourceManager.h"
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK_TRUE(condition, message)                                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                             \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (0)

namespace resources {

// HUDはResourceManagerを参照しないため、内部プールだけを初期化する。
ResourceManager::ResourceManager(graphics::GraphicsDevice &device)
    : m_device(device), m_meshPool(graphics::Mesh{}),
      m_shaderPool(graphics::Shader{}), m_audioPool(audio::AudioClip{}) {}

} // namespace resources

namespace {

template <typename Component>
std::size_t CountComponents(ecs::World &world) {
  std::size_t count = 0;
  world.Query<Component>().Each(
      [&](ecs::Entity, Component &) { ++count; });
  return count;
}

bool ContainsText(ecs::World &world, const std::wstring &expected) {
  bool found = false;
  world.Query<game::components::UIText>().Each(
      [&](ecs::Entity, game::components::UIText &text) {
        if (text.text == expected) {
          found = true;
        }
      });
  return found;
}

game::components::UIText *FindText(ecs::World &world,
                                   const std::wstring &expected) {
  game::components::UIText *found = nullptr;
  world.Query<game::components::UIText>().Each(
      [&](ecs::Entity, game::components::UIText &text) {
        if (text.text == expected) {
          found = &text;
        }
      });
  return found;
}

game::components::UIImage *FindImage(ecs::World &world,
                                     const std::string &texturePath) {
  game::components::UIImage *found = nullptr;
  world.Query<game::components::UIImage>().Each(
      [&](ecs::Entity, game::components::UIImage &image) {
        if (image.texturePath == texturePath) {
          found = &image;
        }
      });
  return found;
}

game::components::UIBarGauge *FindGauge(ecs::World &world) {
  game::components::UIBarGauge *found = nullptr;
  world.Query<game::components::UIBarGauge>().Each(
      [&](ecs::Entity, game::components::UIBarGauge &gauge) {
        found = &gauge;
      });
  return found;
}

bool SameColor(const DirectX::XMFLOAT4 &left,
               const DirectX::XMFLOAT4 &right) {
  return left.x == right.x && left.y == right.y && left.z == right.z &&
         left.w == right.w;
}

bool AreAllHudElementsHidden(ecs::World &world) {
  bool allHidden = true;
  world.Query<game::components::UIText>().Each(
      [&](ecs::Entity, game::components::UIText &text) {
        if (text.visible) {
          allHidden = false;
        }
      });
  world.Query<game::components::UIImage>().Each(
      [&](ecs::Entity, game::components::UIImage &image) {
        if (image.visible) {
          allHidden = false;
        }
      });
  world.Query<game::components::UIBarGauge>().Each(
      [&](ecs::Entity, game::components::UIBarGauge &gauge) {
        if (gauge.isVisible) {
          allHidden = false;
        }
      });
  return allHidden;
}

} // namespace

int main() {
  graphics::GraphicsDevice graphics;
  resources::ResourceManager resources(graphics);
  ecs::World world;
  core::Input input;
  core::GameContext context(resources, world, graphics, input);
  game::controllers::WikiGolfHUD hud;

  hud.Initialize(context);

  const std::size_t initialEntityCount = world.GetEntityCount();
  const std::size_t initialImageCount =
      CountComponents<game::components::UIImage>(world);
  std::cout << "[INFO] HUD entity count: " << initialEntityCount << "\n";
  CHECK_TRUE(initialEntityCount > 0,
             "HUD初期化時に表示用Entityを生成する");
  CHECK_TRUE(CountComponents<game::components::UIText>(world) > 0,
             "HUDがテキストComponentを生成する");
  CHECK_TRUE(CountComponents<game::components::UIBarGauge>(world) == 1,
             "HUDがショットゲージを1個生成する");
  CHECK_TRUE(ContainsText(world, L"CURRENT"),
             "現在記事ラベルを生成する");
  CHECK_TRUE(ContainsText(world, L"TARGET"),
             "目的記事ラベルを生成する");
  CHECK_TRUE(ContainsText(world, L"SHOT   SPACE / CLICK"),
             "ショット操作ボタンを生成する");
  CHECK_TRUE(ContainsText(world, L"Q / E  CLUB     RMB  CAMERA     M  MAP"),
             "通常操作のヒントを生成する");
  CHECK_TRUE(ContainsText(world, L"N\n▲"),
             "ミニマップの北方向表示を生成する");
  CHECK_TRUE(ContainsText(world, L"150m") && ContainsText(world, L" 50m") &&
                 ContainsText(world, L"  0m"),
             "ミニマップの縮尺表示を生成する");
  CHECK_TRUE(ContainsText(world, L"● BALL") &&
                 ContainsText(world, L"⛳ TARGET"),
             "ミニマップの現在地と目的地の凡例を生成する");

  game::components::GolfGameState state;
  state.currentPage = "現在の記事";
  state.targetPage = "目的の記事";
  state.shotCount = 4;
  state.par = 6;
  state.moveCount = 2;
  const std::vector<game::controllers::ClubUIData> clubs;
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, 0.5f, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubs, -1, 0.0f, 0.0f,
             nullptr);

  CHECK_TRUE(ContainsText(world, L"現在の記事"),
             "更新時に現在記事名を反映する");
  CHECK_TRUE(ContainsText(world, L"目的の記事"),
             "更新時に目的記事名を反映する");
  CHECK_TRUE(ContainsText(world, L"打数 4　目標まで目安6リンク　移動 2"),
             "更新時に打数、目安リンク数、移動数を反映する");
  CHECK_TRUE(ContainsText(world, L"3.0"),
             "更新時に風速を小数1桁で反映する");
  CHECK_TRUE(ContainsText(world, L"→ m/s"),
             "更新時にカメラ相対の風向きを反映する");
  CHECK_TRUE(world.GetEntityCount() == initialEntityCount + 4,
             "初回更新時にライ表示用Entityを4個追加する");
  CHECK_TRUE(ContainsText(world, L"BALL LIE") &&
                 ContainsText(world, L"フェアウェイ") &&
                 ContainsText(world, L"コンディション良好"),
             "フェアウェイのライ情報を表示する");

  state.currentMaterial = game::components::TerrainMaterial::Water;
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, 0.5f, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubs, -1, 0.0f, 0.0f,
             nullptr);
  auto *waterLie = FindText(world, L"ウォーター");
  CHECK_TRUE(waterLie && ContainsText(world, L"OUT OF BOUNDS"),
             "ウォーターのOB情報を表示する");
  CHECK_TRUE(SameColor(waterLie->style.color, game::ui::kColorError),
             "ウォーターのライをエラー色で表示する");

  const std::vector<game::controllers::ClubUIData> clubsWithIcon = {
      {"Club 0", "textures/ui/club0.png", "C0", "TYPE", 1.0f, 100.0f},
      {"Club 1", "textures/ui/club1.png", "C1", "TYPE", 1.0f, 110.0f},
      {"Club 2", "textures/ui/club2.png", "C2", "TYPE", 1.0f, 120.0f},
      {"Club 3", "textures/ui/club3.png", "C3", "TYPE", 1.0f, 130.0f},
      {"Club 4", "textures/ui/club4.png", "C4", "TYPE", 1.0f, 140.0f}};
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, 0.5f, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2, 0.0f,
             0.0f, nullptr);
  CHECK_TRUE(CountComponents<game::components::UIImage>(world) ==
                 initialImageCount + 5,
             "クラブごとに画像Componentを1個生成する");
  CHECK_TRUE(FindText(world, L"Club 0") && !FindText(world, L"Club 0")->visible,
             "選択範囲外の前方クラブを隠す");
  CHECK_TRUE(FindText(world, L"Club 1") && FindText(world, L"Club 1")->visible,
             "選択クラブの1つ前を表示する");
  CHECK_TRUE(FindText(world, L"Club 2") && FindText(world, L"Club 2")->visible,
             "選択中クラブを表示する");
  CHECK_TRUE(FindText(world, L"Club 3") && FindText(world, L"Club 3")->visible,
             "選択クラブの1つ後を表示する");
  CHECK_TRUE(FindText(world, L"Club 4") && !FindText(world, L"Club 4")->visible,
             "選択範囲外の後方クラブを隠す");
  CHECK_TRUE(FindImage(world, "textures/ui/club2.png") &&
                 FindImage(world, "textures/ui/club2.png")->alpha == 1.0f,
             "選択中クラブの画像を不透明にする");
  CHECK_TRUE(FindImage(world, "textures/ui/club1.png") &&
                 FindImage(world, "textures/ui/club1.png")->alpha == 0.5f,
             "非選択クラブの画像を半透明にする");

  hud.UpdateLandingPreviewButton(context, false, true, true);
  auto *landingPreviewText = FindText(world, L"⛳ 着弾予測");
  CHECK_TRUE(landingPreviewText &&
                 SameColor(landingPreviewText->style.color,
                           game::ui::kColorWhite),
             "着弾予測が有効なとき文字色を白にする");

  hud.SetShotPhaseUIVisible(context, true);
  CHECK_TRUE(!FindText(world, L"Club 2")->visible,
             "ショット中はクラブ一覧を隠す");
  CHECK_TRUE(!landingPreviewText->visible,
             "ショット中は着弾予測ボタンを隠す");
  CHECK_TRUE(!FindText(world, L"SHOT   SPACE / CLICK")->visible &&
                 !FindText(world,
                           L"Q / E  CLUB     RMB  CAMERA     M  MAP")
                      ->visible,
             "ショット中は開始ボタンと通常操作ヒントを隠す");
  hud.SetShotPhaseUIVisible(context, false);
  CHECK_TRUE(FindText(world, L"Club 1")->visible &&
                 FindText(world, L"Club 2")->visible &&
                 FindText(world, L"Club 3")->visible,
             "ショット終了時に3行のクラブ選択範囲を復元する");
  CHECK_TRUE(FindText(world, L"SHOT   SPACE / CLICK")->visible &&
                 FindText(world,
                          L"Q / E  CLUB     RMB  CAMERA     M  MAP")
                     ->visible,
             "ショット終了時に開始ボタンと通常操作ヒントを復元する");

  hud.SetShotPhaseUIVisible(context, true);
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::PowerCharging, 0.5f, 0.5f,
             0.0f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  auto *gauge = FindGauge(world);
  CHECK_TRUE(gauge && gauge->isVisible &&
                 gauge->mode == game::components::UIBarGaugeMode::Power,
             "パワー調整中はパワーゲージを表示する");
  CHECK_TRUE(ContainsText(world, L"01 / 02") &&
                 ContainsText(world, L"パワー調整") &&
                 ContainsText(world, L"60y"),
             "パワー調整中の手順と基準飛距離を表示する");

  hud.UpdatePowerGauge(context, 50.0f, 30.0f, 0.0f, 100.0f);
  CHECK_TRUE(gauge->value == 0.5f && gauge->markerValue == 0.5f,
             "パワーゲージの値とマーカーを正規化する");

  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::ImpactTiming, 0.4f, 0.5f,
             0.5f, 0.4f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  hud.UpdatePowerGauge(context, 50.0f, 40.0f, 0.0f, 100.0f);
  CHECK_TRUE(gauge->mode == game::components::UIBarGaugeMode::Impact &&
                 gauge->showImpactZones && gauge->markerValue == 0.4f,
             "インパクト調整中は中央合わせゲージと判定帯を表示する");
  CHECK_TRUE(ContainsText(world, L"02 / 02") &&
                 ContainsText(world, L"インパクトタイミング") &&
                 ContainsText(world, L"60y 確定") &&
                 ContainsText(world, L"左 20%"),
             "インパクト調整中の確定距離と左右誤差を表示する");

  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Executing, 0.4f, 0.5f,
             0.5f, 0.4f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  CHECK_TRUE(gauge->isVisible && gauge->showConfirmedMarker,
             "インパクト確定直後は確定マーカーを保持表示する");
  hud.Update(context,
             game::ui::kGaugeHoldDuration + game::ui::kGaugeFadeDuration +
                 0.1f,
             state, game::components::ShotState::Phase::Executing, 0.4f,
             0.5f, 0.5f, 0.4f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  CHECK_TRUE(!gauge->isVisible && gauge->opacity == 1.0f,
             "保持時間とフェード時間の経過後にゲージを隠す");

  hud.UpdateJudge(context, L"GREAT", game::ui::kColorSuccess);
  CHECK_TRUE(ContainsText(world, L"GREAT"),
             "ショット判定文字を反映する");
  hud.ResetShotUI(context);
  CHECK_TRUE(!gauge->isVisible && gauge->value == 0.0f &&
                 gauge->mode == game::components::UIBarGaugeMode::Power &&
                 !gauge->showImpactZones && !gauge->showConfirmedMarker,
             "ショットUIのリセットでゲージを初期状態へ戻す");

  hud.SetVisible(context, false);
  CHECK_TRUE(AreAllHudElementsHidden(world),
             "HUD非表示時にすべてのUI Componentを非表示にする");

  hud.Shutdown(context);
  CHECK_TRUE(world.GetEntityCount() == 0,
             "HUD終了時に生成したEntityをすべて破棄する");

  std::cout << "All WikiGolf HUD lifecycle tests passed!\n";
  return 0;
}

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
      std::cerr << "[FAIL] " << message << std::endl;                          \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << std::endl;                           \
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
  CHECK_TRUE(ContainsText(world, L"現在"),
             "現在記事ラベルを生成する");
  CHECK_TRUE(ContainsText(world, L"ゴール"),
             "目的記事ラベルを生成する");
  CHECK_TRUE(!ContainsText(world, L"クリックでショット"),
             "通常HUDに独立したショットボタンを生成しない");
  CHECK_TRUE(ContainsText(world, L"中クリック  照準ピン設置"),
             "照準ピン操作を左下に生成する");
  CHECK_TRUE(FindImage(world, "Assets/ui/keyboard_q.png") &&
                 FindImage(world, "Assets/ui/keyboard_e.png"),
             "クラブ切替用のQ/Eキー画像を生成する");
  CHECK_TRUE(ContainsText(world, L"N\n▲"),
             "ミニマップの北方向表示を生成する");
  CHECK_TRUE(!ContainsText(world, L"150m") &&
                 !ContainsText(world, L"● BALL"),
             "大型ミニマップに旧縮尺と凡例を重ねない");

  game::components::GolfGameState state;
  state.currentPage = "現在の記事";
  state.targetPage = "目的の記事";
  state.shotCount = 4;
  state.par = 6;
  state.moveCount = 2;
  const std::vector<game::controllers::ClubUIData> clubs;
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubs, -1, 0.0f, 0.0f,
             nullptr);

  CHECK_TRUE(ContainsText(world, L"現在の記事"),
             "更新時に現在記事名を反映する");
  CHECK_TRUE(ContainsText(world, L"目的の記事"),
             "更新時に目的記事名を反映する");
  CHECK_TRUE(!ContainsText(world, L"打数 4　目標まで目安6リンク　移動 2"),
             "通常HUDに旧スコア行を表示しない");
  CHECK_TRUE(ContainsText(world, L"3.0"),
             "更新時に風速を小数1桁で反映する");
  CHECK_TRUE(ContainsText(world, L"→ m/s"),
             "更新時にカメラ相対の風向きを反映する");
  CHECK_TRUE(world.GetEntityCount() == initialEntityCount,
             "初回更新時に旧ライパネルを追加しない");
  CHECK_TRUE(FindImage(world, "Assets/all_terrain_ui_assets/fairway.png"),
             "風表示の座布団へフェアウェイ画像を反映する");

  state.currentMaterial = game::components::TerrainMaterial::Water;
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubs, -1, 0.0f, 0.0f,
             nullptr);
  CHECK_TRUE(FindImage(world, "Assets/all_terrain_ui_assets/water.png"),
             "OB地形では風表示の座布団画像を切り替える");

  const std::vector<game::controllers::ClubUIData> clubsWithIcon = {
      {"Club 0", "Assets/textures/Clubs/1W_driver.png", "DR", "Driver", 1.0f, 100.0f},
      {"Club 1", "Assets/textures/Clubs/3W_fairway_wood.png", "FW", "Wood", 1.0f, 110.0f},
      {"Club 2", "Assets/textures/Clubs/7I_iron.png", "IR", "Iron", 1.0f, 120.0f},
      {"Club 3", "Assets/textures/Clubs/SW_sand_wedge.png", "SW", "Wedge", 1.0f, 130.0f},
      {"Club 4", "Assets/textures/Clubs/PT_putter.png", "PT", "Putter", 1.0f, 140.0f}};
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2, 0.0f,
             0.0f, nullptr);
  CHECK_TRUE(CountComponents<game::components::UIImage>(world) ==
                 initialImageCount + 1,
             "選択中クラブ用の画像Componentだけを生成する");
  CHECK_TRUE(!FindText(world, L"Club 1") && FindText(world, L"Club 2") &&
                 !FindText(world, L"Club 3"),
             "選択中クラブ1件だけを表示用Entityとして生成する");
  CHECK_TRUE(FindText(world, L"Club 2")->style.fontFamily == "Kiwi Maru Medium" &&
                 FindText(world, L"中クリック  照準ピン設置")
                         ->style.fontFamily == "Kiwi Maru Medium" &&
                 FindText(world, L"現在")->style.fontFamily ==
                     "Kiwi Maru Medium",
             "通常HUDで用途別の同梱フォントを使用する");
  CHECK_TRUE(FindImage(world, "Assets/textures/Clubs/7I_iron.png"),
             "選択中クラブ固有の画像を使用する");

  const std::size_t singleClubEntityCount = world.GetEntityCount();
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 3, 0.0f,
             0.0f, nullptr);
  CHECK_TRUE(!FindText(world, L"Club 2") && FindText(world, L"Club 3") &&
                 FindImage(world, "Assets/textures/Clubs/SW_sand_wedge.png"),
             "切替時に単一クラブEntityを固有画像で作り直す");
  CHECK_TRUE(world.GetEntityCount() == singleClubEntityCount,
             "クラブ切替を繰り返してもHUD Entity数を増やさない");

  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Idle, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2, 0.0f,
             0.0f, nullptr);

  hud.SetShotPhaseUIVisible(context, true);
  CHECK_TRUE(!FindText(world, L"Club 2")->visible,
             "ショット中は単一クラブ表示を隠す");
  CHECK_TRUE(!FindText(world, L"中クリック  照準ピン設置")->visible,
             "ショット中は照準ピン操作説明を隠す");
  auto *surfaceText = FindText(world, L"ウォーター");
  CHECK_TRUE(!FindText(world, L"現在")->visible &&
                 surfaceText && !surfaceText->visible &&
                 !FindText(world, L"N\n▲")->visible,
             "ショット中はゲージ以外の通常HUDをすべて隠す");

  hud.Update(context, 0.1f, state,
             game::components::ShotState::Phase::ShowResult, false, 0.0f,
             0.0f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  CHECK_TRUE(FindText(world, L"Club 2")->visible &&
                 FindText(world, L"Club 2")->opacity > 0.0f &&
                 FindText(world, L"Club 2")->opacity < 1.0f,
             "打球後は通常HUDを途中透明度でフェード表示する");
  hud.Update(context, game::ui::kNormalHudFadeDuration, state,
             game::components::ShotState::Phase::ShowResult, false, 0.0f,
             0.0f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  CHECK_TRUE(FindText(world, L"Club 2")->opacity == 1.0f &&
                 FindText(world, L"現在")->opacity == 1.0f,
             "フェード時間経過後に通常HUDを完全表示する");

  hud.SetShotPhaseUIVisible(context, true);
  game::components::AimPinState aimPin;
  aimPin.active = true;
  aimPin.requiredPowerRatio = 0.72f;
  aimPin.reachable = true;
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::PowerCharging, false, 0.5f,
             0.0f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, &aimPin);
  auto *gauge = FindGauge(world);
  CHECK_TRUE(gauge && gauge->isVisible &&
                 gauge->mode == game::components::UIBarGaugeMode::Power,
             "パワー調整中はパワーゲージを表示する");
  CHECK_TRUE(gauge->showAimPinMarker && gauge->aimPinValue == 0.72f &&
                 SameColor(gauge->aimPinMarkerColor,
                           game::ui::kColorSpecial),
             "パワーゲージへ到達可能な照準ピン位置を黄色で表示する");
  CHECK_TRUE(gauge->chipYards == 60 && gauge->chipAnchor == 0.5f &&
                 gauge->rulerMaxYards == 120.0f && gauge->tabLabel == L"Club 2",
             "パワー調整中はクラブ基準の定規と距離札を設定する");
  CHECK_TRUE(!ContainsText(world, L"01 / 02") &&
                 !ContainsText(world, L"パワー調整"),
             "手順番号と見出しのパネルを表示しない");

  hud.UpdatePowerGauge(context, 50.0f, 30.0f, 0.0f, 100.0f);
  CHECK_TRUE(gauge->value == 0.5f && gauge->markerValue == 0.5f,
             "パワーゲージの値とマーカーを正規化する");

  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::ImpactTiming, false, 0.5f,
             0.5f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, &aimPin);
  hud.UpdatePowerGauge(context, 50.0f, 40.0f, 0.0f, 100.0f);
  CHECK_TRUE(gauge->mode == game::components::UIBarGaugeMode::Impact &&
                 gauge->showImpactZones && gauge->markerValue == 0.4f &&
                 gauge->value == 0.5f,
             "インパクト調整中は確定した距離を残して判定帯を表示する");
  CHECK_TRUE(gauge->impactCenter == 0.5f && gauge->chipAnchor == 0.5f &&
                 gauge->chipYards == 60,
             "判定帯と距離札を確定した距離の先端に合わせる");
  CHECK_TRUE(gauge->showAimPinMarker,
             "インパクト調整中も定規へ照準ピンを立てる");

  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::ImpactTiming, false, 0.5f,
             0.5f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  CHECK_TRUE(!gauge->showAimPinMarker,
             "照準ピン未設置なら定規へピンを立てない");

  aimPin.requiredPowerRatio = 1.2f;
  aimPin.reachable = false;
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::PowerCharging, false, 0.5f,
             0.0f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, &aimPin);
  CHECK_TRUE(gauge->showAimPinMarker && gauge->aimPinValue == 1.0f &&
                 SameColor(gauge->aimPinMarkerColor, game::ui::kColorError),
             "到達不能な照準ピン位置は定規の右端へ収めて赤色で表示する");

  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::ImpactTiming, false, 0.5f,
             0.5f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, &aimPin);
  CHECK_TRUE(!gauge->resultActive, "クリック前は結果演出を始めない");

  // インパクト確定: 発射前のスイング中から演出を始める。
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::ImpactTiming, true, 0.5f,
             0.5f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, &aimPin);
  CHECK_TRUE(gauge->resultActive && hud.IsShotGaugeAnimating() &&
                 gauge->judgement == game::components::ShotJudgement::Special,
             "インパクト確定の瞬間に判定付きの結果演出を始める");
  CHECK_TRUE(gauge->chipYards == 66 && !gauge->showAimPinMarker,
             "距離札を判定込みの最終距離へ更新し、ピンを消す");

  // 発射後にShotStateが初期値へ戻っても、確定時の結果を表示し続ける。
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Executing, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2, 0.0f,
             0.0f, nullptr);
  CHECK_TRUE(gauge->isVisible && gauge->chipYards == 66 && gauge->value == 0.5f &&
                 gauge->markerValue == 0.5f,
             "発射後も確定時の距離と針の位置を保持する");
  hud.Update(context, game::ui::kGaugeHoldDuration + 0.1f, state,
             game::components::ShotState::Phase::Executing, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2, 0.0f,
             0.0f, nullptr);
  CHECK_TRUE(gauge->isVisible && gauge->opacity > 0.0f && gauge->opacity < 1.0f,
             "保持時間の後はフェードアウトする");
  hud.Update(context, game::ui::kGaugeFadeDuration, state,
             game::components::ShotState::Phase::Executing, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2, 0.0f,
             0.0f, nullptr);
  CHECK_TRUE(!gauge->isVisible && !hud.IsShotGaugeAnimating(),
             "保持時間とフェード時間の経過後にゲージを隠す");

  // 演出中にHUDを隠した場合（カップインの演出など）は再表示しない。
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::ImpactTiming, false, 0.5f,
             0.5f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::ImpactTiming, true, 0.5f,
             0.5f, 0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2,
             0.0f, 0.0f, nullptr);
  hud.SetVisible(context, false);
  hud.Update(context, 0.016f, state,
             game::components::ShotState::Phase::Executing, false, 0.0f, 0.0f,
             0.5f, 0.5f, 3.0f, {1.0f, 0.0f}, 0.0f, clubsWithIcon, 2, 0.0f,
             0.0f, nullptr);
  CHECK_TRUE(!gauge->isVisible, "HUD非表示後は結果演出を再表示しない");
  hud.SetVisible(context, true);

  hud.UpdateJudge(context, L"GREAT", game::ui::kColorSuccess);
  CHECK_TRUE(ContainsText(world, L"GREAT"),
             "ショット判定文字を反映する");
  hud.ResetShotUI(context);
  CHECK_TRUE(!gauge->isVisible && gauge->value == 0.0f &&
                 gauge->mode == game::components::UIBarGaugeMode::Power &&
                 !gauge->showImpactZones && !gauge->resultActive &&
                 !gauge->showAimPinMarker && gauge->aimPinValue == 0.0f,
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

/**
 * @file MinimapControllerUI.cpp
 * @brief MinimapControllerUI の実装
*/

#include "MinimapController.h"
#include "MinimapControllerInternals.h"
#include "../components/Transform.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/Camera.h"
#include "../components/Skybox.h"
#include "../components/WikiComponents.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../ecs/World.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <format>

using namespace DirectX;
using namespace game::components;


namespace game::controllers {

using namespace DirectX;
using namespace game::components;

/**
 * @brief ミニマップのUI表示用エンティティを生成します。
*/
void MinimapController::InitializeUI(core::GameContext &ctx) {
  if (!m_minimapRenderer) return;

  m_minimapEntity = m_entityOwner.Create(ctx.world);
  auto &ui = ctx.world.Add<UIImage>(m_minimapEntity);
  ui.textureSRV = m_minimapRenderer->GetSRV();
  ui.width = game::ui::kMinimapWidth;
  ui.height = game::ui::kMinimapHeight;
  ui.x = game::ui::kMinimapX;
  ui.y = game::ui::kMinimapY;
  ui.alpha = 0.82f;
  ui.visible = true;
  ui.layer = game::ui::kLayerMinimap;

  // 自ボール内側ドットマーカー (●)
  // 注意: UIText は width=0 のとき描画矩形が画面右端まで自動拡張され、
  // TextAlign::Center はその巨大な矩形の中央に文字を置いてしまう
  // （x,y を中心に置きたい単発グリフでは致命的にズレる）。width/height を
  // フォントサイズに明示することで、x,y を中心とした正しい配置にする。
  m_minimapMarkerEntity = m_entityOwner.Create(ctx.world);
  auto &marker = ctx.world.Add<UIText>(m_minimapMarkerEntity);
  marker.text = L"●";
  marker.x = ui.x + ui.width * 0.5f - 10.0f;
  marker.y = ui.y + ui.height * 0.5f - 10.0f;
  marker.width = game::ui::kMinimapMarkerSize;
  marker.height = game::ui::kMinimapMarkerSize;
  marker.style = graphics::TextStyle::Guide();
  marker.style.fontSize = game::ui::kMinimapMarkerSize;
  marker.style.color = {0.18f, 0.85f, 1.0f, 1.0f}; // 蛍光シアン
  marker.layer = game::ui::kLayerMarker + 1;
  marker.visible = false;

  m_minimapBallIconEntity = m_entityOwner.Create(ctx.world);
  auto& ballIcon = ctx.world.Add<UIImage>(m_minimapBallIconEntity);
  ballIcon = UIImage::Create("golf_ball_icon_transparent.png", marker.x, marker.y);
  ballIcon.width = 24.0f;
  ballIcon.height = 24.0f;
  ballIcon.layer = game::ui::kLayerMarker + 2;
  ballIcon.visible = true;

  // 自ボール外側パルスサークル (○)
  // fontSizeが毎フレーム変化するため、width/heightもMinimapControllerMap.cpp側で
  // 同じ値に追従させる（幅0のまま任せると上のコメントの不具合が再発する）。
  m_minimapPulseMarkerEntity = m_entityOwner.Create(ctx.world);
  auto &pulseMarker = ctx.world.Add<UIText>(m_minimapPulseMarkerEntity);
  pulseMarker.text = L"○";
  pulseMarker.x = marker.x;
  pulseMarker.y = marker.y;
  pulseMarker.width = game::ui::kMinimapMarkerSize;
  pulseMarker.height = game::ui::kMinimapMarkerSize;
  pulseMarker.style = graphics::TextStyle::Guide();
  pulseMarker.style.fontSize = game::ui::kMinimapMarkerSize;
  pulseMarker.style.color = {0.18f, 0.85f, 1.0f, 0.8f}; // 半透明シアン
  pulseMarker.layer = game::ui::kLayerMarker;
  pulseMarker.visible = false;

  // エイムピン(中クリックで設置した狙い所)用マーカー。
  // ターゲットホールの赤"P"と混同しないよう、色とグリフを変えている。
  m_aimPinMarkerEntity = m_entityOwner.Create(ctx.world);
  auto &aimPinMarker = ctx.world.Add<UIText>(m_aimPinMarkerEntity);
  aimPinMarker.text = L"◆";
  aimPinMarker.x = 0.0f;
  aimPinMarker.y = 0.0f;
  aimPinMarker.width = game::ui::kMinimapMarkerSize;
  aimPinMarker.height = game::ui::kMinimapMarkerSize;
  aimPinMarker.style = graphics::TextStyle::Guide();
  aimPinMarker.style.fontSize = game::ui::kMinimapMarkerSize;
  aimPinMarker.style.color = {1.0f, 0.78f, 0.05f, 1.0f};
  aimPinMarker.layer = game::ui::kLayerMarker + 3;
  aimPinMarker.visible = false;

  // 実軌道を右下マップへ投影する最前面ドット列。
  m_minimapGuideDotEntities.clear();
  for (int i = 0; i < 30; ++i) {
    auto dotEntity = m_entityOwner.Create(ctx.world);
    auto &dot = ctx.world.Add<UIText>(dotEntity);
    dot.text = L"●";
    dot.x = 0.0f;
    dot.y = 0.0f;
    dot.style = graphics::TextStyle::Guide();
    dot.width = 8.0f;
    dot.height = 8.0f;
    dot.style.fontSize = 8.0f;
    dot.style.color = {0.75f, 0.95f, 1.0f, 0.95f};
    dot.layer = game::ui::kLayerMarker + 8;
    dot.visible = false;
    m_minimapGuideDotEntities.push_back(dotEntity);
  }

  m_holeHoverLabelEntity = m_entityOwner.Create(ctx.world);
  auto &holeHover = ctx.world.Add<UIText>(m_holeHoverLabelEntity);
  holeHover.text = L"";
  holeHover.width = 160.0f;
  holeHover.height = 28.0f;
  holeHover.style = graphics::TextStyle::Guide();
  holeHover.style.fontFamily = "Kiwi Maru Medium";
  holeHover.style.fontSize = 13.0f;
  holeHover.style.align = graphics::TextAlign::Center;
  holeHover.style.bgColor = {0.03f, 0.07f, 0.04f, 0.92f};
  holeHover.style.borderColor = {1.0f, 1.0f, 1.0f, 0.9f};
  holeHover.style.borderWidth = 1.0f;
  holeHover.style.cornerRadius = 5.0f;
  holeHover.layer = game::ui::kLayerMinimapHover;
  holeHover.visible = false;

  static constexpr const wchar_t *kFilterLabels[kFlagFilterCount] = {
      L"赤 目的地", L"黄 1 hop", L"橙 2 hops",
      L"白 3-5", L"灰 6+", L"青 未解析"};
  constexpr float toggleWidth = 142.0f;
  constexpr float toggleHeight = 22.0f;
  constexpr float rowGap = 4.0f;
  m_flagFilterDropdownWidth = 112.0f;
  m_flagFilterDropdownHeight = 24.0f;
  m_flagFilterDropdownX = ui.x + ui.width - m_flagFilterDropdownWidth - 8.0f;
  m_flagFilterDropdownY = ui.y + 8.0f;
  m_flagFilterDropdownOpen = false;
  m_flagFilterDropdownEntity = m_entityOwner.Create(ctx.world);
  auto &dropdown = ctx.world.Add<UIText>(m_flagFilterDropdownEntity);
  dropdown.x = m_flagFilterDropdownX;
  dropdown.y = m_flagFilterDropdownY;
  dropdown.width = m_flagFilterDropdownWidth;
  dropdown.height = m_flagFilterDropdownHeight;
  dropdown.style = graphics::TextStyle::Guide();
  dropdown.style.fontFamily = "Kiwi Maru Medium";
  dropdown.style.fontSize = 12.0f;
  dropdown.style.align = graphics::TextAlign::Center;
  dropdown.style.hasOutline = false;
  dropdown.style.hasShadow = false;
  dropdown.layer = game::ui::kLayerMinimapHover;

  const float toggleStartX = ui.x + ui.width - toggleWidth - 8.0f;
  const float toggleStartY =
      m_flagFilterDropdownY + m_flagFilterDropdownHeight + rowGap;
  m_flagFilterToggles.clear();
  for (size_t i = 0; i < kFlagFilterCount; ++i) {
    const float toggleX = toggleStartX;
    const float toggleY =
        toggleStartY + static_cast<float>(i) * (toggleHeight + rowGap);
    const ecs::Entity toggleEntity = m_entityOwner.Create(ctx.world);
    auto &toggle = ctx.world.Add<UIText>(toggleEntity);
    toggle.x = toggleX;
    toggle.y = toggleY;
    toggle.width = toggleWidth;
    toggle.height = toggleHeight;
    toggle.style = graphics::TextStyle::Guide();
    toggle.style.fontFamily = "Kiwi Maru Medium";
    toggle.style.fontSize = 11.0f;
    toggle.style.align = graphics::TextAlign::Left;
    toggle.style.hasOutline = false;
    toggle.style.hasShadow = false;
    toggle.layer = game::ui::kLayerMinimapHover;
    m_flagFilterToggles.push_back(
        {toggleEntity, toggleX, toggleY, toggleWidth, toggleHeight, i,
         kFilterLabels[i]});
  }
  UpdateFlagFilterToggles(ctx);

  // 着弾点プレビュー: ばらつき範囲円(大きな薄い○) + 中心マーカー
  // (width/height未設定だとCenter揃えが画面右端方向にズレるため明示する。
  //  ばらつき円はfontSizeが毎フレーム変わるのでMinimapControllerMap.cpp側で追従させる)
  m_landingPreviewRangeEntity = m_entityOwner.Create(ctx.world);
  auto &landingRange = ctx.world.Add<UIText>(m_landingPreviewRangeEntity);
  landingRange.text = L"○";
  landingRange.width = 40.0f;
  landingRange.height = 40.0f;
  landingRange.style = graphics::TextStyle::Guide();
  landingRange.style.color = {1.0f, 0.75f, 0.15f, 0.55f}; // 半透明アンバー
  landingRange.layer = game::ui::kLayerMarker;
  landingRange.visible = false;

  m_landingPreviewCenterEntity = m_entityOwner.Create(ctx.world);
  auto &landingCenter = ctx.world.Add<UIText>(m_landingPreviewCenterEntity);
  landingCenter.text = L"⛳";
  landingCenter.width = 22.0f;
  landingCenter.height = 22.0f;
  landingCenter.style = graphics::TextStyle::Guide();
  landingCenter.style.fontSize = 22.0f;
  landingCenter.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  landingCenter.layer = game::ui::kLayerMarker + 1;
  landingCenter.visible = false;

  // ズームインジケーター背景の生成（マップビューでのズーム率表示用）
  m_mapZoomIndicatorBg = m_entityOwner.Create(ctx.world);
  auto &zoomBg = ctx.world.Add<UIText>(m_mapZoomIndicatorBg);
  zoomBg.x = game::ui::kMinimapX + 10.0f;
  zoomBg.y = game::ui::kMinimapY + 10.0f;
  zoomBg.width = 65.0f;
  zoomBg.height = 24.0f;
  zoomBg.style.bgColor = game::ui::kColorBgDark;
  zoomBg.style.borderColor = game::ui::kColorBorder;
  zoomBg.style.borderWidth = 1.0f;
  zoomBg.style.cornerRadius = 4.0f;
  zoomBg.visible = false;
  zoomBg.layer = game::ui::kLayerMinimap + 2;

  // ズームインジケーターテキストの生成（現在のズーム倍率をテキスト表示）
  m_mapZoomIndicatorText = m_entityOwner.Create(ctx.world);
  auto &zoomTxt = ctx.world.Add<UIText>(m_mapZoomIndicatorText);
  zoomTxt.text = L"x1.00";
  zoomTxt.x = zoomBg.x + 5.0f;
  zoomTxt.y = zoomBg.y + 2.0f;
  zoomTxt.width = zoomBg.width - 10.0f;
  zoomTxt.height = zoomBg.height - 4.0f;
  zoomTxt.style = graphics::TextStyle::Guide();
  zoomTxt.style.fontSize = 14.0f;
  zoomTxt.style.align = graphics::TextAlign::Center;
  zoomTxt.style.color = game::ui::kColorTextPrimary; // 紙面調のズーム表示パネル上に表示するため本文色にする
  // 紙面パネル上では Guide() の黒縁取り+影が小さい文字を滲ませて見せてしまうため外す。
  zoomTxt.style.hasOutline = false;
  zoomTxt.style.hasShadow = false;
  zoomTxt.visible = false;
  zoomTxt.layer = game::ui::kLayerMinimap + 3;

  // マウス位置のワールド座標表示テキストの生成
  m_mapCoordText = m_entityOwner.Create(ctx.world);
  auto &coordTxt = ctx.world.Add<UIText>(m_mapCoordText);
  coordTxt.text = L"";
  coordTxt.style = graphics::TextStyle::Guide();
  coordTxt.style.fontSize = 12.0f;
  coordTxt.style.align = graphics::TextAlign::Left;
  coordTxt.visible = false;
  coordTxt.layer = game::ui::kLayerMinimap + 2;

  // マウス位置とボールの距離表示テキストの生成
  m_mapDistanceText = m_entityOwner.Create(ctx.world);
  auto &distTxt = ctx.world.Add<UIText>(m_mapDistanceText);
  distTxt.text = L"";
  distTxt.style = graphics::TextStyle::Guide();
  distTxt.style.fontSize = 12.0f;
  distTxt.style.align = graphics::TextAlign::Left;
  distTxt.visible = false;
  distTxt.layer = game::ui::kLayerMinimap + 2;

  // 操作ヘルプパネル背景の生成
  m_mapHelpPanelBg = m_entityOwner.Create(ctx.world);
  auto &helpBg = ctx.world.Add<UIText>(m_mapHelpPanelBg);
  helpBg.x = (1280.0f - game::ui::kMapHelpPanelW) * 0.5f;
  helpBg.y = (720.0f - game::ui::kMapHelpPanelH) * 0.5f;
  helpBg.width = game::ui::kMapHelpPanelW;
  helpBg.height = game::ui::kMapHelpPanelH;
  helpBg.style.bgColor = game::ui::kColorBgDark;
  helpBg.style.borderColor = game::ui::kColorBorder;
  helpBg.style.borderWidth = 2.0f;
  helpBg.style.cornerRadius = 12.0f;
  helpBg.visible = false;
  helpBg.layer = game::ui::kLayerOverlay;

  // 操作ヘルプタイトルテキストの生成
  m_mapHelpTitle = m_entityOwner.Create(ctx.world);
  auto &helpTitle = ctx.world.Add<UIText>(m_mapHelpTitle);
  helpTitle.text = L"操作ガイド - マップビュー";
  helpTitle.x = helpBg.x;
  helpTitle.y = helpBg.y + 15.0f;
  helpTitle.width = helpBg.width;
  helpTitle.height = 30.0f;
  helpTitle.style = graphics::TextStyle::Guide();
  helpTitle.style.fontSize = 20.0f;
  helpTitle.style.color = game::ui::kColorAccent;
  helpTitle.style.align = graphics::TextAlign::Center;
  // 紙面パネル上では Guide() の黒縁取り+影が文字を滲ませて見せてしまうため外す。
  helpTitle.style.hasOutline = false;
  helpTitle.style.hasShadow = false;
  helpTitle.visible = false;
  helpTitle.layer = game::ui::kLayerOverlay + 1;

  // 操作ヘルプの各操作説明テキスト行を生成
  std::vector<std::wstring> helpTexts = {
    L"[左 / 右ドラッグ] マップをパン",
    L"[スクロール / + / -] ズームイン / ズームアウト",
    L"[C / Space] ボール位置にフォーカス",
    L"[F] フィールド全体を表示",
    L"[0] ズーム率を等倍(100%)にリセット",
    L"[Q / E] クラブを切り替え",
    L"[中クリック] 照準ピンを設置",
    L"[?] 操作ガイドの表示切り替え",
    L"[M / Esc] マップビューを閉じる"
  };

  float startY = helpBg.y + 55.0f;
  float lineSpacing = 22.0f;
  for (size_t i = 0; i < helpTexts.size(); ++i) {
    auto e = m_entityOwner.Create(ctx.world);
    auto &line = ctx.world.Add<UIText>(e);
    line.text = helpTexts[i];
    line.x = helpBg.x + 25.0f;
    line.y = startY + i * lineSpacing;
    line.width = helpBg.width - 50.0f;
    line.height = 20.0f;
    line.style = graphics::TextStyle::Guide();
    line.style.fontSize = 14.0f;
    line.style.align = graphics::TextAlign::Left;
    line.style.color = game::ui::kColorTextPrimary; // 紙面調のヘルプパネル上に表示するため本文色にする
    // 紙面パネル上では Guide() の黒縁取り+影が小さい文字を滲ませて見せてしまうため外す。
    line.style.hasOutline = false;
    line.style.hasShadow = false;
    line.visible = false;
    line.layer = game::ui::kLayerOverlay + 1;
    m_mapHelpLines.push_back(e);
  }

  // マップビュー突入時の簡易操作ヒント下部バー背景の生成
  m_mapOpenHintBg = m_entityOwner.Create(ctx.world);
  auto &openBg = ctx.world.Add<UIText>(m_mapOpenHintBg);
  openBg.x = 120.0f;
  openBg.y = 665.0f;
  openBg.width = 1040.0f;
  openBg.height = 36.0f;
  openBg.style.bgColor = game::ui::kColorBgDark;
  openBg.style.borderColor = game::ui::kColorBorder;
  openBg.style.borderWidth = 1.0f;
  openBg.style.cornerRadius = 6.0f;
  openBg.visible = false;
  openBg.layer = game::ui::kLayerOverlay;

  // マップビュー突入時の簡易操作ヒントテキストの生成
  m_mapOpenHintText = m_entityOwner.Create(ctx.world);
  auto &openTxt = ctx.world.Add<UIText>(m_mapOpenHintText);
  openTxt.text = L"[左/右ドラッグ] パン  [スクロール/+/-] ズーム  [Q/E] クラブ  [中クリック] 照準ピン  [C/Space] ボール中央  [F] 全体表示  [?] ヘルプ  [Esc/M] 閉じる";
  openTxt.x = openBg.x + 10.0f;
  openTxt.y = openBg.y + 6.0f;
  openTxt.width = openBg.width - 20.0f;
  openTxt.height = openBg.height - 12.0f;
  openTxt.style = graphics::TextStyle::Guide();
  openTxt.style.fontSize = 12.0f;
  openTxt.style.align = graphics::TextAlign::Center;
  openTxt.style.color = game::ui::kColorTextPrimary; // 紙面調のヒントバー上に表示するため本文色にする
  // 紙面パネル上では Guide() の黒縁取り+影が小さい文字を滲ませて見せてしまうため外す。
  openTxt.style.hasOutline = false;
  openTxt.style.hasShadow = false;
  openTxt.visible = false;
  openTxt.layer = game::ui::kLayerOverlay + 1;
}

void MinimapController::UpdateFlagFilterToggles(core::GameContext &ctx) {
  m_flagFilterAvailable.fill(false);
  for (const auto &icon : m_mapHoleIcons) {
    const size_t index = static_cast<size_t>(minimap_detail::ClassifyFlag(
        icon.isTarget, icon.hopsToTarget));
    if (index < m_flagFilterAvailable.size()) {
      m_flagFilterAvailable[index] = true;
    }
  }

  const bool menuVisible = m_isVisible && !m_isMapView;
  if (auto *dropdown = ctx.world.Get<UIText>(m_flagFilterDropdownEntity)) {
    dropdown->text = m_flagFilterDropdownOpen ? L"旗表示 ▲" : L"旗表示 ▼";
    dropdown->visible = menuVisible;
    dropdown->style.bgColor = game::ui::kColorBgDark;
    dropdown->style.borderColor = m_flagFilterDropdownOpen
                                      ? game::ui::kColorAccent
                                      : game::ui::kColorBorder;
    dropdown->style.borderWidth = 1.0f;
    dropdown->style.cornerRadius = game::ui::kRadiusChip;
    dropdown->style.color = game::ui::kColorTextPrimary;
  }

  for (const auto &filter : m_flagFilterToggles) {
    auto *toggle = ctx.world.Get<UIText>(filter.entity);
    if (!toggle) {
      continue;
    }
    const bool available = m_flagFilterAvailable[filter.filterIndex];
    const bool enabled = m_flagFilterEnabled[filter.filterIndex];
    toggle->text = (enabled && available ? L"☑ " : L"☐ ") + filter.label;
    toggle->visible = menuVisible && m_flagFilterDropdownOpen;
    toggle->style.bgColor = available
                                ? DirectX::XMFLOAT4{0.973f, 0.976f, 0.980f,
                                                   0.92f}
                                : DirectX::XMFLOAT4{0.82f, 0.83f, 0.84f,
                                                   0.76f};
    toggle->style.borderColor = enabled && available
                                    ? game::ui::kColorAccent
                                    : game::ui::kColorBorder;
    toggle->style.borderWidth = 1.0f;
    toggle->style.cornerRadius = game::ui::kRadiusChip;
    if (!available) {
      toggle->style.color = {0.48f, 0.49f, 0.50f, 0.72f};
    } else if (enabled) {
      toggle->style.color = game::ui::kColorTextPrimary;
    } else {
      toggle->style.color = game::ui::kColorTextSub;
    }
  }
}

/**
 * @brief ミニマップ上のすべてのホールアイコンを削除します。
*/
void MinimapController::ClearHoleIcons(core::GameContext &ctx) {
  for (auto &icon : m_mapHoleIcons) {
    ctx.world.DestroyEntity(icon.iconEntity);
  }
  m_mapHoleIcons.clear();
  UpdateFlagFilterToggles(ctx);
}

void MinimapController::SetTutorialHelpMode(core::GameContext& ctx,
                                            bool enabled) {
  static const std::vector<std::wstring> tutorialHelp = {
      L"[M] マップビューを開く",
      L"[左ドラッグ] マップをパン",
      L"[スクロール] ズームイン / ズームアウト",
      L"[中クリック] 照準ピンを設置",
      L"[?] 操作ガイドの表示切り替え",
      L"[M / Esc] マップビューを閉じる",
  };
  static const std::vector<std::wstring> normalHelp = {
      L"[左 / 右ドラッグ] マップをパン",
      L"[スクロール / + / -] ズームイン / ズームアウト",
      L"[C / Space] ボール位置にフォーカス",
      L"[F] フィールド全体を表示",
      L"[0] ズーム率を等倍(100%)にリセット",
      L"[Q / E] クラブを切り替え",
      L"[中クリック] 照準ピンを設置",
      L"[?] 操作ガイドの表示切り替え",
      L"[M / Esc] マップビューを閉じる",
  };
  const auto& texts = enabled ? tutorialHelp : normalHelp;
  for (size_t i = 0; i < m_mapHelpLines.size(); ++i) {
    if (auto* line = ctx.world.Get<UIText>(m_mapHelpLines[i])) {
      line->text = i < texts.size() ? texts[i] : L"";
    }
  }
  if (auto* hint = ctx.world.Get<UIText>(m_mapOpenHintText)) {
    hint->text = enabled
        ? L"[左ドラッグ] パン  [スクロール] ズーム  [中クリック] 照準ピン  [?] ヘルプ  [Esc/M] 閉じる"
        : L"[左/右ドラッグ] パン  [スクロール/+/-] ズーム  [Q/E] クラブ  [中クリック] 照準ピン  [C/Space] ボール中央  [F] 全体表示  [?] ヘルプ  [Esc/M] 閉じる";
  }
}

/**
 * @brief ミニマップ上にホールアイコンを追加します。
*/
void MinimapController::AddHoleIcon(core::GameContext &ctx, float x, float z,
                                    const std::string& linkTarget,
                                    bool isTargetHole, bool isPlayableHole,
                                    int hopsToTarget) {
  auto iconEntity = m_entityOwner.Create(ctx.world);
  auto &ui = ctx.world.Add<UIImage>(iconEntity);

  ui = UIImage::Create("golf_hole_icon_transparent.png", 0.0f, 0.0f);
  ui.grayscaleTint = true;
  if (isTargetHole) {
    ui.width = 34.0f;
  } else if (isPlayableHole) {
    ui.width = 20.0f;
  } else {
    ui.width = 12.0f;
  }
  ui.height = ui.width;
  if (isTargetHole) {
    ui.alpha = 1.0f;
    ui.layer = game::ui::kLayerMarker + 3;
  } else if (isPlayableHole) {
    ui.alpha = 0.72f;
    ui.layer = game::ui::kLayerMarker + 1;
  } else {
    ui.alpha = 0.34f;
    ui.layer = game::ui::kLayerMarker;
  }
  ui.visible = false;

  MapHoleIcon mapIcon{};
  mapIcon.iconEntity = iconEntity;
  mapIcon.worldPos = {x, z};
  mapIcon.linkTarget = linkTarget;
  mapIcon.isTarget = isTargetHole;
  mapIcon.isPlayable = isPlayableHole;
  mapIcon.hopsToTarget = hopsToTarget;
  m_mapHoleIcons.push_back(mapIcon);
  UpdateFlagFilterToggles(ctx);
}

/**
 * @brief 経路評価後のホールアイコン情報を更新します。
*/
void MinimapController::UpdateHoleIconEvaluation(
    const std::string& linkTarget, bool isPlayableHole, int hopsToTarget) {
  for (auto& icon : m_mapHoleIcons) {
    if (icon.linkTarget != linkTarget) {
      continue;
    }
    icon.isPlayable = isPlayableHole;
    icon.hopsToTarget = hopsToTarget;
  }
}

} // namespace game::controllers


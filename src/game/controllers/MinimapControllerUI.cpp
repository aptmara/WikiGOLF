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

  // ターゲットピン用フォールバックマーカー（fontSizeが変化するのでこちらも追従させる）
  m_minimapFlagMarkerEntity = m_entityOwner.Create(ctx.world);
  auto &flagMarker = ctx.world.Add<UIText>(m_minimapFlagMarkerEntity);
  flagMarker.text = L"P";
  flagMarker.x = 0.0f;
  flagMarker.y = 0.0f;
  flagMarker.width = game::ui::kMinimapMarkerSize;
  flagMarker.height = game::ui::kMinimapMarkerSize;
  flagMarker.style = graphics::TextStyle::Guide();
  flagMarker.style.fontSize = game::ui::kMinimapMarkerSize;
  flagMarker.style.color = {1.0f, 0.2f, 0.2f, 1.0f}; // 鮮烈なレッド
  flagMarker.layer = game::ui::kLayerMarker + 2; // ボールより前面に描画
  flagMarker.visible = false;

  // エイムピン(中クリックで設置した狙い所)用マーカー。
  // ターゲットホールの赤"P"と混同しないよう、色とグリフを変えている。
  m_aimPinMarkerEntity = m_entityOwner.Create(ctx.world);
  auto &aimPinMarker = ctx.world.Add<UIText>(m_aimPinMarkerEntity);
  aimPinMarker.text = L"📍";
  aimPinMarker.x = 0.0f;
  aimPinMarker.y = 0.0f;
  aimPinMarker.width = game::ui::kMinimapMarkerSize;
  aimPinMarker.height = game::ui::kMinimapMarkerSize;
  aimPinMarker.style = graphics::TextStyle::Guide();
  aimPinMarker.style.fontSize = game::ui::kMinimapMarkerSize;
  aimPinMarker.style.color = {1.0f, 0.2f, 0.9f, 1.0f}; // マゼンタ
  aimPinMarker.layer = game::ui::kLayerMarker + 3;
  aimPinMarker.visible = false;

  // ショット方向案内用のガイドドット配列 (·)
  m_minimapGuideDotEntities.clear();
  for (int i = 0; i < 3; ++i) {
    auto dotEntity = m_entityOwner.Create(ctx.world);
    auto &dot = ctx.world.Add<UIText>(dotEntity);
    dot.text = L"·";
    dot.x = 0.0f;
    dot.y = 0.0f;
    dot.style = graphics::TextStyle::Guide();
    dot.style.fontSize = 12.0f;
    dot.style.color = {0.18f, 0.85f, 1.0f, 0.65f}; // 薄めのシアン
    dot.layer = game::ui::kLayerMarker;
    dot.visible = false;
    m_minimapGuideDotEntities.push_back(dotEntity);
  }

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
    L"[ドラッグ / 左クリック] マップをパン",
    L"[スクロール / + / -] ズームイン / ズームアウト",
    L"[C / Space] ボール位置にフォーカス",
    L"[F] フィールド全体を表示",
    L"[0] ズーム率を等倍(100%)にリセット",
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
  openBg.x = 240.0f;
  openBg.y = 665.0f;
  openBg.width = 800.0f;
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
  openTxt.text = L"[ドラッグ] パン  [スクロール/+/-] ズーム  [C/Space] ボール中央  [F] 全体表示  [?] ヘルプ  [Esc/M] 閉じる";
  openTxt.x = openBg.x + 10.0f;
  openTxt.y = openBg.y + 6.0f;
  openTxt.width = openBg.width - 20.0f;
  openTxt.height = openBg.height - 12.0f;
  openTxt.style = graphics::TextStyle::Guide();
  openTxt.style.fontSize = 13.0f;
  openTxt.style.align = graphics::TextAlign::Center;
  openTxt.style.color = game::ui::kColorTextPrimary; // 紙面調のヒントバー上に表示するため本文色にする
  // 紙面パネル上では Guide() の黒縁取り+影が小さい文字を滲ませて見せてしまうため外す。
  openTxt.style.hasOutline = false;
  openTxt.style.hasShadow = false;
  openTxt.visible = false;
  openTxt.layer = game::ui::kLayerOverlay + 1;
}

/**
 * @brief ミニマップ上のすべてのホールアイコンを削除します。
*/
void MinimapController::ClearHoleIcons(core::GameContext &ctx) {
  for (auto &icon : m_mapHoleIcons) {
    ctx.world.DestroyEntity(icon.iconEntity);
  }
  m_mapHoleIcons.clear();
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



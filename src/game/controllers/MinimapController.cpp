/**
 * @file MinimapController.cpp
 * @brief MinimapController の実装
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

void MinimapController::Initialize(Config cfg, core::GameContext &ctx) {
  m_cfg = cfg;
  m_minimapRenderer = std::make_unique<game::systems::MapSys>();
  m_minimapRenderer->Initialize(ctx.graphics.GetDevice(), 256, 256);

  // 変化駆動レンダリングの状態をリセット（初回は必ず1回描画させる）
  m_minimapHasRenderedOnce = false;
  m_lastRenderedCenter = {0.0f, 0.0f, 0.0f};
  m_lastRenderedZoom = -1.0f;
  m_lastRenderedFieldWidth = -1.0f;
  m_lastRenderedFieldDepth = -1.0f;
  m_lastRenderedSpan = -1.0f;
  m_lastRenderedMoveCount = -1;
  m_pendingMoveCount = -1;
}

void MinimapController::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_minimapGuideDotEntities.clear();
  m_mapHelpLines.clear();
  m_mapHoleIcons.clear();
  m_minimapRenderer.reset();
  m_minimapRenderPending = false;

  m_minimapEntity = UINT32_MAX;
  m_minimapMarkerEntity = UINT32_MAX;
  m_minimapBallIconEntity = UINT32_MAX;
  m_minimapPulseMarkerEntity = UINT32_MAX;
  m_minimapFlagMarkerEntity = UINT32_MAX;
  m_minimapHelpEntity = UINT32_MAX;
  m_landingPreviewRangeEntity = UINT32_MAX;
  m_landingPreviewCenterEntity = UINT32_MAX;
  m_mapZoomIndicatorBg = UINT32_MAX;
  m_mapZoomIndicatorText = UINT32_MAX;
  m_mapCoordText = UINT32_MAX;
  m_mapDistanceText = UINT32_MAX;
  m_mapHelpPanelBg = UINT32_MAX;
  m_mapHelpTitle = UINT32_MAX;
  m_mapOpenHintBg = UINT32_MAX;
  m_mapOpenHintText = UINT32_MAX;
}

/**
 * @brief ミニマップUI全体の表示状態を切り替えます。
 *
 * 入力: 表示フラグ（visible）
 * 変更: UIImageやUITextの表示フラグ
 * 出力: なし（副作用としてコンポーネントの表示状態が変化）
*/
void MinimapController::SetVisible(core::GameContext& ctx, bool visible) {
    if (!visible && m_isVisible) {
      // 非表示化：再表示時に必ず1回再描画させるため状態を無効化する
      m_minimapHasRenderedOnce = false;
      m_minimapRenderPending = false;
      m_lastRenderedMoveCount = -1;
    }
    m_isVisible = visible;
    auto setUIImg = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* img = ctx.world.Get<components::UIImage>(e)) img->visible = v;
    };
    auto setUITxt = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* t = ctx.world.Get<components::UIText>(e)) t->visible = v;
    };

    // ミニマップ本体とマーカー類
    setUIImg(m_minimapEntity,            visible);
    setUITxt(m_minimapMarkerEntity,      false);
    setUIImg(m_minimapBallIconEntity,    visible);
    setUITxt(m_minimapPulseMarkerEntity, visible);
    setUITxt(m_minimapFlagMarkerEntity,  false);
    for (auto dotEntity : m_minimapGuideDotEntities) {
      setUITxt(dotEntity, false);
    }
    setUITxt(m_minimapHelpEntity,        visible);

    // ズームインジケーター
    setUITxt(m_mapZoomIndicatorBg,   false); // マップビュー時のみ表示のため常にfalse
    setUITxt(m_mapZoomIndicatorText, false);

    // 座標・距離テキスト（マップビュー時のみ）
    setUITxt(m_mapCoordText,    false);
    setUITxt(m_mapDistanceText, false);

    // ヘルプパネル
    setUITxt(m_mapHelpPanelBg, false);
    setUITxt(m_mapHelpTitle,   false);
    for (auto e : m_mapHelpLines) setUITxt(e, false);

    // 簡易操作ヒント
    setUITxt(m_mapOpenHintBg,   false);
    setUITxt(m_mapOpenHintText, false);

    // ホールアイコン
    for (auto& icon : m_mapHoleIcons) {
        setUIImg(icon.iconEntity, false);
    }
}

} // namespace game::controllers

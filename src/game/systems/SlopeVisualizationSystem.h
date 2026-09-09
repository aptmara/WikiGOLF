#pragma once
/**
 * @file SlopeVisualizationSystem.h
 * @brief パター保持時にグリーンの高低差（傾斜）を可視化するオーバーレイを
 *        管理します。
*/

#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include "../../resources/ResourceManager.h"
#include "SlopeVisualizationMeshBuilder.h"
#include <DirectXMath.h>

namespace core {
struct GameContext;
}

namespace game::systems {

class WikiTerrainSystem;

/**
 * @brief 傾斜可視化オーバーレイのライフサイクルを管理するシステム。
 * @details メッシュ生成そのものはSlopeVisualizationMeshBuilderへ委譲し、
 *          本クラスはEntity生成/破棄と表示条件・再構築タイミングの管理のみを担う。
*/
class SlopeVisualizationSystem {
public:
  SlopeVisualizationSystem() = default;
  ~SlopeVisualizationSystem() = default;

  /** @brief シェーダーをロードします。*/
  void Initialize(core::GameContext &ctx);

  /** @brief 生成したEntityを破棄します。*/
  void Shutdown(core::GameContext &ctx);

  /**
   * @brief 毎フレーム更新します。
   * @param visible 表示すべきか（パター保持中かつ待機中など、呼び出し側で判定済み）
   * @param center 表示範囲の中心（通常はボール位置）
   * @param terrain 高さサンプリングに使う地形システム（nullptrなら非表示扱い）
  */
  void Update(core::GameContext &ctx, bool visible,
             const DirectX::XMFLOAT3 &center,
             const WikiTerrainSystem *terrain);

private:
  void Show(core::GameContext &ctx);
  void Hide(core::GameContext &ctx);
  void RebuildMesh(core::GameContext &ctx, const DirectX::XMFLOAT3 &center,
                   const WikiTerrainSystem &terrain);

  /** @brief 前回構築時の中心からこの距離以上動いたら再構築する(m)*/
  static constexpr float kRebuildDistance = 0.75f;

  ecs::Entity m_overlayEntity = UINT32_MAX;
  ecs::EntityOwner m_entityOwner;
  resources::ShaderHandle m_shader;
  bool m_visible = false;
  bool m_hasMesh = false;
  DirectX::XMFLOAT3 m_lastBuildCenter = {1.0e9f, 0.0f, 1.0e9f};
  SlopeOverlayConfig m_config;
};

} // namespace game::systems

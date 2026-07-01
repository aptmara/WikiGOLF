#pragma once
/**
 * @file ResultScene.h
 * @brief ゲームクリア時のリザルトシーン
 */

#include "../../core/Scene.h"
#include "../../graphics/WikiTextureGenerator.h"
#include <DirectXMath.h>
#include <string>
#include <vector>


namespace game::scenes {
using namespace DirectX;

/**
 * @brief リザルト画面に渡すデータ
 */
struct ResultData {
  std::string targetPage;
  int shotCount;
  int par;
  std::vector<std::string> pathHistory;
  bool isNewRecord;
};

/**
 * @brief リザルトシーンクラス
 */
class ResultScene : public core::Scene {
public:
  ResultScene(const ResultData &data);
  ~ResultScene() override;

  const char *GetName() const override { return "ResultScene"; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void Render(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;

private:
  ResultData m_data;

  // UIエンティティ
  ecs::Entity m_bgEntity = UINT32_MAX;
  ecs::Entity m_frameEntity = UINT32_MAX;
  ecs::Entity m_titleEntity = UINT32_MAX;
  ecs::Entity m_badgeEntity = UINT32_MAX;
  ecs::Entity m_subtitleEntity = UINT32_MAX;
  ecs::Entity m_textEntity = UINT32_MAX;
  ecs::Entity m_hintEntity = UINT32_MAX;
  ecs::Entity m_retryBtnEntity = UINT32_MAX;
  ecs::Entity m_titleBtnEntity = UINT32_MAX;

  // 内部ヘルパー関数
  void SetupUI(core::GameContext &ctx);
  // 3Dビジュアル関連
  ecs::Entity m_globeEntity =
      0; ///< 自転する地球儀エンティティ
  ecs::Entity m_floorEntity = 0;  ///< 反射する床面エンティティ
  ecs::Entity m_cameraEntity = 0; ///< 周回するカメラエンティティ

  // 装飾リング
  struct RingObject {
    ecs::Entity entity;
    float baseRadius;
    float rotationSpeed;
    float phase;
  };
  std::vector<RingObject> m_rings;

  // 花火用の構造体
  struct HanabiSpark {
    ecs::Entity entity;
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 vel;
    DirectX::XMFLOAT4 color;
    float age;
    float lifeTime;
    float size;
    float drag;
  };

  struct HanabiShell {
    enum class Phase { Ascending, FlashFrame, Burst, Fading };
    Phase phase = Phase::Ascending;
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 vel;
    float age = 0.0f;
    float lifeTime = 0.0f;
    float flashRadius = 0.0f;
  };

  std::vector<HanabiShell> m_shells;
  std::vector<HanabiSpark> m_sparks;
  
  float m_cameraShake = 0.0f;
  float m_volleyTimer = 1.0f;
  float m_volleyInterval = 4.0f;
  int m_shellsPerVolley = 3;

  void LaunchVolley();

  // 豪華なUI要素
  struct UIElement {
    ecs::Entity entity;
    float baseX, baseY;
    float currentScale;
    float targetScale;
    bool isHovered;
    std::wstring text; // 識別用テキスト
    DirectX::XMFLOAT4 baseColor;
  };
  std::vector<UIElement> m_uiElements;

  // アニメーション状態
  float m_time = 0.0f;
  float m_scoreDisplayValue = 0.0f; // カウントアップ用アニメーション変数
  bool m_isScoreCountFinished = false;

  // UI生成ヘルパー関数
  void CreateLuxuryUI(core::GameContext &ctx);
  void CreateVisualEnvironment(core::GameContext &ctx);
  void UpdateVisuals(core::GameContext &ctx);
};

} // namespace game::scenes

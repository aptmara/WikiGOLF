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

  // UI Entities
  ecs::Entity m_bgEntity = UINT32_MAX;
  ecs::Entity m_frameEntity = UINT32_MAX;
  ecs::Entity m_titleEntity = UINT32_MAX;
  ecs::Entity m_badgeEntity = UINT32_MAX;
  ecs::Entity m_subtitleEntity = UINT32_MAX;
  ecs::Entity m_textEntity = UINT32_MAX;
  ecs::Entity m_hintEntity = UINT32_MAX;
  ecs::Entity m_retryBtnEntity = UINT32_MAX;
  ecs::Entity m_titleBtnEntity = UINT32_MAX;

  // Internal helpers
  void SetupUI(core::GameContext &ctx);
  // --- 3D Visuals ---
  ecs::Entity m_globeEntity =
      0; ///< Rotating Wikipedia Globe (Victory Monument)
  ecs::Entity m_floorEntity = 0;  ///< Reflective Golden Floor
  ecs::Entity m_cameraEntity = 0; ///< Dynamic Orbit Camera

  // --- Rings ---
  struct RingObject {
    ecs::Entity entity;
    float baseRadius;
    float rotationSpeed;
    float phase;
  };
  std::vector<RingObject> m_rings;

  // --- Particles ---
  struct Particle {
    ecs::Entity entity;
    DirectX::XMFLOAT3 velocity;
    float lifeTime;
    float maxLife;
    bool isConfetti; // true: confetti, false: data updraft
  };
  std::vector<Particle> m_particles;
  float m_particleTimer = 0.0f;

  // --- UI Elements (Luxury) ---
  struct UIElement {
    ecs::Entity entity;
    float baseX, baseY;
    float currentScale;
    float targetScale;
    bool isHovered;
    std::wstring text; // For identification
    DirectX::XMFLOAT4 baseColor;
  };
  std::vector<UIElement> m_uiElements;

  // --- Animation State ---
  float m_time = 0.0f;
  float m_scoreDisplayValue = 0.0f; // For count-up animation
  bool m_isScoreCountFinished = false;

  // Helper for UI creation
  void CreateLuxuryUI(core::GameContext &ctx);
  void CreateVisualEnvironment(core::GameContext &ctx);
  void UpdateVisuals(core::GameContext &ctx);
};

} // namespace game::scenes

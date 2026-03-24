#pragma once

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../../graphics/TextStyle.h"
#include "WikiGolfHUD.h" // HUDなどへのアクセス用
#include "../scenes/WikiPageLoader.h"
#include <DirectXMath.h>
#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace game::controllers {

class ArticleTransitionController {
public:
    ArticleTransitionController();
    ~ArticleTransitionController();

    void Initialize(core::GameContext& ctx);
    void Cleanup(core::GameContext& ctx);

    /// @brief トランジションを開始する
    void StartTransition(core::GameContext& ctx, const std::string& targetPage, scenes::WikiPageLoader* pageLoader, ecs::Entity ball, ecs::Entity cam, ecs::Entity sky, game::controllers::MinimapController* minimap);

    /// @brief トランジション中の更新。ロード完了とフェードアウトが終われば true を返す
    bool Update(core::GameContext& ctx);

    /// @brief 現在トランジション中かどうか
    bool IsActive() const { return m_isActive; }

private:
    void SpawnEntities(core::GameContext& ctx);
    void DestroyEntities(core::GameContext& ctx);
    void UpdateAnimation(core::GameContext& ctx, float dt);
    void UpdateUI(core::GameContext& ctx, float dt);
    void ResetLoadState();

    bool m_isActive = false;
    float m_stateTimer = 0.0f;

    // トランジションのフェーズ
    enum class Phase {
        FadeIn,
        Loading,
        Building,
        FadeOut
    };
    Phase m_phase = Phase::FadeIn;
    float m_fadeAlpha = 0.0f;
    const float FADE_SPEED = 2.0f;

    // 非同期ロード関連
    std::future<scenes::PageDataAsyncResult> m_loadTask;
    std::shared_ptr<std::atomic<float>> m_loadProgress;
    std::optional<scenes::PageDataAsyncResult> m_pendingPageData;
    scenes::WikiPageLoader* m_pageLoader = nullptr;
    std::string m_targetPage;
    ecs::Entity m_targetBall;
    ecs::Entity m_targetCam;
    ecs::Entity m_targetSky;
    game::controllers::MinimapController* m_minimap;
    bool m_loadCompleted = false;
    bool m_buildSucceeded = false;
    bool m_buildFailed = false;
    bool m_buildDelayStarted = false;
    float m_buildDelayTimer = 0.0f;
    float m_displayProgress = 0.0f;

    // アニメーション用変数
    float m_globeRotation = 0.0f;
    float m_cartAngle = 0.0f;

    // エンティティ
    ecs::Entity m_globeEntity = UINT32_MAX;
    ecs::Entity m_cartEntity = UINT32_MAX;
    ecs::Entity m_bgEntity = UINT32_MAX;
    ecs::Entity m_cameraEntity = UINT32_MAX; // トランジション専用カメラ
    
    // UI エンティティ
    ecs::Entity m_textEntity = UINT32_MAX;
    ecs::Entity m_progressTextEntity = UINT32_MAX;
    ecs::Entity m_captionTextEntity = UINT32_MAX;
    
    graphics::TextStyle m_primaryStyle{};
    graphics::TextStyle m_progressStyle{};
    graphics::TextStyle m_captionStyle{};

    float m_tipTimer = 0.0f;
    size_t m_tipIndex = 0;
};

} // namespace game::controllers

#pragma once
/**
 * @file ArticleTransitionController.h
 * @brief ArticleTransitionController クラスおよび関連定義
*/

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../../ecs/EntityOwner.h"
#include "../../graphics/TextStyle.h"
#include "WikiGolfHUD.h" // HUDなどへのアクセス用
#include "../scenes/WikiPageLoader.h"
#include <DirectXMath.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace game::controllers {

class ArticleTransitionController {
public:
    ArticleTransitionController();
    ~ArticleTransitionController();

    void Initialize(core::GameContext& ctx);
    void Cleanup(core::GameContext& ctx);

    /** @brief トランジションを開始する*/
    void StartTransition(core::GameContext& ctx, const std::string& targetPage, scenes::WikiPageLoader* pageLoader, ecs::Entity ball, ecs::Entity cam, ecs::Entity sky, game::controllers::MinimapController* minimap);

    /**
     * @brief 遷移演出を始める前から次ページのデータ取得を裏で開始する
     * @details 同じページでStartTransitionされた場合はこの取得結果を引き継ぐ。
     *          地形構築はワールドを書き換えるため、裏で行うのは取得のみ。
    */
    void PrefetchPage(const std::string& targetPage, scenes::WikiPageLoader* pageLoader);

    /** @brief トランジション中の更新。ロード完了とフェードアウトが終われば true を返す*/
    bool Update(core::GameContext& ctx);

    /** @brief 現在トランジション中かどうか*/
    bool IsActive() const { return m_isActive; }

private:
    void SpawnEntities(core::GameContext& ctx);
    void DestroyEntities(core::GameContext& ctx);
    /** @brief 遷移演出前のメインカメラ状態を退避する*/
    void CaptureMainCamera(core::GameContext& ctx);
    /** @brief 遷移演出後に元のメインカメラを復元する*/
    void RestoreMainCamera(core::GameContext& ctx);
    void UpdateAnimation(core::GameContext& ctx, float dt);
    void UpdateUI(core::GameContext& ctx, float dt);
    void BeginCourseIntroduction(core::GameContext& ctx);
    void UpdateCourseIntroduction(core::GameContext& ctx, float dt);
    void ApplyCourseIntroductionShot(core::GameContext& ctx);
    void FinishCourseIntroduction(core::GameContext& ctx);
    void ResetLoadState();

    bool m_isActive = false;
    float m_stateTimer = 0.0f;

    // トランジションのフェーズ
    enum class Phase {
        FadeIn,
        Loading,
        ErrorWait,
        Building,
        CourseIntroduction,
        FadeOut
    };
    Phase m_phase = Phase::FadeIn;
    float m_fadeAlpha = 0.0f;
    const float FADE_SPEED = 2.0f;

    // 非同期ロード関連
    std::future<scenes::PageDataAsyncResult> m_loadTask;
    std::future<scenes::PageDataAsyncResult> m_prefetchTask; ///< PrefetchPageで先行開始した取得
    std::string m_prefetchPage;                              ///< 先行取得中のページ名
    std::shared_ptr<std::atomic<float>> m_loadProgress;
    std::optional<scenes::PageDataAsyncResult> m_pendingPageData;
    scenes::WikiPageLoader* m_pageLoader = nullptr;
    std::string m_targetPage;
    std::string m_previousPage; // 遷移元のページ名
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
    std::chrono::steady_clock::time_point m_transitionStartedAt =
        std::chrono::steady_clock::time_point::min();
    std::chrono::steady_clock::time_point m_fetchStartedAt =
        std::chrono::steady_clock::time_point::min();
    std::chrono::steady_clock::time_point m_buildStartedAt =
        std::chrono::steady_clock::time_point::min();

    // アニメーション用変数
    float m_globeRotation = 0.0f;

    enum class IntroductionShotKind {
        Overview,
        Goal,
        GoalGroup,
        OneHop,
        ReturnToTee
    };

    struct IntroductionShot {
        IntroductionShotKind kind = IntroductionShotKind::Overview;
        DirectX::XMFLOAT3 cameraPosition{};
        DirectX::XMFLOAT3 focusPosition{};
        std::wstring label;
        std::wstring title;
        std::wstring body;
        std::wstring detail;
        float duration = 1.0f;
    };

    std::vector<IntroductionShot> m_introductionShots;
    size_t m_introductionShotIndex = 0;
    float m_introductionShotTimer = 0.0f;
    DirectX::XMFLOAT3 m_introductionCameraFrom{};

    // エンティティ
    ecs::Entity m_globeEntity = UINT32_MAX;
    ecs::Entity m_bgEntity = UINT32_MAX;
    ecs::Entity m_cameraEntity = UINT32_MAX; // トランジション専用カメラ
    ecs::Entity m_previousMainCameraEntity = UINT32_MAX;

    // UI エンティティ
    ecs::Entity m_textEntity = UINT32_MAX;
    ecs::Entity m_progressTextEntity = UINT32_MAX;
    ecs::Entity m_captionTextEntity = UINT32_MAX;
    ecs::Entity m_introductionPanelEntity = UINT32_MAX;
    ecs::Entity m_introductionDetailEntity = UINT32_MAX;
    ecs::Entity m_introductionSkipEntity = UINT32_MAX;
    ecs::EntityOwner m_entityOwner;

    graphics::TextStyle m_primaryStyle{};
    graphics::TextStyle m_progressStyle{};
    graphics::TextStyle m_captionStyle{};

    float m_tipTimer = 0.0f;
    size_t m_tipIndex = 0;

    bool m_hasError = false;
    float m_errorTimer = 0.0f;
    std::wstring m_errorMsg;
};

} // namespace game::controllers

#include "ArticleTransitionController.h"
#include "../../ecs/World.h"
#include "../../core/StringUtils.h"

#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../graphics/TextRenderer.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../systems/WikiClient.h"
#include <algorithm>
#include <chrono>

namespace game::controllers {

ArticleTransitionController::ArticleTransitionController() = default;
ArticleTransitionController::~ArticleTransitionController() = default;

void ArticleTransitionController::Initialize(core::GameContext& ctx) {
    // UIスタイルの構築
    m_primaryStyle = graphics::TextStyle::Title();
    m_primaryStyle.fontSize = 46.0f;
    m_primaryStyle.align = graphics::TextAlign::Center;
    m_primaryStyle.color = {0.96f, 0.98f, 1.0f, 0.0f}; // 最初は透明
    m_primaryStyle.outlineColor = {0.07f, 0.18f, 0.35f, 0.0f};
    m_primaryStyle.outlineWidth = 2.2f;
    m_primaryStyle.hasShadow = true;
    m_primaryStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.0f};
    m_primaryStyle.shadowOffsetX = 2.5f;
    m_primaryStyle.shadowOffsetY = 2.5f;

    m_progressStyle = graphics::TextStyle::ModernBlack();
    m_progressStyle.fontSize = 28.0f;
    m_progressStyle.align = graphics::TextAlign::Center;
    m_progressStyle.color = {0.1f, 0.45f, 0.6f, 0.0f};
    m_progressStyle.hasShadow = true;
    m_progressStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.0f};

    m_captionStyle = graphics::TextStyle::ModernBlack();
    m_captionStyle.fontSize = 20.0f;
    m_captionStyle.align = graphics::TextAlign::Center;
    m_captionStyle.color = {0.2f, 0.2f, 0.25f, 0.0f};
    m_captionStyle.hasShadow = true;
    m_captionStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.0f};
}

void ArticleTransitionController::Cleanup(core::GameContext& ctx) {
    DestroyEntities(ctx);
    m_isActive = false;
}

void ArticleTransitionController::StartTransition(core::GameContext& ctx, const std::string& targetPage, scenes::WikiPageLoader* pageLoader, ecs::Entity ball, ecs::Entity cam, ecs::Entity sky, game::controllers::MinimapController* minimap) {
    m_targetBall = ball;
    m_targetCam = cam;
    m_targetSky = sky;
    m_minimap = minimap;
    m_isActive = true;
    m_phase = Phase::FadeIn;
    m_fadeAlpha = 0.0f;
    m_stateTimer = 0.0f;
    m_targetPage = targetPage;
    m_pageLoader = pageLoader;
    m_loadCompleted = false;
    m_globeRotation = 0.0f;
    m_cartAngle = 0.0f;
    m_tipTimer = 0.0f;
    m_tipIndex = 0;

    SpawnEntities(ctx);

    if (m_pageLoader) {
        // 非同期ロード開始
        auto pageLoaderPtr = m_pageLoader;
        std::string page = targetPage;
        m_loadTask = std::async(std::launch::async, [pageLoaderPtr, page]() {
            return pageLoaderPtr->FetchPageDataAsync(page);
        });
    } else {
        LOG_ERROR("Transition", "WikiPageLoader is null!");
    }
}

void ArticleTransitionController::SpawnEntities(core::GameContext& ctx) {
    auto shaderHandle = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

    // 1. トランジション専用カメラ (遥か上空に配置して既存フィールドと干渉させない)
    m_cameraEntity = ctx.world.CreateEntity();
    auto& camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
    camTr.position = {0.0f, 5000.0f, -30.0f}; 
    camTr.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    auto& cam = ctx.world.Add<components::Camera>(m_cameraEntity);
    cam.fov = DirectX::XM_PIDIV4;
    cam.nearZ = 0.1f;
    cam.farZ = 1000.0f;
    cam.isMainCamera = true; // メインカメラをジャックする

    // 2. 地球儀
    m_globeEntity = ctx.world.CreateEntity();
    auto& globeTr = ctx.world.Add<components::Transform>(m_globeEntity);
    globeTr.position = {0.0f, 5000.0f, 0.0f};
    globeTr.scale = {0.1f, 0.1f, 0.1f}; // STLはサイズが大きい場合が多いので仮スケール
    auto& globeMr = ctx.world.Add<components::MeshRenderer>(m_globeEntity);
    globeMr.mesh = ctx.resource.LoadMesh("Assets/models/Wikipedia_puzzle_globe_3D_render.glb");
    globeMr.shader = shaderHandle;
    globeMr.color = {0.9f, 0.9f, 0.95f, 0.0f}; // 初期アルファ0
    globeMr.isVisible = true;

    // 3. ゴルフカート
    m_cartEntity = ctx.world.CreateEntity();
    auto& cartTr = ctx.world.Add<components::Transform>(m_cartEntity);
    cartTr.position = {10.0f, 5000.0f, 0.0f};
    cartTr.scale = {1.0f, 1.0f, 1.0f}; // GLB仮スケール
    auto& cartMr = ctx.world.Add<components::MeshRenderer>(m_cartEntity);
    cartMr.mesh = ctx.resource.LoadMesh("Assets/models/GolfCart.glb");
    cartMr.shader = shaderHandle;
    cartMr.color = {1.0f, 1.0f, 1.0f, 0.0f};
    cartMr.isVisible = true;

    // 4. 背景 (暗転用)
    m_bgEntity = ctx.world.CreateEntity();
    auto& bgTr = ctx.world.Add<components::Transform>(m_bgEntity);
    bgTr.position = {0.0f, 5000.0f, 50.0f};
    bgTr.scale = {200.0f, 200.0f, 1.0f};
    auto& bgMr = ctx.world.Add<components::MeshRenderer>(m_bgEntity);
    bgMr.mesh = ctx.resource.LoadMesh("builtin/cube");
    bgMr.shader = shaderHandle;
    bgMr.color = {0.05f, 0.05f, 0.1f, 0.0f};
    bgMr.isVisible = true;

    // 5. UI テキスト
    m_textEntity = ctx.world.CreateEntity();
    auto& titleText = ctx.world.Add<components::UIText>(m_textEntity);
    titleText.text = L"Traveling to " + core::ToWString(m_targetPage) + L"...";
    titleText.x = 0.0f;
    titleText.y = 100.0f;
    titleText.width = 1280.0f;
    titleText.style = m_primaryStyle;
    titleText.visible = true;
    titleText.layer = 20;

    m_progressTextEntity = ctx.world.CreateEntity();
    auto& progText = ctx.world.Add<components::UIText>(m_progressTextEntity);
    progText.text = L"0%";
    progText.x = 0.0f;
    progText.y = 160.0f;
    progText.width = 1280.0f;
    progText.style = m_progressStyle;
    progText.visible = true;
    progText.layer = 20;

    m_captionTextEntity = ctx.world.CreateEntity();
    auto& capText = ctx.world.Add<components::UIText>(m_captionTextEntity);
    capText.text = L"Loading Wiki Data...";
    capText.x = 0.0f;
    capText.y = 600.0f;
    capText.width = 1280.0f;
    capText.style = m_captionStyle;
    capText.visible = true;
    capText.layer = 20;
}

void ArticleTransitionController::DestroyEntities(core::GameContext& ctx) {
    if (ctx.world.IsAlive(m_globeEntity)) ctx.world.DestroyEntity(m_globeEntity);
    if (ctx.world.IsAlive(m_cartEntity)) ctx.world.DestroyEntity(m_cartEntity);
    if (ctx.world.IsAlive(m_bgEntity)) ctx.world.DestroyEntity(m_bgEntity);
    if (ctx.world.IsAlive(m_cameraEntity)) ctx.world.DestroyEntity(m_cameraEntity);
    if (ctx.world.IsAlive(m_textEntity)) ctx.world.DestroyEntity(m_textEntity);
    if (ctx.world.IsAlive(m_progressTextEntity)) ctx.world.DestroyEntity(m_progressTextEntity);
    if (ctx.world.IsAlive(m_captionTextEntity)) ctx.world.DestroyEntity(m_captionTextEntity);

    m_globeEntity = m_cartEntity = m_bgEntity = m_cameraEntity = UINT32_MAX;
    m_textEntity = m_progressTextEntity = m_captionTextEntity = UINT32_MAX;
}

bool ArticleTransitionController::Update(core::GameContext& ctx) {
    if (!m_isActive) return true;

    float dt = ctx.dt;
    m_stateTimer += dt;

    // アニメーション更新
    UpdateAnimation(ctx, dt);

    // ロード状況確認とUI更新
    UpdateUI(ctx, dt);

    // フェーズ制御
    switch (m_phase) {
        case Phase::FadeIn:
            m_fadeAlpha += FADE_SPEED * dt;
            if (m_fadeAlpha >= 1.0f) {
                m_fadeAlpha = 1.0f;
                m_phase = Phase::Loading;
            }
            break;

        case Phase::Loading:
            // ロードタスクのチェック
            if (!m_loadCompleted && m_loadTask.valid()) {
                auto status = m_loadTask.wait_for(std::chrono::milliseconds(0));
                if (status == std::future_status::ready) {
                    m_loadCompleted = true;
                    // 同期処理でシーンを構築する
                    auto asyncData = m_loadTask.get();
                    if (m_pageLoader) {
                        LOG_INFO("Transition", "Async fetch complete. Building page sync...");
                        // 注意: ここで球体やカートのエンティティIDが破壊されないよう、
                        // WikiPageLoader 内で明示的に保持すべきエンティティ以外を消す必要がある
                        // (現在は WikiHole や Flag のみ消すロジックなので衝突しないはず)
                        
                        // 元のカメラやボールのIDを取得する処理が必要だが、
                        // 簡単のため、一時的に0（無視）として渡し、後で WikiGolfScene 側でケアするか、
                        // WikiGolfScene が持つボールIDなどを Controller に渡す設計にするか
                        // ※ここでは単純化のため、一旦 UINT32_MAX を渡す
                        m_pageLoader->BuildPageSync(ctx, asyncData, m_targetBall, m_targetCam, m_targetSky, m_minimap);
                    }
                    m_phase = Phase::FadeOut;
                }
            }
            break;

        case Phase::FadeOut:
            m_fadeAlpha -= FADE_SPEED * dt;
            if (m_fadeAlpha <= 0.0f) {
                m_fadeAlpha = 0.0f;
                Cleanup(ctx);
                return true; // トランジション完了
            }
            break;
    }

    // アルファ適用
    if (auto* mr = ctx.world.Get<components::MeshRenderer>(m_globeEntity)) mr->color.w = m_fadeAlpha;
    if (auto* mr = ctx.world.Get<components::MeshRenderer>(m_cartEntity)) mr->color.w = m_fadeAlpha;
    if (auto* mr = ctx.world.Get<components::MeshRenderer>(m_bgEntity)) mr->color.w = m_fadeAlpha;

    return false;
}

void ArticleTransitionController::UpdateAnimation(core::GameContext& ctx, float dt) {
    // 地球儀の自転
    m_globeRotation += dt * 0.5f;
    if (auto* tr = ctx.world.Get<components::Transform>(m_globeEntity)) {
        auto rot = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, m_globeRotation, 0.2f);
        DirectX::XMStoreFloat4(&tr->rotation, rot);
    }

    // カートの公転 (半径15, Y軸回転)
    m_cartAngle += dt * 1.5f;
    float radius = 15.0f;
    if (auto* tr = ctx.world.Get<components::Transform>(m_cartEntity)) {
        tr->position.x = sinf(m_cartAngle) * radius;
        tr->position.z = cosf(m_cartAngle) * radius;
        tr->position.y = 5000.0f + sinf(m_cartAngle * 3.0f) * 1.5f; // 上下にバウンド

        // カートが常に進行方向を向くように
        auto rot = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, m_cartAngle + DirectX::XM_PIDIV2, 0.0f);
        DirectX::XMStoreFloat4(&tr->rotation, rot);
    }
}

void ArticleTransitionController::UpdateUI(core::GameContext& ctx, float dt) {
    float progress = m_loadCompleted ? 1.0f : std::clamp(m_stateTimer / 5.0f, 0.0f, 0.9f);
    int percent = static_cast<int>(progress * 100.0f);

    if (auto* text = ctx.world.Get<components::UIText>(m_progressTextEntity)) {
        text->text = L"Loading... " + std::to_wstring(percent) + L"%";
        auto style = m_progressStyle;
        style.color.w = m_fadeAlpha;
        text->style = style;
    }
    if (auto* text = ctx.world.Get<components::UIText>(m_textEntity)) {
        auto style = m_primaryStyle;
        style.color.w = m_fadeAlpha;
        text->style = style;
    }

    const std::wstring tips[] = {
        L"Cart is traversing the links...",
        L"Checking knowledge base...",
        L"Generating terrain...",
        L"Almost there..."
    };
    m_tipTimer += dt;
    if (m_tipTimer > 2.0f) {
        m_tipTimer = 0.0f;
        m_tipIndex = (m_tipIndex + 1) % 4;
    }
    if (auto* text = ctx.world.Get<components::UIText>(m_captionTextEntity)) {
        text->text = tips[m_tipIndex];
        auto style = m_captionStyle;
        style.color.w = m_fadeAlpha;
        text->style = style;
    }
}

} // namespace game::controllers

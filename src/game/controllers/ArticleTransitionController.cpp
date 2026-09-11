/**
 * @file ArticleTransitionController.cpp
 * @brief ArticleTransitionController の実装
*/

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
#include "hud/HudStyles.h"
#include "../systems/WikiClient.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace game::controllers {

namespace {

constexpr float kIntroductionCameraMoveDuration = 1.5f;
constexpr float kIntroductionSkipX = 990.0f;
constexpr float kIntroductionSkipY = 646.0f;
constexpr float kIntroductionSkipWidth = 240.0f;
constexpr float kIntroductionSkipHeight = 48.0f;

/**
 * @brief 開始時刻からの経過時間をミリ秒で返します。
*/
long long ElapsedMs(const std::chrono::steady_clock::time_point& startedAt) {
    if (startedAt == std::chrono::steady_clock::time_point::min()) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
}

} // namespace

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
    m_captionStyle.fontFamily = "Kiwi Maru Medium"; // Tips は日本語文なので丸ゴシックにする
    m_captionStyle.fontSize = 20.0f;
    m_captionStyle.align = graphics::TextAlign::Center;
    m_captionStyle.color = {0.2f, 0.2f, 0.25f, 0.0f};
    m_captionStyle.hasShadow = true;
    m_captionStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.0f};
}

void ArticleTransitionController::Cleanup(core::GameContext& ctx) {
    LOG_INFO("Transition", "Cleanup started page='{}' total={}ms",
             m_targetPage, ElapsedMs(m_transitionStartedAt));
    DestroyEntities(ctx);
    m_isActive = false;
    LOG_INFO("Transition", "Cleanup finished page='{}' total={}ms",
             m_targetPage, ElapsedMs(m_transitionStartedAt));
}

void ArticleTransitionController::PrefetchPage(const std::string& targetPage, scenes::WikiPageLoader* pageLoader) {
    if (!pageLoader || m_prefetchTask.valid()) {
        return;
    }
    m_prefetchPage = targetPage;
    LOG_INFO("Transition", "Background prefetch started page='{}'", targetPage);
    m_prefetchTask = std::async(std::launch::async, [pageLoader, targetPage]() {
        return pageLoader->FetchPageDataAsync(targetPage);
    });
}

void ArticleTransitionController::StartTransition(core::GameContext& ctx, const std::string& targetPage, scenes::WikiPageLoader* pageLoader, ecs::Entity ball, ecs::Entity cam, ecs::Entity sky, game::controllers::MinimapController* minimap) {
    auto* state = ctx.world.GetGlobal<components::GolfGameState>();
    m_previousPage = state ? state->currentPage : "";
    m_hasError = false;
    m_errorTimer = 0.0f;
    m_errorMsg = L"";

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
    m_tipTimer = 0.0f;
    m_tipIndex = 0;
    m_transitionStartedAt = std::chrono::steady_clock::now();
    m_fetchStartedAt = std::chrono::steady_clock::time_point::min();
    m_buildStartedAt = std::chrono::steady_clock::time_point::min();
    m_displayProgress = 0.0f;

    LOG_INFO("Transition", "StartTransition page='{}'", m_targetPage);

    const auto spawnStartedAt = std::chrono::steady_clock::now();
    SpawnEntities(ctx);
    LOG_INFO("Transition", "Transition entities spawned page='{}' elapsed={}ms",
             m_targetPage, ElapsedMs(spawnStartedAt));

    if (m_pageLoader) {
        m_fetchStartedAt = std::chrono::steady_clock::now();
        if (m_prefetchTask.valid() && m_prefetchPage == targetPage) {
            // カップイン演出中などに裏で開始済みの取得を引き継ぐ
            LOG_INFO("Transition", "Using prefetched page data page='{}'", targetPage);
            m_loadTask = std::move(m_prefetchTask);
        } else {
            // 非同期ロード開始
            auto pageLoaderPtr = m_pageLoader;
            std::string page = targetPage;
            LOG_INFO("Transition", "Async fetch started page='{}'", page);
            m_loadTask = std::async(std::launch::async, [pageLoaderPtr, page]() {
                return pageLoaderPtr->FetchPageDataAsync(page);
            });
        }
        m_prefetchPage.clear();
    } else {
        LOG_ERROR("Transition", "WikiPageLoader is null!");
    }
}

void ArticleTransitionController::SpawnEntities(core::GameContext& ctx) {
    auto shaderHandle = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

    CaptureMainCamera(ctx);

    // トランジション専用カメラ（既存フィールドと干渉しないよう遥か上空に配置）
    m_cameraEntity = m_entityOwner.Create(ctx.world);
    auto& camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
    camTr.position = {0.0f, 5000.0f, -30.0f};
    camTr.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    auto& cam = ctx.world.Add<components::Camera>(m_cameraEntity);
    cam.fov = DirectX::XM_PIDIV4;
    cam.nearZ = 0.1f;
    cam.farZ = 1000.0f;
    cam.isMainCamera = true; // メインカメラをジャックする

    // 地球儀エンティティの生成
    m_globeEntity = m_entityOwner.Create(ctx.world);
    auto& globeTr = ctx.world.Add<components::Transform>(m_globeEntity);
    globeTr.position = {0.0f, 5000.0f, 0.0f};
    globeTr.scale = {2.0f, 2.0f, 2.0f};
    auto& globeMr = ctx.world.Add<components::MeshRenderer>(m_globeEntity);
    globeMr.mesh = ctx.resource.LoadMesh("Assets/models/Wikipedia_puzzle_globe_3D_render.stl");
    globeMr.shader = shaderHandle;
    globeMr.color = {0.9f, 0.9f, 0.95f, 0.0f}; // 初期アルファ0
    globeMr.isTransparent = true;
    globeMr.isVisible = true;

    // 背景エンティティの生成（暗転用）
    m_bgEntity = m_entityOwner.Create(ctx.world);
    auto& bgTr = ctx.world.Add<components::Transform>(m_bgEntity);
    bgTr.position = {0.0f, 5000.0f, 50.0f};
    bgTr.scale = {200.0f, 200.0f, 1.0f};
    auto& bgMr = ctx.world.Add<components::MeshRenderer>(m_bgEntity);
    bgMr.mesh = ctx.resource.LoadMesh("builtin/cube");
    bgMr.shader = shaderHandle;
    bgMr.color = {0.08f, 0.14f, 0.28f, 1.0f};
    bgMr.isTransparent = false;
    bgMr.isVisible = true;

    // UIテキストエンティティの生成
    m_textEntity = m_entityOwner.Create(ctx.world);
    auto& titleText = ctx.world.Add<components::UIText>(m_textEntity);
    titleText.text = L"Traveling to " + core::ToWString(m_targetPage) + L"...";
    titleText.x = 0.0f;
    titleText.y = 100.0f;
    titleText.width = 1280.0f;
    titleText.style = m_primaryStyle;
    titleText.visible = true;
    titleText.layer = 20;

    m_progressTextEntity = m_entityOwner.Create(ctx.world);
    auto& progText = ctx.world.Add<components::UIText>(m_progressTextEntity);
    progText.text = L"0%";
    progText.x = 0.0f;
    progText.y = 160.0f;
    progText.width = 1280.0f;
    progText.style = m_progressStyle;
    progText.visible = true;
    progText.layer = 20;

    m_captionTextEntity = m_entityOwner.Create(ctx.world);
    auto& capText = ctx.world.Add<components::UIText>(m_captionTextEntity);
    capText.text = L"Loading Wiki Data...";
    capText.x = 0.0f;
    capText.y = 600.0f;
    capText.width = 1280.0f;
    capText.style = m_captionStyle;
    capText.visible = true;
    capText.layer = 20;

    m_introductionPanelEntity = m_entityOwner.Create(ctx.world);
    auto& introPanel = ctx.world.Add<components::UIText>(m_introductionPanelEntity);
    introPanel.x = 54.0f;
    introPanel.y = 48.0f;
    introPanel.width = 610.0f;
    introPanel.height = 286.0f;
    game::controllers::hud::ApplySurfaceStyle(introPanel.style);
    introPanel.visible = false;
    introPanel.layer = 30;

    m_introductionDetailEntity = m_entityOwner.Create(ctx.world);
    auto& introDetail = ctx.world.Add<components::UIText>(m_introductionDetailEntity);
    introDetail.x = 82.0f;
    introDetail.y = 285.0f;
    introDetail.width = 554.0f;
    introDetail.height = 28.0f;
    introDetail.visible = false;
    introDetail.layer = 32;

    m_introductionSkipEntity = m_entityOwner.Create(ctx.world);
    auto& introSkip = ctx.world.Add<components::UIText>(m_introductionSkipEntity);
    introSkip.text = L"スキップしてスタート";
    introSkip.x = kIntroductionSkipX;
    introSkip.y = kIntroductionSkipY;
    introSkip.width = kIntroductionSkipWidth;
    introSkip.height = kIntroductionSkipHeight;
    introSkip.visible = false;
    introSkip.layer = 32;
}

void ArticleTransitionController::DestroyEntities(core::GameContext& ctx) {
    RestoreMainCamera(ctx);

    m_entityOwner.DestroyAll(ctx.world);

    m_globeEntity = m_bgEntity = m_cameraEntity = UINT32_MAX;
    m_textEntity = m_progressTextEntity = m_captionTextEntity = UINT32_MAX;
    m_introductionPanelEntity = m_introductionDetailEntity =
        m_introductionSkipEntity = UINT32_MAX;
}

void ArticleTransitionController::CaptureMainCamera(core::GameContext& ctx) {
    m_previousMainCameraEntity = UINT32_MAX;

    ctx.world.Query<components::Camera>().Each(
        [&](ecs::Entity entity, components::Camera& camera) {
            if (camera.isMainCamera && m_previousMainCameraEntity == UINT32_MAX) {
                m_previousMainCameraEntity = entity;
            }
            camera.isMainCamera = false;
        });
}

void ArticleTransitionController::RestoreMainCamera(core::GameContext& ctx) {
    if (m_previousMainCameraEntity == UINT32_MAX) {
        return;
    }

    if (ctx.world.IsAlive(m_previousMainCameraEntity)) {
        if (auto* camera = ctx.world.Get<components::Camera>(m_previousMainCameraEntity)) {
            camera->isMainCamera = true;
        }
    }
    m_previousMainCameraEntity = UINT32_MAX;
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
                    // 同期処理ではなく、インクリメンタル構築を開始する
                    auto asyncData = m_loadTask.get();
                    bool hasError = !asyncData.hasData || asyncData.allLinks.empty() || asyncData.articleText.empty();

                    if (hasError) {
                        LOG_WARN("Transition", "Failed to transition to '{}': No links or load error. Returning to '{}'", m_targetPage, m_previousPage);
                        m_hasError = true;
                        m_errorTimer = 0.0f;
                        m_errorMsg = L"「" + core::ToWString(m_targetPage) + L"」にはリンクがないか、読込エラーです。\n前のページ「" + core::ToWString(m_previousPage) + L"」に戻ります...";

                        // 次にロードするターゲットを前のページに変更
                        m_targetPage = m_previousPage;

                        // 前のページのロードタスクを開始
                        if (m_pageLoader) {
                            auto pageLoaderPtr = m_pageLoader;
                            std::string page = m_targetPage;
                            m_fetchStartedAt = std::chrono::steady_clock::now();
                            m_loadTask = std::async(std::launch::async, [pageLoaderPtr, page]() {
                                return pageLoaderPtr->FetchPageDataAsync(page);
                            });
                            m_loadCompleted = false; // 再度ロード待ちにする
                        }

                        m_phase = Phase::ErrorWait;
                    } else {
                        LOG_INFO("Transition",
                                 "Async fetch complete page='{}' elapsed={}ms "
                                 "total={}ms links={} extractBytes={} categories={}",
                                 m_targetPage, ElapsedMs(m_fetchStartedAt),
                                 ElapsedMs(m_transitionStartedAt),
                                 asyncData.allLinks.size(),
                                 asyncData.articleText.size(),
                                 asyncData.pageCategories.size());
                        if (m_pageLoader) {
                            LOG_INFO("Transition", "Async fetch complete. Starting incremental build...");
                            m_buildStartedAt = std::chrono::steady_clock::now();
                            m_pageLoader->BeginBuildPage(ctx, std::move(asyncData), m_targetBall, m_targetCam, m_targetSky, m_minimap);
                            m_phase = Phase::Building;
                        } else {
                            m_phase = Phase::FadeOut;
                        }
                    }
                }
            }
            break;

        case Phase::ErrorWait:
            m_errorTimer += dt;
            if (m_errorTimer >= 4.0f) { // 4秒間表示
                m_phase = Phase::Loading;
                m_hasError = false;
                if (auto* text = ctx.world.Get<components::UIText>(m_textEntity)) {
                    text->text = L"Traveling to " + core::ToWString(m_targetPage) + L"...";
                }
            }
            break;

        case Phase::Building:
            if (m_pageLoader) {
                constexpr auto kBuildBudget = std::chrono::milliseconds(24);
                const auto stepStart = std::chrono::steady_clock::now();
                bool done =
                    m_pageLoader->StepBuildPageWithinFrameBudget(ctx, kBuildBudget);
                const auto stepElapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - stepStart);
                if (stepElapsed.count() > 33) {
                    LOG_WARN("Transition",
                             "Build step took {} ms at progress {:.2f}",
                             stepElapsed.count(), m_pageLoader->GetBuildProgress());
                }
                if (done) {
                    LOG_INFO("Transition",
                             "Incremental build complete page='{}' elapsed={}ms "
                             "total={}ms",
                             m_targetPage, ElapsedMs(m_buildStartedAt),
                             ElapsedMs(m_transitionStartedAt));
                    BeginCourseIntroduction(ctx);
                }
            } else {
                m_phase = Phase::FadeOut;
            }
            break;

        case Phase::CourseIntroduction:
            UpdateCourseIntroduction(ctx, dt);
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

    return false;
}

void ArticleTransitionController::UpdateAnimation(core::GameContext& ctx, float dt) {
    if (m_phase == Phase::CourseIntroduction) {
        return;
    }
    // 地球儀の自転
    m_globeRotation += dt * 0.5f;
    if (auto* tr = ctx.world.Get<components::Transform>(m_globeEntity)) {
        auto rot = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, m_globeRotation, 0.2f);
        DirectX::XMStoreFloat4(&tr->rotation, rot);
    }
}

void ArticleTransitionController::UpdateUI(core::GameContext& ctx, float dt) {
    if (m_phase == Phase::CourseIntroduction) {
        return;
    }
    if (m_hasError) {
        if (auto* text = ctx.world.Get<components::UIText>(m_progressTextEntity)) {
            text->text = m_errorMsg;
            auto style = m_progressStyle;
            style.fontSize = 20.0f;
            style.color = {1.0f, 0.35f, 0.35f, m_fadeAlpha}; // 赤色で強調
            text->style = style;
        }
        if (auto* text = ctx.world.Get<components::UIText>(m_textEntity)) {
            text->text = L"エラーが発生しました";
            auto style = m_primaryStyle;
            style.color = {1.0f, 0.35f, 0.35f, m_fadeAlpha};
            text->style = style;
        }
    } else {
        float progress = 0.0f;
        if (m_phase == Phase::FadeIn) {
            progress = 0.0f;
        } else if (m_phase == Phase::Loading) {
            // 通信待ちは最大 20% とする
            progress = std::clamp(m_stateTimer / 5.0f, 0.0f, 0.2f);
        } else if (m_phase == Phase::Building) {
            // 構築進捗は 20% ~ 100%
            float buildProgress = m_pageLoader ? m_pageLoader->GetBuildProgress() : 0.0f;
            progress = 0.2f + 0.8f * buildProgress;
        } else {
            progress = 1.0f;
        }

        m_displayProgress = std::max(m_displayProgress,
                                     std::clamp(progress, 0.0f, 1.0f));
        int percent = static_cast<int>(m_displayProgress * 100.0f);

        if (auto* text = ctx.world.Get<components::UIText>(m_progressTextEntity)) {
            text->text = L"Loading... " + std::to_wstring(percent) + L"%";
            auto style = m_progressStyle;
            style.color.w = m_fadeAlpha;
            text->style = style;
        }
        if (auto* text = ctx.world.Get<components::UIText>(m_textEntity)) {
            text->text = L"Traveling to " + core::ToWString(m_targetPage) + L"...";
            auto style = m_primaryStyle;
            style.color.w = m_fadeAlpha;
            text->style = style;
        }
    }

    const std::array<std::wstring, 60> tips = {
        L"バンカーの砂粒を数えています。現在1,048,576粒目です。",
        L"ジミー・ウェールズの写真に、季節に合った服を着せています。",
        L"キャディが「パー」の語源を調べています。現在、古代ラテン語の項目まで遡りました。",
        L"ゴルフクラブ14本を五十音順に並べ替えています。",
        L"カップを一度外に出して、拭いてから戻しています。",
        L"芝の一本一本に個別のURLを割り当てています。",
        L"ゴルフボールのディンプル数を手作業で数え直しています。",
        L"グリーンの芝をアルファベット順に並べています。ほぼ変わりません。",
        L"風を一時停止して、折り畳んでいます。",
        L"ボールに名前をつけています。現在「太郎」まで終わりました。",
        L"フェアウェイの緑色を16進数カラーコードで再定義しています。",
        L"キャディが「バーディ」の鳥の種類を調べています。現在、鳥類学の専門論文まで到達しました。",
        L"カップの円周率を小数点以下100桁まで確認しています。",
        L"ゴルフボールの影を手書きで描いています。",
        L"風速を手旗信号に変換しています。受信者はまだ見つかっていません。",
        L"ジミー・ウェールズに今日の天気を報告しています。",
        L"ボールの軌道を万葉仮名で記録しています。",
        L"旗の色について、ノートページで議論が白熱しています。現在234コメント目です。",
        L"前のプレイヤーが残した足跡を一つずつ消しゴムで消しています。",
        L"ホールの旗を、風向きに関係なく常に真北に向けるよう調整しています。",
        L"バンカーの砂を粒ごとにソートしています。比較関数の選定中です。",
        L"フェアウェイの長さをプランク長で計算しています。",
        L"木の葉の枚数を数えています。風で揺れるたびに最初からやり直しています。",
        L"キャディが昨日見た夢について、ノートページに記述しています。",
        L"旗の布地の素材について出典のある文献を探しています。現在、検索結果が0件です。",
        L"芝の一本一本に住民票を交付しています。",
        L"ジミー・ウェールズの眉毛の本数をデータベースに登録しています。",
        L"カップの深さを宇宙の膨張速度で割っています。 意味は不明です。",
        L"キャディが「芝」の記事を全言語版で読み比べています。現在フィンランド語版です。",
        L"ゴルフボールの白さが「白」のWikipedia記事の定義と一致するか照合しています。",
        L"ボールに「出典」を付与しています。信頼できる情報源が見つかり次第、発射できます。",
        L"クラブ14本の特筆性を審査中です。認められたものだけバッグに入ります。",
        L"キャディがWikipediaで「正しいスイングフォーム」を調べています。",
        L"ティーグラウンドの位置について、ノートページで合意形成中です。完了次第ロードします。",
        L"グリーンの傾斜データは匿名IPユーザーが入力しました。正確性は保証されません。",
        L"サーバーが「善意に基づいて」ロードしています。完了時刻は保証されません。",
        L"風向きの計算に使う記事が、現在編集合戦中のため、風が安定していません。",
        L"スコアカードをWikipediaの「表の書き方」ガイドラインに準拠した形式に変換しています。",
        L"カップの縁を「秀逸な記事」の基準に照らして磨いています。",
        L"このゲームのWikipedia記事の草稿を作成しています。現在、特筆性の確認で止まっています。",
        L"ゴルフボールを「曖昧さ回避」ページ経由で打ち込んでいます。転送先は3つあります。",
        L"カップの座標をWikidataのQ番号に変換しています。",
        L"クラブの振り方を14言語に翻訳した後、日本語に再翻訳しています。若干ずれる場合があります。",
        L"空の色がWikipediaの「青」の定義と一致するか照合しています。",
        L"このロード画面のアスペクト比が黄金比であるか確認しています。",
        L"Botがロード画面を誤ってロールバックしました。1つ前の状態から再ロード中です。",
        L"荒らし対策Botが、起動操作を「不審な編集」と判定しました。審査中です。",
        L"深夜0時、Botがカップの直径を0.1mmずつ削り続けています。 ロード中も止まりません。",
        L"キャディのユーザー名を決めています。現在「Golf_bot_4829」まで試しました。",
        L"空を「要出典」から「出典あり」に格上げするための文献を探しています。",
        L"このTipsには「要出典」タグが付いています。内容の正確性は確認されていません。",
        L"このTipsは現在、ノートページで「削除すべきかどうか」が議論されています。表示されていれば、まだ生き残っています。",
        L"ロード中にTipsを生成しています。 つまり、あなたは今、ロード待ちのロード待ちをしています。",
        L"このTipsを読み終わる頃にはロードが完了しているはずです。（出典なし）",
        L"このロード画面はCCライセンスで公開されています。改変・再配布は自由ですが、出典を明記してください。",
        L"このゲームは存在しますが、Wikipediaの記事がないため、公式には存在していません。",
        L"ゲームを起動したあなたの行動は、すでに「最近の更新」に記録されました。",
        L"「赤リンク」から生成された地形には、まだ何もありません。何があるかは誰も知りません。",
        L"ホールアウト後の記録は自動的に一覧へ追記されます。特筆性がなければ即時削除されます。",
        L"ホールの番号を素因数分解して確認しています。"
    };
    m_tipTimer += dt;
    if (m_tipTimer > 3.0f) {
        m_tipTimer = 0.0f;
        m_tipIndex = (m_tipIndex + 1) % tips.size();
    }
    if (auto* text = ctx.world.Get<components::UIText>(m_captionTextEntity)) {
        text->text = tips[m_tipIndex];
        auto style = m_captionStyle;
        style.color.w = m_fadeAlpha;
        text->style = style;
    }
}

void ArticleTransitionController::BeginCourseIntroduction(core::GameContext& ctx) {
    if (!m_pageLoader) {
        m_phase = Phase::FadeOut;
        return;
    }

    const scenes::CourseIntroductionData& data =
        m_pageLoader->GetCourseIntroductionData();
    const float teeZ = -data.fieldDepth * 0.4f;
    const auto featured = game::utils::SelectFeaturedCourseHoles(
        data.holes, 0.0f, teeZ, 5);
    LOG_INFO("Transition",
             "Course introduction page='{}' abstractChars={} holes={} goals={} "
             "oneHopFeatured={}",
             data.pageName, data.abstractText.size(), data.holes.size(),
             featured.goals.size(), featured.oneHop.size());

    m_introductionShots.clear();
    IntroductionShot overview;
    overview.kind = IntroductionShotKind::Overview;
    const float overviewHeight =
        std::clamp(std::max(data.fieldWidth, data.fieldDepth) * 0.45f,
                   55.0f, 900.0f);
    overview.cameraPosition = {0.0f, overviewHeight,
                               -data.fieldDepth * 0.15f};
    overview.focusPosition = {0.0f, 0.0f, 0.0f};
    overview.label = L"ABOUT THIS COURSE";
    overview.title = core::ToWString(data.pageName);
    overview.body = data.abstractText;
    std::wostringstream courseStats;
    courseStats << L"PAR " << data.par << L"   HOLES " << data.holes.size()
                << L"   SIZE " << static_cast<int>(std::round(data.fieldWidth))
                << L" × " << static_cast<int>(std::round(data.fieldDepth))
                << L"   WIND " << std::fixed << std::setprecision(1)
                << data.windSpeed << L" m/s";
    overview.detail = courseStats.str();
    overview.duration = 7.0f;
    m_introductionShots.push_back(std::move(overview));

    if (featured.goals.size() <= 3) {
        for (std::size_t index = 0; index < featured.goals.size(); ++index) {
            const auto& hole = featured.goals[index];
            IntroductionShot goal;
            goal.kind = IntroductionShotKind::Goal;
            goal.cameraPosition = {hole.x + 12.0f, hole.y + 15.0f,
                                   hole.z - 18.0f};
            goal.focusPosition = {hole.x, hole.y + 1.5f, hole.z};
            goal.label = L"DIRECT GOAL";
            goal.title = core::ToWString(hole.linkTarget);
            goal.body = L"このホールから目的記事へ直接到達できます。";
            goal.detail = L"GOAL HOLE " + std::to_wstring(index + 1) + L" / " +
                          std::to_wstring(featured.goals.size());
            goal.duration = 3.0f;
            m_introductionShots.push_back(std::move(goal));
        }
    } else {
        float centerX = 0.0f;
        float centerY = 0.0f;
        float centerZ = 0.0f;
        for (const auto& hole : featured.goals) {
            centerX += hole.x;
            centerY += hole.y;
            centerZ += hole.z;
        }
        centerX /= static_cast<float>(featured.goals.size());
        centerY /= static_cast<float>(featured.goals.size());
        centerZ /= static_cast<float>(featured.goals.size());
        float radius = 0.0f;
        for (const auto& hole : featured.goals) {
            const float dx = hole.x - centerX;
            const float dz = hole.z - centerZ;
            radius = std::max(radius, std::sqrt(dx * dx + dz * dz));
        }

        IntroductionShot goals;
        goals.kind = IntroductionShotKind::GoalGroup;
        const float distance = std::max(24.0f, radius * 0.55f);
        goals.cameraPosition = {centerX,
                                centerY + std::max(35.0f, radius * 1.25f),
                                centerZ - distance};
        goals.focusPosition = {centerX, centerY + 1.5f, centerZ};
        goals.label = L"DIRECT GOALS";
        goals.title = L"ゴールホール " +
                      std::to_wstring(featured.goals.size()) + L"カ所";
        goals.body = L"赤い旗は、どれも目的記事へ直接つながっています。";
        goals.detail = L"ALL GOAL HOLES";
        goals.duration = 4.5f;
        m_introductionShots.push_back(std::move(goals));
    }

    if (featured.goals.empty()) {
        for (std::size_t index = 0; index < featured.oneHop.size(); ++index) {
            const auto& hole = featured.oneHop[index];
            IntroductionShot oneHop;
            oneHop.kind = IntroductionShotKind::OneHop;
            oneHop.cameraPosition = {hole.x + 11.0f, hole.y + 14.0f,
                                     hole.z - 17.0f};
            oneHop.focusPosition = {hole.x, hole.y + 1.5f, hole.z};
            oneHop.label = L"PRIORITY HOLE · 1 HOP";
            oneHop.title = core::ToWString(hole.linkTarget);
            oneHop.body = L"このリンク先から、あと1回の移動で目的記事へ到達できます。";
            oneHop.detail = L"RECOMMENDED " + std::to_wstring(index + 1) + L" / " +
                            std::to_wstring(featured.oneHop.size());
            oneHop.duration = 2.8f;
            m_introductionShots.push_back(std::move(oneHop));
        }
    }

    IntroductionShot ready;
    ready.kind = IntroductionShotKind::ReturnToTee;
    ready.cameraPosition = {0.0f, 18.0f, teeZ - 22.0f};
    ready.focusPosition = {0.0f, 1.0f, teeZ + 12.0f};
    ready.label = L"READY";
    ready.title = L"TEE OFF";
    ready.body = L"価値の高いホールを狙って、目的記事を目指しましょう。";
    ready.detail = L"PLAY";
    ready.duration = 2.5f;
    m_introductionShots.push_back(std::move(ready));

    if (auto* globe = ctx.world.Get<components::MeshRenderer>(m_globeEntity)) {
        globe->isVisible = false;
    }
    if (auto* background = ctx.world.Get<components::MeshRenderer>(m_bgEntity)) {
        background->isVisible = false;
    }
    if (auto* transitionCamera = ctx.world.Get<components::Camera>(m_cameraEntity)) {
        transitionCamera->isMainCamera = false;
    }
    if (auto* courseCamera = ctx.world.Get<components::Camera>(m_targetCam)) {
        courseCamera->isMainCamera = true;
    }

    ctx.input.SetMouseCursorVisible(true);
    ctx.input.SetMouseCursorLocked(false);

    m_phase = Phase::CourseIntroduction;
    m_introductionShotIndex = 0;
    m_introductionShotTimer = 0.0f;
    if (auto* transform = ctx.world.Get<components::Transform>(m_targetCam)) {
        m_introductionCameraFrom = transform->position;
    }
    ApplyCourseIntroductionShot(ctx);
}

void ArticleTransitionController::UpdateCourseIntroduction(
    core::GameContext& ctx, float dt) {
    if (m_introductionShots.empty() ||
        m_introductionShotIndex >= m_introductionShots.size()) {
        FinishCourseIntroduction(ctx);
        return;
    }

    const auto mousePosition = ctx.input.GetMousePosition();
    const bool skipHovered =
        mousePosition.x >= kIntroductionSkipX &&
        mousePosition.x <= kIntroductionSkipX + kIntroductionSkipWidth &&
        mousePosition.y >= kIntroductionSkipY &&
        mousePosition.y <= kIntroductionSkipY + kIntroductionSkipHeight;
    if (auto* skip = ctx.world.Get<components::UIText>(m_introductionSkipEntity)) {
        skip->style.bgColor = skipHovered
            ? game::ui::kColorSurfaceRaised
            : game::ui::kColorShotBtn;
        skip->style.borderColor = skipHovered
            ? game::ui::kColorAccent
            : game::ui::kColorShotBtnBorder;
    }
    if (skipHovered && ctx.input.GetMouseButtonDown(0)) {
        FinishCourseIntroduction(ctx);
        return;
    }

    m_introductionShotTimer += dt;
    const IntroductionShot& shot = m_introductionShots[m_introductionShotIndex];
    const float rawT = std::clamp(
        m_introductionShotTimer / kIntroductionCameraMoveDuration, 0.0f, 1.0f);
    const float easedT = rawT * rawT * (3.0f - 2.0f * rawT);
    if (auto* transform = ctx.world.Get<components::Transform>(m_targetCam)) {
        transform->position.x = m_introductionCameraFrom.x +
            (shot.cameraPosition.x - m_introductionCameraFrom.x) * easedT;
        transform->position.y = m_introductionCameraFrom.y +
            (shot.cameraPosition.y - m_introductionCameraFrom.y) * easedT;
        transform->position.z = m_introductionCameraFrom.z +
            (shot.cameraPosition.z - m_introductionCameraFrom.z) * easedT;

        const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&transform->position);
        const DirectX::XMVECTOR focus =
            DirectX::XMLoadFloat3(&shot.focusPosition);
        const DirectX::XMVECTOR direction = DirectX::XMVectorSubtract(focus, eye);
        if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(direction)) > 0.001f) {
            const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
                eye, focus, DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
            DirectX::XMStoreFloat4(
                &transform->rotation,
                DirectX::XMQuaternionRotationMatrix(
                    DirectX::XMMatrixInverse(nullptr, view)));
        }
    }

    if (m_introductionShotTimer < shot.duration) {
        return;
    }

    ++m_introductionShotIndex;
    if (m_introductionShotIndex >= m_introductionShots.size()) {
        FinishCourseIntroduction(ctx);
        return;
    }
    m_introductionShotTimer = 0.0f;
    if (auto* transform = ctx.world.Get<components::Transform>(m_targetCam)) {
        m_introductionCameraFrom = transform->position;
    }
    ApplyCourseIntroductionShot(ctx);
}

void ArticleTransitionController::ApplyCourseIntroductionShot(
    core::GameContext& ctx) {
    const IntroductionShot& shot = m_introductionShots[m_introductionShotIndex];

    if (auto* panel = ctx.world.Get<components::UIText>(m_introductionPanelEntity)) {
        panel->visible = true;
    }
    if (auto* label = ctx.world.Get<components::UIText>(m_progressTextEntity)) {
        label->text = L"WIKI  ·  " + shot.label;
        label->x = 82.0f;
        label->y = 72.0f;
        label->width = 554.0f;
        label->height = 24.0f;
        label->style = graphics::TextStyle::CardLabel();
        label->style.fontSize = 14.0f;
        label->style.color = game::ui::kColorAccent;
        label->visible = true;
        label->layer = 32;
    }
    if (auto* title = ctx.world.Get<components::UIText>(m_textEntity)) {
        title->text = shot.title;
        title->x = 82.0f;
        title->y = 101.0f;
        title->width = 554.0f;
        title->height = 58.0f;
        if (shot.kind == IntroductionShotKind::Goal ||
            shot.kind == IntroductionShotKind::GoalGroup) {
            title->style = graphics::TextStyle::GoalHighlight();
        } else {
            title->style = graphics::TextStyle::BrowserURL();
        }
        title->style.fontSize = 32.0f;
        title->style.align = graphics::TextAlign::Left;
        title->visible = true;
        title->layer = 32;
    }
    if (auto* body = ctx.world.Get<components::UIText>(m_captionTextEntity)) {
        body->text = shot.body;
        body->x = 82.0f;
        body->y = 166.0f;
        body->width = 554.0f;
        body->height = 104.0f;
        body->style = graphics::TextStyle::BrowserURL();
        body->style.fontSize = 16.0f;
        body->style.color = game::ui::kColorTextPrimary;
        body->style.align = graphics::TextAlign::Left;
        body->visible = true;
        body->layer = 32;
    }
    if (auto* detail = ctx.world.Get<components::UIText>(m_introductionDetailEntity)) {
        detail->text = shot.detail;
        detail->style = graphics::TextStyle::BrowserSub();
        detail->style.fontFamily = "Share Tech Mono";
        detail->style.fontSize = 14.0f;
        detail->style.color = game::ui::kColorTextSub;
        detail->style.align = graphics::TextAlign::Left;
        detail->visible = true;
    }
    if (auto* skip = ctx.world.Get<components::UIText>(m_introductionSkipEntity)) {
        skip->style = graphics::TextStyle::BrowserURL();
        skip->style.fontSize = 17.0f;
        skip->style.align = graphics::TextAlign::Center;
        skip->style.valign = graphics::TextVAlign::Middle;
        skip->style.color = game::ui::kColorTextPrimary;
        skip->style.bgColor = game::ui::kColorShotBtn;
        skip->style.cornerRadius = game::ui::kRadiusChip;
        skip->style.borderWidth = game::ui::kBorderWidthThin;
        skip->style.borderColor = game::ui::kColorShotBtnBorder;
        skip->style.hasShadow = false;
        skip->visible = true;
    }
}

void ArticleTransitionController::FinishCourseIntroduction(
    core::GameContext& ctx) {
    const std::array<ecs::Entity, 6> introductionUi = {
        m_textEntity, m_progressTextEntity, m_captionTextEntity,
        m_introductionPanelEntity, m_introductionDetailEntity,
        m_introductionSkipEntity};
    for (ecs::Entity entity : introductionUi) {
        if (auto* text = ctx.world.Get<components::UIText>(entity)) {
            text->visible = false;
        }
    }
    m_fadeAlpha = 0.0f;
    m_phase = Phase::FadeOut;
}

} // namespace game::controllers

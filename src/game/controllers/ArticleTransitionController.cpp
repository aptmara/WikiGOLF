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
#include <array>
#include <chrono>

namespace game::controllers {

namespace {

/**
 * @brief 開始時刻からの経過時間をミリ秒で返します。 山内陽
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
        // 非同期ロード開始
        auto pageLoaderPtr = m_pageLoader;
        std::string page = targetPage;
        m_fetchStartedAt = std::chrono::steady_clock::now();
        LOG_INFO("Transition", "Async fetch started page='{}'", page);
        m_loadTask = std::async(std::launch::async, [pageLoaderPtr, page]() {
            return pageLoaderPtr->FetchPageDataAsync(page);
        });
    } else {
        LOG_ERROR("Transition", "WikiPageLoader is null!");
    }
}

void ArticleTransitionController::SpawnEntities(core::GameContext& ctx) {
    auto shaderHandle = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl", L"Assets/shaders/BasicPS.hlsl");

    CaptureMainCamera(ctx);

    // トランジション専用カメラ（既存フィールドと干渉しないよう遥か上空に配置）
    m_cameraEntity = ctx.world.CreateEntity();
    auto& camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
    camTr.position = {0.0f, 5000.0f, -30.0f}; 
    camTr.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    auto& cam = ctx.world.Add<components::Camera>(m_cameraEntity);
    cam.fov = DirectX::XM_PIDIV4;
    cam.nearZ = 0.1f;
    cam.farZ = 1000.0f;
    cam.isMainCamera = true; // メインカメラをジャックする

    // 地球儀エンティティの生成
    m_globeEntity = ctx.world.CreateEntity();
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
    m_bgEntity = ctx.world.CreateEntity();
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
    RestoreMainCamera(ctx);

    if (ctx.world.IsAlive(m_globeEntity)) ctx.world.DestroyEntity(m_globeEntity);
    if (ctx.world.IsAlive(m_bgEntity)) ctx.world.DestroyEntity(m_bgEntity);
    if (ctx.world.IsAlive(m_cameraEntity)) ctx.world.DestroyEntity(m_cameraEntity);
    if (ctx.world.IsAlive(m_textEntity)) ctx.world.DestroyEntity(m_textEntity);
    if (ctx.world.IsAlive(m_progressTextEntity)) ctx.world.DestroyEntity(m_progressTextEntity);
    if (ctx.world.IsAlive(m_captionTextEntity)) ctx.world.DestroyEntity(m_captionTextEntity);

    m_globeEntity = m_bgEntity = m_cameraEntity = UINT32_MAX;
    m_textEntity = m_progressTextEntity = m_captionTextEntity = UINT32_MAX;
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
                    m_phase = Phase::FadeOut;
                }
            } else {
                m_phase = Phase::FadeOut;
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

    return false;
}

void ArticleTransitionController::UpdateAnimation(core::GameContext& ctx, float dt) {
    // 地球儀の自転
    m_globeRotation += dt * 0.5f;
    if (auto* tr = ctx.world.Get<components::Transform>(m_globeEntity)) {
        auto rot = DirectX::XMQuaternionRotationRollPitchYaw(0.0f, m_globeRotation, 0.2f);
        DirectX::XMStoreFloat4(&tr->rotation, rot);
    }
}

void ArticleTransitionController::UpdateUI(core::GameContext& ctx, float dt) {
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
        auto style = m_primaryStyle;
        style.color.w = m_fadeAlpha;
        text->style = style;
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

} // namespace game::controllers

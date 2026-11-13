/**
 * @file LoadingScene.cpp
 * @brief ローディング画面シーン（ゴルフボール物理演出）の実装
*/

#include "LoadingScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "../utils/ParRules.h"
#include "LoadingSceneUtils.h"
#include "WikiPageDataFetcher.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d2d1_1.h>
#include <filesystem>
#include <random>
#include <thread>

namespace game::scenes {

namespace {

/**
 * @brief ロード前グローバルデータが明示的な開始指定か判定します。
 * @details 標準スタートでは空データを渡すため、空のままならランダム抽選を継続します。
*/
bool HasExplicitStartData(const game::components::WikiGlobalData &data) {
  return data.isUserOverride || !data.startPage.empty() ||
         !data.targetPage.empty() || data.targetPageId != -1 ||
         data.isDailyChallenge;
}

/**
 * @brief 開始時刻からの経過時間をミリ秒で返します。
*/
long long ElapsedMs(const std::chrono::steady_clock::time_point &startedAt) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - startedAt)
      .count();
}

} // namespace

LoadingScene::LoadingScene(
    std::function<std::unique_ptr<core::Scene>()> nextSceneFactory)
    : m_nextSceneFactory(std::move(nextSceneFactory)) {}

void LoadingScene::OnEnter(core::GameContext &ctx) {
  const auto enterStartedAt = std::chrono::steady_clock::now();
  LOG_INFO("LoadingScene", "OnEnter");

  m_balls.clear();
  m_spawnedCount = 0;
  m_spawnTimer = 0.0f;
  m_fadeAlpha = 0.0f;
  m_fadeStarted = false;
  m_fadeDelay = 0.6f;
  m_sceneTime = 0.0f;
  m_tipTimer = 0.0f;
  m_tipIndex = 0;
  m_cameraTime = 0.0f;
  m_logTimer = 0.0f;
  m_spawnFinishedLogged = false;
  m_allSettledLogged = false;
  m_fadeLogged = false;
  m_movingCount = 0;
  m_maxSpeed = 0.0f;
  m_avgSpeed = 0.0f;
  m_settledCount = 0;
  m_lastSettledCount = 0;
  m_stuckTimer = 0.0f;
  m_lastMovingPos = {0.0f, 0.0f, 0.0f};
  m_hasMovingSample = false;
  m_forceFinishTimer = 0.0f;
  m_isLoading = false;
  m_loadCompleted = false;
  m_loadedData.reset();
  m_uiProgress = 0.0f;
  m_explosionTimer = 0.0f;
  m_exploded = false;
  m_loadProgress = std::make_shared<std::atomic<float>>(0.0f);

  // ゲームプレイ用アセットの先行ロードキューを再構築
  m_preloadTasks.clear();
  m_preloadIndex = 0;
  m_preloadComplete = false;
  BuildGameplayPreloadQueue();

  std::string overrideStartPage;
  std::string overrideTargetPage;
  int overrideTargetId = -1;
  bool overrideIsUserOverride = false;
  bool overrideIsDailyChallenge = false;
  bool overrideIsOniMode = false;
  std::uint32_t overrideDailyChallengeSeed = 0;
  if (auto* globalData = ctx.world.GetGlobal<game::components::WikiGlobalData>()) {
    overrideIsOniMode = globalData->isOniMode;
    if (HasExplicitStartData(*globalData)) {
      overrideStartPage = globalData->startPage;
      overrideTargetPage = globalData->targetPage;
      overrideTargetId = globalData->targetPageId;
      overrideIsUserOverride = globalData->isUserOverride;
      overrideIsDailyChallenge = globalData->isDailyChallenge;
      overrideDailyChallengeSeed = globalData->dailyChallengeSeed;
      // デイリーは全員同じ条件にするため、常に通常モードで遊ぶ。
      if (overrideIsDailyChallenge) {
        overrideIsOniMode = false;
      }
      LOG_INFO("LoadingScene", "Found overridden global data: Start={}, Target={}", overrideStartPage, overrideTargetPage);
    } else {
      LOG_INFO("LoadingScene", "No explicit start data. Standard random selection will run.");
    }
  }

  // 非同期ロード開始
  m_isLoading = true;
  auto progressPtr = m_loadProgress;
  m_loadTask = std::async(std::launch::async, [progressPtr, overrideStartPage, overrideTargetPage, overrideTargetId, overrideIsUserOverride, overrideIsDailyChallenge, overrideIsOniMode, overrideDailyChallengeSeed]() {
    const auto loadStartedAt = std::chrono::steady_clock::now();
    const auto setProgress = [progressPtr](float value) {
      if (progressPtr) {
        progressPtr->store(std::clamp(value, 0.0f, 1.0f),
                           std::memory_order_relaxed);
      }
    };
    const auto logStage = [&](const char *stage, float progress) {
      LOG_INFO("LoadingScene",
               "AsyncLoad stage={} elapsed={}ms progress={:.0f}%",
               stage, ElapsedMs(loadStartedAt), progress * 100.0f);
    };

    setProgress(0.02f);
    logStage("start", 0.02f);
    auto data = std::make_unique<game::components::WikiGlobalData>();
    data->startPage = overrideStartPage;
    data->targetPage = overrideTargetPage;
    data->targetPageId = overrideTargetId;
    data->isUserOverride = overrideIsUserOverride;
    data->isDailyChallenge = overrideIsDailyChallenge;
    data->isOniMode = overrideIsOniMode;
    data->dailyChallengeSeed = overrideDailyChallengeSeed;

    // WikiShortestPathの初期化（重い処理）
    const auto dbStartedAt = std::chrono::steady_clock::now();
    data->pathSystem = std::make_unique<game::systems::WikiShortestPath>();
    bool dbLoaded =
        data->pathSystem->Initialize();
    setProgress(0.3f);
    const char *dbLoadedLabel = "false";
    if (dbLoaded) {
      dbLoadedLabel = "true";
    }
    LOG_INFO("LoadingScene", "AsyncLoad dbInitialize loaded={} elapsed={}ms",
             dbLoadedLabel, ElapsedMs(dbStartedAt));
    logStage("db-ready", 0.3f);
    if (!dbLoaded) {
      LOG_WARN("LoadingScene",
               "AsyncLoad DB initialization failed. Falling back to API target.");
    }

    /**
     * @brief ユーザー指定ターゲットでも経路評価用のページIDを補完します。
*/
    const auto resolveTargetPageId = [&]() {
      if (!dbLoaded || !data->pathSystem || !data->pathSystem->IsAvailable() ||
          data->targetPage.empty() || data->targetPageId != -1) {
        return;
      }

      data->targetPageId = data->pathSystem->ResolvePageId(data->targetPage);
      if (data->targetPageId != -1) {
        LOG_INFO("LoadingScene", "Resolved target page ID: {} -> {}",
                 data->targetPage, data->targetPageId);
      } else {
        LOG_WARN("LoadingScene", "Failed to resolve target page ID: {}",
                 data->targetPage);
      }
    };

    // スタート選定
    // デイリーは全員同じ条件にするため、スタートもゴールもサーバー日付から決める。
    // スタートはゴールが決まってから、ゴールまでの手数を見て選ぶ(下の処理)。
    game::systems::WikiClient wikiClient;
    const bool selectDailyStart =
        data->isDailyChallenge && data->startPage.empty();
    if (data->isDailyChallenge) {
      data->dailyChallengeSeed = wikiClient.FetchServerDateSeed();
      LOG_INFO("LoadingScene",
               "AsyncLoad daily challenge server date seed={}",
               data->dailyChallengeSeed);
    }
    if (selectDailyStart) {
      LOG_INFO("LoadingScene",
               "AsyncLoad daily start will be selected after the target");
    } else if (data->startPage.empty()) {
      const auto randomStartedAt = std::chrono::steady_clock::now();
      data->startPage = wikiClient.FetchRandomPageTitle();
      LOG_INFO("LoadingScene",
               "AsyncLoad random start fetched: title='{}' elapsed={}ms",
               data->startPage, ElapsedMs(randomStartedAt));
    } else {
      LOG_INFO("LoadingScene", "AsyncLoad start override used: title='{}'",
               data->startPage);
    }
    setProgress(0.45f);
    logStage("start-page-ready", 0.45f);

    // 人気記事からターゲット選定
    constexpr int kTargetMinIncomingLinks = 10000;
    constexpr int kFallbackTargetMinIncomingLinks = 5000;
    std::uint32_t dailySelectionOffset = 0;
    const auto fetchPopularTarget = [&](int minIncomingLinks) {
      if (data->isDailyChallenge) {
        return data->pathSystem->FetchPopularPageTitle(
            minIncomingLinks,
            data->dailyChallengeSeed + dailySelectionOffset++);
      }
      return data->pathSystem->FetchPopularPageTitle(minIncomingLinks);
    };
    if (data->targetPage.empty() && dbLoaded && data->pathSystem->IsAvailable()) {
      const auto targetStartedAt = std::chrono::steady_clock::now();
      auto result = fetchPopularTarget(kTargetMinIncomingLinks);
      data->targetPage = result.first;
      data->targetPageId = result.second;
      setProgress(0.7f);
      if (data->targetPage.empty()) {
        result = fetchPopularTarget(kFallbackTargetMinIncomingLinks);
        data->targetPage = result.first;
        data->targetPageId = result.second;
      }
      LOG_INFO("LoadingScene",
               "AsyncLoad popular target fetched: title='{}' id={} elapsed={}ms",
               data->targetPage, data->targetPageId, ElapsedMs(targetStartedAt));
    } else if (!data->targetPage.empty()) {
      LOG_INFO("LoadingScene",
               "AsyncLoad target override used: title='{}' id={}",
               data->targetPage, data->targetPageId);
    }

    resolveTargetPageId();

    // フォールバック
    if (data->targetPage.empty()) {
      const auto fallbackStartedAt = std::chrono::steady_clock::now();
      data->targetPage = wikiClient.FetchTargetPageTitle();
      data->targetPageId = -1;
      resolveTargetPageId();
      setProgress(0.8f);
      LOG_INFO("LoadingScene",
               "AsyncLoad fallback target fetched: title='{}' elapsed={}ms",
               data->targetPage, ElapsedMs(fallbackStartedAt));
    }

    // デイリーのスタート: ゴールまで3〜5手の記事を日付seedから決定的に選ぶ。
    if (selectDailyStart) {
      int dailyStartHops = -1;
      if (dbLoaded && data->pathSystem && data->pathSystem->IsAvailable() &&
          data->targetPageId != -1 && data->dailyChallengeSeed != 0) {
        const auto dailyStartedAt = std::chrono::steady_clock::now();
        const auto start = data->pathSystem->FetchDailyStartPage(
            data->targetPageId, data->dailyChallengeSeed, &dailyStartHops);
        data->startPage = start.first;
        LOG_INFO("LoadingScene",
                 "AsyncLoad daily start selected: title='{}' hops={} elapsed={}ms",
                 data->startPage, dailyStartHops, ElapsedMs(dailyStartedAt));
      }
      if (data->startPage.empty()) {
        // 選べなければ従来どおりランダム(この場合は全員同じにはならない)。
        data->startPage = wikiClient.FetchRandomPageTitle();
        LOG_WARN("LoadingScene",
                 "AsyncLoad daily start fallback to random: title='{}'",
                 data->startPage);
      }
    }

    if (overrideTargetPage.empty() && data->startPage == data->targetPage &&
        data->isDailyChallenge) {
      data->startPage = wikiClient.FetchRandomPageTitle();
      LOG_INFO("LoadingScene",
               "AsyncLoad daily duplicate start replaced: title='{}'",
               data->startPage);
    } else if (overrideTargetPage.empty() &&
               data->startPage == data->targetPage) {
      const auto retryStartedAt = std::chrono::steady_clock::now();
      data->targetPage = wikiClient.FetchTargetPageTitle();
      data->targetPageId = -1;
      resolveTargetPageId();
      setProgress(0.82f);
      LOG_INFO("LoadingScene",
               "AsyncLoad duplicate target replaced: title='{}' elapsed={}ms",
               data->targetPage, ElapsedMs(retryStartedAt));
    }

    // 開始記事はここで確定する。記事の取得（HTML・画像）は時間がかかるので、
    // 以降のゴール再抽選・パー計算・サムネイル取得と並行して進める。
    //
    // 記事の画像ライセンス確認は応答が1秒を超えやすく、超えると次のAPI呼び出しまで
    // 5秒空けることになる。こちらの短い問い合わせ（パー計算・サムネイルの照会）が
    // 巻き込まれないよう、それらが済むまでライセンス確認を始めさせない。
    std::promise<void> quickLookupsDone;
    const std::shared_future<void> quickLookupsFuture =
        quickLookupsDone.get_future().share();
    std::future<PageDataAsyncResult> startPageFetch;
    // 途中で抜けても取得側が待ち続けないよう、スコープを出るときに必ず知らせる。
    // startPageFetch より後に宣言して、先に破棄されるようにする。
    struct LookupsDoneNotifier {
      std::promise<void> &promise;
      bool notified = false;
      void Notify() {
        if (!notified) {
          notified = true;
          promise.set_value();
        }
      }
      ~LookupsDoneNotifier() { Notify(); }
    } quickLookups{quickLookupsDone};
    if (!data->startPage.empty()) {
      LOG_INFO("LoadingScene", "AsyncLoad start page fetch launched: title='{}'",
               data->startPage);
      startPageFetch = std::async(
          std::launch::async, [page = data->startPage, quickLookupsFuture]() {
            return WikiPageDataFetcher::FetchLive(page, nullptr, {}, {},
                                                  quickLookupsFuture);
          });
    }

    // 最短1記事（または同一）のターゲットは自動選定時のみ再抽選します。
    if (dbLoaded && data->pathSystem && data->pathSystem->IsAvailable() &&
        !data->startPage.empty() && !data->targetPage.empty()) {
      constexpr int kStartPagePathCheckMaxDepth = 4;
      const int maxRetry = 5;
      const auto pathCheckStartedAt = std::chrono::steady_clock::now();
      for (int attempt = 0; attempt < maxRetry; ++attempt) {
        const auto attemptStartedAt = std::chrono::steady_clock::now();
        game::systems::ShortestPathResult pathResult;
        if (data->targetPageId != -1) {
          pathResult = data->pathSystem->FindShortestPath(
              data->startPage, data->targetPageId, kStartPagePathCheckMaxDepth);
        } else {
          pathResult = data->pathSystem->FindShortestPath(
              data->startPage, data->targetPage, kStartPagePathCheckMaxDepth);
        }

        if (!pathResult.success) {
          LOG_WARN("LoadingScene",
                   "Shortest path check failed (attempt {}): {} (start={}, "
                   "target={})",
                   attempt + 1, pathResult.errorMessage, data->startPage,
                   data->targetPage);
          break;
        }

        LOG_INFO("LoadingScene", "Shortest path to '{}' is {} hops from '{}'",
                 data->targetPage, pathResult.degrees, data->startPage);
        LOG_INFO("LoadingScene",
                 "AsyncLoad shortest path attempt={} elapsed={}ms total={}ms",
                 attempt + 1, ElapsedMs(attemptStartedAt),
                 ElapsedMs(pathCheckStartedAt));

        if (overrideIsUserOverride || data->isDailyChallenge ||
            pathResult.degrees > 1) {
          break;
        }

        auto newTarget = fetchPopularTarget(100);
        if (newTarget.first.empty()) {
          newTarget = fetchPopularTarget(50);
        }

        if (newTarget.first.empty()) {
          break; // 取得できなければ諦める
        }

        data->targetPage = newTarget.first;
        data->targetPageId = newTarget.second;
        resolveTargetPageId();
      }
      setProgress(0.9f);
      logStage("path-check-ready", 0.9f);
    }

    // ラウンドパーの元になる、最短経路でプレイする記事のコース長を見積もる。
    // 記事を読み込む前なので、コース奥行は記事長(バイト数)から近似する。
    if (dbLoaded && data->pathSystem && data->pathSystem->IsAvailable() &&
        !data->startPage.empty() && !data->targetPage.empty()) {
      constexpr int kParPathMaxDepth = 20;
      const auto parStartedAt = std::chrono::steady_clock::now();
      game::systems::ShortestPathResult parPath;
      if (data->targetPageId != -1) {
        parPath = data->pathSystem->FindShortestPath(
            data->startPage, data->targetPageId, kParPathMaxDepth, false);
      } else {
        parPath = data->pathSystem->FindShortestPath(
            data->startPage, data->targetPage, kParPathMaxDepth, false);
      }
      if (parPath.success && parPath.path.size() >= 2) {
        // 最後はゴール記事。ゴールはカップインした時点で終わるのでプレイしない。
        std::vector<std::string> playedArticles(parPath.path.begin(),
                                                parPath.path.end() - 1);
        std::vector<int> lengths;
        if (wikiClient.FetchPageLengths(playedArticles, lengths)) {
          float total = 0.0f;
          for (const int length : lengths) {
            total += game::utils::EstimateCourseDepthFromArticleBytes(length);
          }
          // 取得できなかった記事は最小のコースとして数える。
          if (lengths.size() < playedArticles.size()) {
            total += game::utils::kEstimatedCourseMinDepth *
                     static_cast<float>(playedArticles.size() - lengths.size());
          }
          data->parCourseLength = total;
          data->parArticleCount = static_cast<int>(playedArticles.size());
        }
        data->shortestHops = parPath.degrees;
      }
      LOG_INFO("LoadingScene",
               "AsyncLoad par course length={:.1f}m articles={} shortestHops={} "
               "elapsed={}ms",
               data->parCourseLength, data->parArticleCount, data->shortestHops,
               ElapsedMs(parStartedAt));
    }

    // 目的記事の代表サムネイルを取得（ゴール看板表示用）。
    // ゲーム1回につき最大1回だけ（URL取得1リクエスト＋画像DL1回）。
    if (!data->targetPage.empty()) {
      const auto thumbStartedAt = std::chrono::steady_clock::now();
      std::string thumbUrl = wikiClient.FetchPageThumbnail(data->targetPage, 256);
      // ここから先はAPIを使わないので、開始記事のライセンス確認を始めさせる
      quickLookups.Notify();
      if (!thumbUrl.empty()) {
        std::string thumbBytes = wikiClient.DownloadBinary(thumbUrl);
        if (!thumbBytes.empty()) {
          graphics::DecodeWikiImageFromMemory(
              thumbBytes, data->targetThumbnailPixelsBGRA,
              data->targetThumbnailWidth, data->targetThumbnailHeight);
        }
      }
      LOG_INFO("LoadingScene",
               "AsyncLoad target thumbnail: url={} decodedSize={}x{} elapsed={}ms",
               !thumbUrl.empty(), data->targetThumbnailWidth,
               data->targetThumbnailHeight, ElapsedMs(thumbStartedAt));
    }
    quickLookups.Notify();

    // 開始記事の取得を待つ。本編はこの内容をそのまま使うので、入った直後に通信を待たない。
    if (startPageFetch.valid()) {
      const auto waitStartedAt = std::chrono::steady_clock::now();
      auto page = startPageFetch.get();
      const bool usable = WikiPageDataFetcher::IsUsable(page);
      LOG_INFO("LoadingScene",
               "AsyncLoad start page ready: usable={} links={} images={} "
               "waited={}ms",
               usable, page.allLinks.size(), page.pendingImages.size(),
               ElapsedMs(waitStartedAt));
      if (usable) {
        data->preloadedStartPage =
            std::make_shared<PageDataAsyncResult>(std::move(page));
      }
      setProgress(0.97f);
      logStage("initial-page-ready", 0.97f);
    }

    setProgress(1.0f);
    LOG_INFO("LoadingScene",
             "AsyncLoad complete elapsed={}ms start='{}' target='{}' "
             "targetId={} startPagePreloaded={}",
             ElapsedMs(loadStartedAt), data->startPage, data->targetPage,
             data->targetPageId,
             static_cast<bool>(data->preloadedStartPage));
    return data;
  });
  LOG_INFO("LoadingScene", "Async load task launched at {} ms",
           ElapsedMs(enterStartedAt));

  // マウスカーソルを非表示
  ctx.input.SetMouseCursorVisible(false);

  // ゴルフボールメッシュをロード
  const auto ballMeshStartedAt = std::chrono::steady_clock::now();
  m_ballMeshHandle = ctx.resource.LoadMesh("Assets/models/golfball.glb");
  LOG_INFO("LoadingScene", "Ball mesh load requested in {} ms",
           ElapsedMs(ballMeshStartedAt));

  // カメラを作成
  m_cameraEntity = CreateEntity(ctx.world);
  auto &camTr = ctx.world.Add<components::Transform>(m_cameraEntity);
  camTr.position = {0.0f, 6.0f, -68.0f};
  camTr.rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // 軽く俯瞰させるための基準

  auto &cam = ctx.world.Add<components::Camera>(m_cameraEntity);
  cam.fov = DirectX::XM_PIDIV4; // 45度
  cam.nearZ = 0.1f;
  cam.farZ = 100.0f;
  cam.isMainCamera = true;

  // 床と壁を生成
  const auto boundaryStartedAt = std::chrono::steady_clock::now();
  CreateBoundaries(ctx);
  LOG_INFO("LoadingScene", "Boundary setup finished in {} ms",
           ElapsedMs(boundaryStartedAt));

  // UIスタイル構築
  m_primaryStyle = graphics::TextStyle::Title();
  m_primaryStyle.fontSize = 46.0f;
  m_primaryStyle.align = graphics::TextAlign::Center;
  m_primaryStyle.color = {0.96f, 0.98f, 1.0f, 1.0f};
  m_primaryStyle.outlineColor = {0.07f, 0.18f, 0.35f, 0.85f};
  m_primaryStyle.outlineWidth = 2.2f;
  m_primaryStyle.hasShadow = true;
  m_primaryStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.7f};
  m_primaryStyle.shadowOffsetX = 2.5f;
  m_primaryStyle.shadowOffsetY = 2.5f;

  m_progressStyle = graphics::TextStyle::ModernBlack();
  m_progressStyle.fontSize = 28.0f;
  m_progressStyle.align = graphics::TextAlign::Center;
  m_progressStyle.color = {0.1f, 0.45f, 0.6f, 1.0f};
  m_progressStyle.hasShadow = true;
  m_progressStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.6f};

  m_captionStyle = graphics::TextStyle::ModernBlack();
  m_captionStyle.fontFamily = "Kiwi Maru Medium"; // Tips は日本語文なので丸ゴシックにする
  m_captionStyle.fontSize = 20.0f;
  m_captionStyle.align = graphics::TextAlign::Center;
  m_captionStyle.color = {0.2f, 0.2f, 0.25f, 0.9f};
  m_captionStyle.hasShadow = true;
  m_captionStyle.shadowColor = {0.0f, 0.0f, 0.0f, 0.4f};

  // メインタイトル
  m_textEntity = CreateEntity(ctx.world);
  auto &titleText = ctx.world.Add<components::UIText>(m_textEntity);
  titleText.text = L"WIKI GOLF LOADING";
  titleText.x = 0.0f;
  titleText.y = 110.0f;
  titleText.width = 1280.0f;
  titleText.style = m_primaryStyle;
  titleText.visible = true;
  titleText.layer = 12;

  // 進行状況
  m_progressTextEntity = CreateEntity(ctx.world);
  auto &progressText = ctx.world.Add<components::UIText>(m_progressTextEntity);
  progressText.text = L"0%";
  progressText.x = 0.0f;
  progressText.y = 170.0f;
  progressText.width = 1280.0f;
  progressText.style = m_progressStyle;
  progressText.visible = true;
  progressText.layer = 12;

  // キャプション（ tips 巡回用 ）
  m_captionTextEntity = CreateEntity(ctx.world);
  auto &caption = ctx.world.Add<components::UIText>(m_captionTextEntity);
  caption.text = L"芝目をスキャン中...";
  caption.x = 0.0f;
  caption.y = 210.0f;
  caption.width = 1280.0f;
  caption.style = m_captionStyle;
  caption.visible = true;
  caption.layer = 11;

  // 最初のボールは即スポーンさせて動きを見せる
  SpawnBall(ctx);

  LOG_INFO("LoadingScene", "OnEnter complete ({} ms)",
           ElapsedMs(enterStartedAt));
}

/**
 * @brief 本編プレイで実際に参照されるテクスチャ/SEアセットを列挙し、
 *        1フレーム1件ロード用のタスクキューを構築します。
 * @details ここで作るのはロードタスクの「予約」のみで、実際のWIC/MFの
 *          同期IOはOnUpdateで1フレームに1件ずつ実行される。
*/

} // namespace game::scenes

#include "src/audio/AudioSystem.h"
#include "src/core/DisplaySettings.h"
#include "src/core/GameContext.h"
#include "src/core/Input.h"
#include "src/core/Logger.h"
#include "src/core/SceneManager.h"
#include "src/ecs/World.h"
#include "src/game/scenes/TitleScene.h"
#include "src/game/scenes/WikiGolfScene.h"
#include "src/game/components/Camera.h"
#include "src/game/systems/PostProcessSystem.h"
#include "src/game/systems/RenderSystem.h"
#include "src/game/systems/SkyboxRenderSystem.h"
#include "src/game/systems/UIBarGaugeRenderSystem.h" // 追加
#include "src/game/systems/UIButtonRenderSystem.h"
#include "src/game/systems/UIButtonSystem.h"
#include "src/game/systems/UIImageRenderSystem.h"
#include "src/game/systems/UIRenderSystem.h"
#include "src/game/systems/WikiShortestPath.h"
#include "src/graphics/GraphicsDevice.h"
#include "src/core/Profiler.h"
#include "src/graphics/TextRenderer.h"
#include "src/resources/ResourceManager.h"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

// グローバル入力ポインタ（WndProc用）
core::Input *g_Input = nullptr;
// グローバルオーディオポインタ（WndProc用。×閉じるで即座に音を止めるために使う）
game::systems::AudioSystem *g_Audio = nullptr;
// グローバルグラフィックス/テキストレンダラポインタ（WndProc用。WM_SIZEでの追従に使う）
graphics::GraphicsDevice *g_Graphics = nullptr;
graphics::TextRenderer *g_TextRenderer = nullptr;

// ウィンドウプロシージャ
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  if (g_Input) {
    g_Input->ProcessMessage(message, wParam, lParam);
  }

  switch (message) {
  case WM_LBUTTONDOWN:
    // LOG_DEBUG("WndProc", "WM_LBUTTONDOWN");
    break;
  case WM_RBUTTONDOWN:
    // LOG_DEBUG("WndProc", "WM_RBUTTONDOWN");
    break;
  case WM_KEYDOWN:
    // LOG_DEBUG("WndProc", "WM_KEYDOWN: {}", wParam);
    break;
  case WM_SIZE: {
    // 最小化時はクライアントサイズが0x0になるため無視する
    if (wParam == SIZE_MINIMIZED) {
      break;
    }
    const UINT newWidth = LOWORD(lParam);
    const UINT newHeight = HIWORD(lParam);
    if (newWidth == 0 || newHeight == 0) {
      break;
    }
    // graphics/textRenderer/inputの初期化前（ウィンドウ作成直後）に届くWM_SIZEは無視する
    if (g_Graphics && (newWidth != g_Graphics->GetWidth() ||
                       newHeight != g_Graphics->GetHeight())) {
      // D2Dのバックバッファ参照を先に外さないとResizeBuffersが失敗する
      if (g_TextRenderer) {
        g_TextRenderer->ReleaseTargetForResize();
      }
      if (g_Graphics->Resize(newWidth, newHeight)) {
        if (g_TextRenderer) {
          g_TextRenderer->RecreateTargetAfterResize();
        }
        if (g_Input) {
          g_Input->SetResolution(static_cast<int>(newWidth),
                                static_cast<int>(newHeight));
        }
      } else if (g_TextRenderer) {
        // Resize失敗時も、旧バックバッファへの参照を持ち続けないよう最低限復旧を試みる
        g_TextRenderer->RecreateTargetAfterResize();
      }
    }
    break;
  }
  case WM_CLOSE:
    // Profiler::Shutdown()の同期CSV書き出しで実際のプロセス終了までは
    // 数十秒かかることがあるが、ユーザーには即座に閉じたと感じてもらうため
    // ここで先に音を止め、実行中のWikipedia経路計算(SQLite)を中断要求し、
    // ウィンドウを隠してしまう。残りの重い後片付けは見えないところで続行する。
    game::systems::WikiShortestPath::RequestCancelAll();
    if (g_Audio) {
      g_Audio->Shutdown();
    }
    ShowWindow(hWnd, SW_HIDE);
    DestroyWindow(hWnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {

  wchar_t modulePathBuffer[32768] = {};
  const DWORD modulePathLength =
      GetModuleFileNameW(nullptr, modulePathBuffer,
                         static_cast<DWORD>(std::size(modulePathBuffer)));
  std::filesystem::path executableDirectory;
  std::error_code workingDirectoryError;
  if (modulePathLength > 0 &&
      modulePathLength < static_cast<DWORD>(std::size(modulePathBuffer))) {
    executableDirectory =
        std::filesystem::path(modulePathBuffer).parent_path();
    std::filesystem::current_path(executableDirectory, workingDirectoryError);
  } else {
    workingDirectoryError =
        std::error_code(static_cast<int>(GetLastError()), std::system_category());
  }

  // 表示設定（ウィンドウモード・解像度）の読み込み。
  // ウィンドウ作成前なので、この時点ではファイルの値のみを保持しウィンドウには反映しない。
  core::DisplaySettings displaySettings;
  displaySettings.LoadFromFile();
  const int initialWidth = displaySettings.GetData().windowedWidth;
  const int initialHeight = displaySettings.GetData().windowedHeight;

  // ウィンドウクラス登録
  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"DX_GAME_WINDOW";
  RegisterClassEx(&wc);

  // ウィンドウサイズを計算（保存済み設定のクライアント領域を確保するため）
  // ボーダーレスモードだった場合も、まずは通常ウィンドウとして作成し、
  // 各システム初期化完了後にdisplaySettings.ApplyToWindow()で切り替える
  // （WM_SIZEハンドラがg_Graphics等に依存しているため）。
  // kWindowedStyleはリサイズ枠・最大化ボタンを持たない
  // （マウスによるウィンドウサイズ変更を禁止するため）。
  RECT rc = {0, 0, initialWidth, initialHeight};
  AdjustWindowRect(&rc, core::DisplaySettings::kWindowedStyle, FALSE);

  // ウィンドウ作成
  HWND hWnd =
      CreateWindowEx(0, L"DX_GAME_WINDOW", L"WikiGolf",
                     core::DisplaySettings::kWindowedStyle, CW_USEDEFAULT,
                     CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                     nullptr, nullptr, hInstance, nullptr);

  if (!hWnd) {
    return -1;
  }

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  // COM初期化
  HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hrCom)) {
    MessageBox(nullptr, L"COM Initialization Failed", L"Error", MB_OK);
    return -1;
  }

  // FPS上限のSleep精度を上げる（既定の約15.6msだとFPS上限が大きくブレるため）
  timeBeginPeriod(1);

  // システム初期化（保存済みの利用GPU設定があれば優先的に使う。GPU切り替えは
  // デバイス再生成が必要なため、設定変更自体は次回起動時に反映される）
  graphics::GraphicsDevice graphics;
  if (!graphics.Initialize(hWnd, initialWidth, initialHeight,
                           displaySettings.GetGpuAdapterNameWide())) {
    return -1;
  }

  resources::ResourceManager resource(graphics);
  ecs::World world;
  core::Input input;
  input.Initialize();
  input.SetResolution(initialWidth, initialHeight);
  g_Input = &input;

  // ログシステム初期化
  core::Logger::Instance().Initialize("game_startup.log");
  if (workingDirectoryError) {
    LOG_WARN("Main", "Failed to set executable working directory: {}",
             workingDirectoryError.message());
  } else {
    LOG_INFO("Main", "Working directory fixed to executable location: '{}'",
             executableDirectory.string());
  }
  LOG_INFO("Main",
           "Graphics device: Driver={} Adapter='{}' DedicatedVRAM={:.0f}MB",
           graphics.GetDriverType() == D3D_DRIVER_TYPE_HARDWARE ? "HARDWARE"
                                                                : "WARP",
           graphics.GetAdapterName(),
           static_cast<double>(graphics.GetDedicatedVideoMemoryBytes()) /
               (1024.0 * 1024.0));
  core::Profiler::Instance().Initialize("profiling");

  graphics::TextRenderer textRenderer;
  if (!textRenderer.Initialize(graphics.GetSwapChain())) {
    LOG_ERROR("Main", "TextRenderer Init failed.");
    return -1;
  }

  // WndProcのWM_SIZEハンドラが追従処理を行えるようにする
  g_Graphics = &graphics;
  g_TextRenderer = &textRenderer;

  // ウィンドウハンドル/GraphicsDeviceを登録し、読み込み済みのRender Scale/MSAA/FXAA/
  // VSyncをgraphicsへ反映する。保存済みモードがボーダーレス/フルスクリーンなら
  // 今ここで切り替える。ここまでにgraphics/textRenderer/inputが揃っているため、
  // 発生するWM_SIZEでスワップチェーン等が正しく追従する。
  displaySettings.Initialize(hWnd, &graphics);
  if (displaySettings.GetData().mode == core::WindowMode::Borderless ||
      displaySettings.GetData().mode == core::WindowMode::Fullscreen) {
    displaySettings.ApplyToWindow();
  }

  // オーディオシステム初期化
  game::systems::AudioSystem audioSystem;
  if (!audioSystem.Initialize()) {
    LOG_ERROR("Main", "AudioSystem Init failed. Running without audio.");
    // 続行可能（nullptrチェックを入れる想定）
  }
  g_Audio = &audioSystem;

  // ポストプロセス（霧/色調補正/ビネット/ブルーム）パラメータ計算。
  // 記事のスカイボックステーマに応じた環境プリセット(WikiPageLoader)が
  // UpdateFromEnvironment()経由でここへ反映される。
  game::systems::PostProcessSystem postProcessSystem;
  postProcessSystem.ResetToDefaults();
  postProcessSystem.SetBloom(0.18f, 0.75f, 1.2f); // 常時ごく控えめなハイライトの発光にじみ

  // ゲームコンテキスト
  core::GameContext ctx(resource, world, graphics, input);
  ctx.audio = &audioSystem;
  ctx.textRenderer = &textRenderer;
  ctx.displaySettings = &displaySettings;
  ctx.postProcess = &postProcessSystem;

  // 同梱フォントを登録し、ゲーム中HUDの文字描画を環境依存にしない。山内陽
  // 用途別に使い分ける（TextStyle.h の各プリセット参照）:
  //   Mamelon 5 Hi        - 見出し・特別演出用の装飾フォント
  //   Barlow Condensed *  - ラベル/本文用の引き締まったサンセリフ（UIの主力）
  //   Share Tech Mono     - 風速/距離/パワー等、数値読み取り用のデジタル等幅
  //   Kiwi Maru Medium    - 記事名・クラブ名など日本語文章用の丸ゴシック
  // 登録名は実際のフォント内部ファミリー名と一致させる必要がある
  // （一致しないと CreateTextFormat が見つけられず既定フォントへ
  // 静かにフォールバックしてしまう）。
  struct BundledFont { const char* family; const char* path; };
  const BundledFont kBundledFonts[] = {
      {"Mamelon 5 Hi",             "Assets/Fonts/Mamelon-5-Hi-Regular.otf"},
      {"Barlow Condensed",         "Assets/Fonts/Barlow_Condensed/BarlowCondensed-Regular.ttf"},
      {"Barlow Condensed Medium",  "Assets/Fonts/Barlow_Condensed/BarlowCondensed-Medium.ttf"},
      {"Barlow Condensed SemiBold","Assets/Fonts/Barlow_Condensed/BarlowCondensed-SemiBold.ttf"},
      {"Barlow Condensed Black",   "Assets/Fonts/Barlow_Condensed/BarlowCondensed-Black.ttf"},
      {"Kiwi Maru Medium",         "Assets/Fonts/Kiwi_Maru/KiwiMaru-Medium.ttf"},
      {"Share Tech Mono",          "Assets/Fonts/Share_Tech_Mono/ShareTechMono-Regular.ttf"},
  };
  for (const auto& font : kBundledFonts) {
    if (!textRenderer.LoadFont(font.family, font.path)) {
      LOG_WARN("Main", "Bundled font load failed: {} ({}). Falling back to system font.",
               font.family, font.path);
    }
  }

  // システムインスタンス
  game::systems::UIButtonSystem uiButtonSystem;

  // UIボタンクリック時の処理（汎用）
  uiButtonSystem.SetClickCallback([&](const std::string &action) {
    // シーン側でポーリングしているのでここではログ出力程度にしておく
    // LOG_INFO("UI", "Button clicked: {}", action);
  });

  game::systems::UIRenderSystem uiRenderSystem(textRenderer);
  game::systems::UIButtonRenderSystem uiButtonRenderSystem(textRenderer);
  game::systems::UIImageRenderSystem uiImageRenderSystem(textRenderer);
  game::systems::UIBarGaugeRenderSystem uiBarGaugeRenderSystem; // 追加

  // シーンマネージャ初期化
  core::SceneManager sceneManager;
  ctx.sceneManager = &sceneManager;
  sceneManager.ChangeScene(std::make_unique<game::scenes::TitleScene>());

  // メインループ
  MSG msg = {0};
  auto lastTime = std::chrono::high_resolution_clock::now();
  float fpsDisplaySmoothed = 0.0f; // 画面表示用の平滑化FPS（設定画面のFPS表示ON時）

  while (msg.message != WM_QUIT && !ctx.shouldClose) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      // 時間計測
      auto currentTime = std::chrono::high_resolution_clock::now();
      float dt = std::chrono::duration<float>(currentTime - lastTime).count();
      lastTime = currentTime;
      ctx.dt = dt;
      ctx.time += dt;

      // 表示用FPSを指数移動平均で平滑化（瞬間値のちらつきを抑える）
      if (dt > 0.0f) {
        const float instantFps = 1.0f / dt;
        fpsDisplaySmoothed = (fpsDisplaySmoothed <= 0.0f)
                                 ? instantFps
                                 : fpsDisplaySmoothed * 0.9f + instantFps * 0.1f;
      }

      const char *sceneName = sceneManager.Current()
                                  ? sceneManager.Current()->GetName()
                                  : "NoScene";
      auto &profiler = core::Profiler::Instance();
      const uint64_t profileFrame =
          profiler.BeginFrame(sceneName, world.GetEntityCount());
      profiler.SetCounter("Frame.DeltaSeconds", dt);
      profiler.SetCounter("GPU.DriverIsWarp",
                          graphics.GetDriverType() == D3D_DRIVER_TYPE_WARP ? 1.0
                                                                          : 0.0);
      profiler.SetCounter(
          "GPU.DedicatedVideoMemoryMB",
          static_cast<double>(graphics.GetDedicatedVideoMemoryBytes()) /
              (1024.0 * 1024.0));

      {
          PROFILE_SCOPE("LogicUpdate");
          // UI更新 (Logic)
          {
              PROFILE_SCOPE("Logic.UIButtonSystem");
              uiButtonSystem(ctx);
          }

          // シーン更新 (Game Logic + Physics)
          {
              PROFILE_SCOPE("Logic.SceneUpdate");
              sceneManager.Update(ctx);
          }

          // オーディオ更新
          {
              PROFILE_SCOPE("Logic.AudioSystem");
              audioSystem.Update(ctx);
          }
      }

      {
          PROFILE_SCOPE("Render_Total");
          
          {
              PROFILE_SCOPE("BeginFrame");
              graphics.BeginFrame(profileFrame);
          }

          {
              PROFILE_SCOPE("Render.Offscreen");
              graphics::ScopedGpuTimer gpuTimer(graphics, "GPU.Minimap");
              sceneManager.RenderOffscreen(ctx);
          }

          {
              PROFILE_SCOPE("Render_3D");
              {
                  PROFILE_SCOPE("Render.Skybox");
                  graphics::ScopedGpuTimer gpuTimer(graphics, "GPU.Skybox");
                  game::systems::SkyboxRenderSystem(ctx);
              }
              {
                  PROFILE_SCOPE("Render.Meshes");
                  graphics::ScopedGpuTimer gpuTimer(graphics, "GPU.Meshes");
                  game::systems::RenderSystem(ctx);
              }
          }

          {
              // ポストプロセス（霧/色調補正/ビネット/ブルーム）の定数を、現在の
              // メインカメラのnear/far（霧の深度線形化に使用）と合わせて渡す。
              PROFILE_SCOPE("Render.PostProcessParams");
              float camNear = 0.01f;
              float camFar = 1000.0f;
              bool foundMainCamera = false;
              world.Query<game::components::Camera>().Each(
                  [&](ecs::Entity, game::components::Camera &c) {
                      if (c.isMainCamera || !foundMainCamera) {
                          camNear = c.nearZ;
                          camFar = c.farZ;
                          foundMainCamera = true;
                      }
                  });

              const auto &pc = postProcessSystem.GetConstants();
              graphics::PostProcessParams pp;
              pp.fogColor = pc.fogColor;
              pp.fogParams = pc.fogParams;
              pp.colorTint = pc.colorTint;
              pp.colorParams = pc.colorParams;
              pp.vignetteParams = pc.vignetteParams;
              pp.timeParams = {ctx.time, pc.timeParams.y, pc.timeParams.z,
                               pc.timeParams.w};
              pp.depthParams = {camNear, camFar, 0.0f, 0.0f}; // zはGraphicsDevice側で決定

              // マップビュー(俯瞰トップビュー)中は、高高度からの距離が
              // フォグ終了距離をすぐ超えて画面全体が白く覆われてしまうため、
              // 距離フォグを無効化する(density成分のみ0にし、他の色調補正等は維持)。
              if (auto *golfState =
                      world.GetGlobal<game::components::GolfGameState>()) {
                  if (golfState->isMapView) {
                      pp.fogColor.w = 0.0f;
                  }
              }

              graphics.SetPostProcessParams(pp);
          }

          {
              // 内部描画解像度(Render Scale)のシーンをポストプロセス→MSAA解決/FXAA/
              // アップスケールした上でバックバッファへ転送する。以降のUI/ScreenFadeは
              // このバックバッファへ直接描画される。
              PROFILE_SCOPE("Render.ResolveScene");
              graphics::ScopedGpuTimer gpuTimer(graphics, "GPU.ResolveScene");
              graphics.ResolveSceneToBackbuffer();
          }

          {
              PROFILE_SCOPE("Render_UI");
              {
                  PROFILE_SCOPE("Render.UI2D");
                  graphics::ScopedGpuTimer gpuTimer(graphics, "GPU.UI2D");
                  textRenderer.BeginDraw();
                  {
                      PROFILE_SCOPE("Render.UIText");
                      uiRenderSystem(ctx);
                  }
                  {
                      PROFILE_SCOPE("Render.UIImage");
                      uiImageRenderSystem(ctx);
                  }
                  {
                      PROFILE_SCOPE("Render.UIBarGauge");
                      uiBarGaugeRenderSystem(ctx); // 追加
                  }
                  {
                      PROFILE_SCOPE("Render.UIButton");
                      uiButtonRenderSystem(ctx);
                  }
                  if (ctx.displaySettings && ctx.displaySettings->GetData().showFps) {
                      PROFILE_SCOPE("Render.FpsOverlay");
                      const int fpsRounded = static_cast<int>(fpsDisplaySmoothed + 0.5f);
                      graphics::TextStyle fpsStyle = graphics::TextStyle::FPS();
                      fpsStyle.align = graphics::TextAlign::Right;
                      textRenderer.RenderText(L"FPS: " + std::to_wstring(fpsRounded),
                                              D2D1::RectF(960.0f, 8.0f, 1270.0f, 44.0f),
                                              fpsStyle);
                  }
                  textRenderer.EndDraw();
              }
              {
                  PROFILE_SCOPE("Render.SceneOverlay");
                  graphics::ScopedGpuTimer gpuTimer(graphics, "GPU.SceneOverlay");
                  sceneManager.Render(ctx);
              }
          }

          {
              PROFILE_SCOPE("EndFrame");
              graphics.EndFrame();
          }
      }

      // 入力状態更新（次フレームのためにフラグクリア）
      // Logic処理の後、描画の後に行う
      {
          PROFILE_SCOPE("Input.EndFrame");
          input.Update();
      }

      profiler.EndFrame();
      for (auto &sample : graphics.ConsumeGpuProfileSamples()) {
        profiler.SubmitGpuFrame(std::move(sample));
      }

      // FPS上限（0 = 無制限）。VSync ONの場合はPresentの垂直同期待ちで既に
      // 概ねフレームレートが制御されるが、無制限/高リフレッシュレート環境でも
      // 上限を守れるようここで明示的にスリープする。
      {
          PROFILE_SCOPE("Frame.FpsLimitSleep");
          const int fpsLimit = ctx.displaySettings ? ctx.displaySettings->GetData().fpsLimit : 0;
          if (fpsLimit > 0) {
              const double targetSeconds = 1.0 / static_cast<double>(fpsLimit);
              const auto frameEnd = std::chrono::high_resolution_clock::now();
              const double elapsedSeconds =
                  std::chrono::duration<double>(frameEnd - currentTime).count();
              if (elapsedSeconds < targetSeconds) {
                  std::this_thread::sleep_for(
                      std::chrono::duration<double>(targetSeconds - elapsedSeconds));
              }
          }
      }
    }
  }

  // タイトル画面の「終了」は WM_CLOSE を経由せず shouldClose で抜けるため、
  // 重い後片付けより先に画面と音を止め、経路計算へ中断を要求する。
  game::systems::WikiShortestPath::RequestCancelAll();
  ShowWindow(hWnd, SW_HIDE);
  audioSystem.Shutdown();

  core::Profiler::Instance().Shutdown();
  textRenderer.Shutdown();
  graphics.Shutdown();
  core::Logger::Instance().Shutdown();

  timeEndPeriod(1);
  CoUninitialize();

  return (int)msg.wParam;
}

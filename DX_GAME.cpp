#include "src/audio/AudioSystem.h"
#include "src/core/GameContext.h"
#include "src/core/Input.h"
#include "src/core/Logger.h"
#include "src/core/SceneManager.h"
#include "src/ecs/World.h"
#include "src/game/scenes/TitleScene.h"
#include "src/game/scenes/WikiGolfScene.h"
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

// グローバル入力ポインタ（WndProc用）
core::Input *g_Input = nullptr;
// グローバルオーディオポインタ（WndProc用。×閉じるで即座に音を止めるために使う）
game::systems::AudioSystem *g_Audio = nullptr;

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

  // ウィンドウクラス登録
  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"DX_GAME_WINDOW";
  RegisterClassEx(&wc);

  // ウィンドウサイズを計算（クライアント領域を1920x1080確保するため）
  RECT rc = {0, 0, 1920, 1080};
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

  // ウィンドウ作成
  HWND hWnd =
      CreateWindowEx(0, L"DX_GAME_WINDOW", L"WikiGolf", WS_OVERLAPPEDWINDOW,
                     CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                     rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);

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

  // システム初期化
  graphics::GraphicsDevice graphics;
  if (!graphics.Initialize(hWnd, 1920, 1080)) {
    return -1;
  }

  resources::ResourceManager resource(graphics);
  ecs::World world;
  core::Input input;
  input.Initialize();
  input.SetResolution(1920, 1080);
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

  // オーディオシステム初期化
  game::systems::AudioSystem audioSystem;
  if (!audioSystem.Initialize()) {
    LOG_ERROR("Main", "AudioSystem Init failed. Running without audio.");
    // 続行可能（nullptrチェックを入れる想定）
  }
  g_Audio = &audioSystem;

  // ゲームコンテキスト
  core::GameContext ctx(resource, world, graphics, input);
  ctx.audio = &audioSystem;
  ctx.textRenderer = &textRenderer;

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
    }
  }

  core::Profiler::Instance().Shutdown();
  textRenderer.Shutdown();
  graphics.Shutdown();
  audioSystem.Shutdown();
  core::Logger::Instance().Shutdown();

  CoUninitialize();

  return (int)msg.wParam;
}

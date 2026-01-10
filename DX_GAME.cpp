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
#include "src/game/systems/UIButtonRenderSystem.h"
#include "src/game/systems/UIButtonSystem.h"
#include "src/game/systems/UIImageRenderSystem.h"
#include "src/game/systems/UIRenderSystem.h"
#include "src/graphics/GraphicsDevice.h"
#include "src/graphics/TextRenderer.h"
#include "src/resources/ResourceManager.h"
#include <Windows.h>
#include <chrono>

// グローバル入力ポインタ（WndProc用）
core::Input *g_Input = nullptr;

// ウィンドウプロシージャ
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  if (g_Input) {
    g_Input->ProcessMessage(message, wParam, lParam);
  }

  switch (message) {
  case WM_LBUTTONDOWN:
    LOG_DEBUG("WndProc", "WM_LBUTTONDOWN");
    break;
  case WM_RBUTTONDOWN:
    LOG_DEBUG("WndProc", "WM_RBUTTONDOWN");
    break;
  case WM_KEYDOWN:
    LOG_DEBUG("WndProc", "WM_KEYDOWN: {}", wParam);
    break;
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

  // ウィンドウクラス登録
  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"DX_GAME_WINDOW";
  RegisterClassEx(&wc);

  // ウィンドウサイズを計算（クライアント領域を1280x720確保するため）
  RECT rc = {0, 0, 1280, 720};
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
  if (!graphics.Initialize(hWnd, 1280, 720)) {
    return -1;
  }

  resources::ResourceManager resource(graphics);
  ecs::World world;
  core::Input input;
  input.Initialize();
  g_Input = &input;

  // ログシステム初期化
  core::Logger::Instance().Initialize("game_startup.log");

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

  // ゲームコンテキスト
  core::GameContext ctx(resource, world, graphics, input);
  ctx.audio = &audioSystem;

  // フォントロード（必要なら）
  // textRenderer.LoadFont("Default", "C:\\Windows\\Fonts\\msgothic.ttc");

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

      // シーン更新 (Game Logic + Physics)
      sceneManager.Update(ctx);

      // オーディオ更新
      audioSystem.Update(ctx);

      // UI更新 (Logic)
      uiButtonSystem(ctx);

      // 描画開始
      graphics.BeginFrame();

      // スカイボックス描画 (背景)
      game::systems::SkyboxRenderSystem(ctx);

      // 3Dシーン描画
      game::systems::RenderSystem(ctx);

      // UI描画
      uiImageRenderSystem(ctx);
      uiButtonRenderSystem(ctx);
      uiRenderSystem(ctx);

      // 描画終了
      graphics.EndFrame();

      // 入力状態更新（次フレームのためにフラグクリア）
      // Logic処理の後、描画の後に行う
      input.Update();
    }
  }

  textRenderer.Shutdown();
  graphics.Shutdown();
  core::Logger::Instance().Shutdown();

  return (int)msg.wParam;
}

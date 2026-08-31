#pragma once
/**
 * @file DisplaySettings.h
 * @brief 表示・画質設定（ウィンドウモード/解像度/Render Scale/VSync/FPS上限/
 *        FXAA/MSAA/TAA）の管理と永続化
 */

#include <Windows.h>
#include <string>
#include <utility>
#include <vector>

namespace graphics {
class GraphicsDevice;
} // namespace graphics

namespace core {

/// @brief ウィンドウ表示モード
enum class WindowMode {
  Windowed,   ///< 通常のタイトルバー付きウィンドウ
  Borderless, ///< 枠なし・モニタ全体を覆うウィンドウ（疑似フルスクリーン）
  Fullscreen, ///< DXGI排他的フルスクリーン
};

/// @brief 永続化される表示・画質設定の実体
struct DisplaySettingsData {
  WindowMode mode = WindowMode::Windowed;
  // Windowed/Fullscreen時に使う解像度（設定ファイルに保存される値）。
  // Borderless中はモニタ解像度で上書き表示されるため、ここは変化しない
  // （他モードへ戻したときに復元するための値として保持し続ける）。
  int windowedWidth = 1920;
  int windowedHeight = 1080;

  float renderScale = 1.0f; ///< 内部描画解像度の倍率 (0.5〜1.0)
  bool vsync = true;        ///< Present同期
  int fpsLimit = 60;        ///< フレームレート上限。0 = 無制限
  bool fxaaEnabled = false; ///< 最終画面へのFXAA適用
  int msaaSamples = 1;      ///< 1(オフ)/2/4/8
  // TAAは現状未実装（前フレーム情報・モーションベクトル等の基盤が必要なため）。
  // 設定値としては保持・永続化するが、描画には反映されない。
  bool taaEnabled = false;

  bool showFps = false; ///< 画面右上へのFPS表示
};

/// @brief 表示・画質設定の読み書きとウィンドウ/GraphicsDeviceへの反映を行う
/// @details ウィンドウサイズ変更に伴う実際のスワップチェーン/D2Dターゲット/
///          入力座標系の追従はWndProcのWM_SIZEハンドラ側で行う。このクラスは
///          ウィンドウスタイル変更・排他フルスクリーン切り替え・GraphicsDeviceへの
///          画質設定反映までを担当する。
class DisplaySettings {
public:
  static constexpr const char *kDefaultPath = "settings.ini";

  /// @brief マウスでのリサイズ・最大化を禁止した、通常ウィンドウ用のスタイル。
  ///        WS_OVERLAPPEDWINDOWからWS_THICKFRAME(リサイズ枠)とWS_MAXIMIZEBOX
  ///        (最大化ボタン/タイトルバーダブルクリックでの最大化)を除いたもの。
  static constexpr DWORD kWindowedStyle =
      WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

  /// @brief 設定ファイルから読み込む（ウィンドウ/GraphicsDeviceにはまだ反映しない）
  void LoadFromFile(const std::string &path = kDefaultPath);

  /// @brief 設定ファイルへ書き出す
  void SaveToFile(const std::string &path = kDefaultPath) const;

  /// @brief ウィンドウハンドルとGraphicsDeviceを登録し、以後の設定変更を反映できるようにする。
  ///        利用可能な解像度一覧の列挙、読み込み済み画質設定のGraphicsDeviceへの反映も行う。
  void Initialize(HWND hwnd, graphics::GraphicsDevice *graphicsDevice);

  /// @brief 現在の設定をウィンドウへ反映する（スタイル変更・SetWindowPos・排他フルスクリーン切替）
  /// @details サイズが変化する場合はWM_SIZEが発生し、GraphicsDevice/TextRenderer/
  ///          Inputの追従処理がWndProc側で走る。
  void ApplyToWindow();

  /// @brief ウィンドウモードを変更し、即座にウィンドウへ反映・保存する
  void SetWindowMode(WindowMode mode);
  /// @brief 列挙順（Windowed→Borderless→Fullscreen）で前後に移動する
  void CycleWindowMode(int direction);

  /// @brief Windowed/Fullscreen時の解像度を変更する。現在その状態であれば即座に反映する。
  void SetResolution(int width, int height);
  /// @brief 列挙済み解像度リストの中で現在値から前後に移動する（UIの矢印ボタン用）
  void CycleResolution(int direction);

  void SetRenderScale(float scale);
  void CycleRenderScale(int direction);

  void SetVSync(bool enabled);

  void SetFpsLimit(int fps); ///< 0 = 無制限
  void CycleFpsLimit(int direction);

  void SetFxaaEnabled(bool enabled);

  void SetMsaaSamples(int samples); ///< 1/2/4/8
  void CycleMsaa(int direction);

  /// @brief TAA有効/無効を保存する。※現状描画には反映されない（未実装）。
  void SetTaaEnabled(bool enabled);

  /// @brief 画面右上のFPS表示の有効/無効を設定する
  void SetShowFps(bool enabled);

  const DisplaySettingsData &GetData() const { return m_data; }
  const std::vector<std::pair<int, int>> &GetAvailableResolutions() const {
    return m_resolutions;
  }

  /// @brief 現在ウィンドウに実際に反映されている解像度。
  /// @details Windowed/Fullscreen時はGetData().windowedWidth/Heightと一致するが、
  ///          Borderless時はモニタの実解像度になる
  ///          （設定表示UIはこちらを使うこと）。
  int GetCurrentWidth() const { return m_currentWidth; }
  int GetCurrentHeight() const { return m_currentHeight; }

private:
  static std::vector<std::pair<int, int>> EnumerateResolutions();
  RECT GetMonitorRect(bool workAreaOnly) const;
  void RefreshCurrentResolution();
  void ApplyQualityToGraphics();

  HWND m_hwnd = nullptr;
  graphics::GraphicsDevice *m_graphics = nullptr;
  DisplaySettingsData m_data;
  std::vector<std::pair<int, int>> m_resolutions;
  int m_currentWidth = 0;
  int m_currentHeight = 0;
};

} // namespace core

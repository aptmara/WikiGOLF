#pragma once
/**
 * @file GraphicsDevice.h
 * @brief DirectX11デバイス・コンテキスト管理
*/

#include <DirectXMath.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

#include "../core/Profiler.h"
#include "DlssUpscaler.h"
#include "Shader.h"

namespace graphics {

using Microsoft::WRL::ComPtr;

/** @brief 画質設定（Render Scale/MSAA/FXAA/TAA）。SettingsSceneからDisplaySettings経由で渡される。 */
struct QualitySettings {
  float renderScale = 1.0f; /**< 内部描画解像度の倍率 (0.5〜1.0) */
  int msaaSamples = 1;      /**< 1(オフ)/2/4/8 */
  bool fxaaEnabled = false; /**< 最終画面へのFXAA適用 */
  bool taaEnabled = false;  /**< テンポラルAA（MSAA無効時のみ。描画解像度<100%ならTAAU） */
  bool dlssEnabled = false; /**< DLSS（MSAA無効かつ対応GPUのみ。非対応ならTAAで代替） */
};

/**
 * @brief ポストプロセス（霧/色調補正/ビネット/ブルーム）のGPU定数バッファ相当。
 * @details game::systems::PostProcessConstantsの6項目をそのままコピーし、
 *          描画側だけが知る深度線形化パラメータ(depthParams)を追加したもの。
 */
struct PostProcessParams {
  DirectX::XMFLOAT4 fogColor{0.7f, 0.75f, 0.8f, 0.0f};      // RGB + density
  DirectX::XMFLOAT4 fogParams{100.0f, 500.0f, 0.0f, 0.0f};  // start, end, -, -
  DirectX::XMFLOAT4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};      // RGB + brightness
  DirectX::XMFLOAT4 colorParams{1.0f, 1.0f, 0.0f, 0.0f};    // saturation, contrast, -, -
  DirectX::XMFLOAT4 vignetteParams{0.25f, 0.7f, 0.5f, 0.0f};// intensity, radius, softness, -
  DirectX::XMFLOAT4 timeParams{0.0f, 0.0f, 0.72f, 1.0f};    // time, bloomIntensity, bloomThreshold, bloomSpread
  // x=nearZ, y=farZ（霧の深度線形化用）, z=depth利用可否(1/0。MSAA時は常に0), w=未使用
  DirectX::XMFLOAT4 depthParams{0.01f, 1000.0f, 0.0f, 0.0f};
};

/** @brief 利用可能なGPUアダプタの情報（GPU選択UI用） */
struct AdapterInfo {
  std::wstring name;
  uint64_t dedicatedVideoMemoryBytes = 0;
};

/** @brief DirectX11グラフィックスデバイス */
class GraphicsDevice {
public:
  GraphicsDevice() = default;
  ~GraphicsDevice() = default;

  /**
   * @brief 利用可能な物理GPU（WARP/ソフトウェアアダプタを除く）を列挙する。
   *        デバイス作成前でも呼び出し可能（GPU選択UI用）。
   */
  static std::vector<AdapterInfo> EnumerateAdapters();

  // コピー禁止
  GraphicsDevice(const GraphicsDevice &) = delete;
  GraphicsDevice &operator=(const GraphicsDevice &) = delete;

  /**
   * @brief 初期化
   * @param hWnd ウィンドウハンドル
   * @param width ウィンドウ幅
   * @param height ウィンドウ高さ
   * @param preferredAdapterName 優先的に使用するGPUのアダプタ名（DXGI_ADAPTER_DESC1::
   *        Descriptionと一致するもの）。空、または一致するアダプタが無い/作成に
   *        失敗した場合は既定の自動選択（高性能優先）にフォールバックする。
   * @return 成功ならtrue
   */
  bool Initialize(HWND hWnd, uint32_t width, uint32_t height,
                  const std::wstring &preferredAdapterName = L"");

  /** @brief シャットダウン */
  void Shutdown();

  /**
   * @brief フレーム開始（GPU query開始・シーンレンダーターゲットのクリア）
   * @details 3D描画（Skybox/メッシュ）は内部描画解像度のオフスクリーンターゲットへ
   *          描画される。等倍のバックバッファへ直接描画するわけではない点に注意。
   */
  void BeginFrame(uint64_t profileFrameIndex, float r = 0.1f, float g = 0.1f,
                  float b = 0.2f,
                  float a = 1.0f);

  /**
   * @brief 3D描画（Skybox/メッシュ）が終わった直後に呼ぶ。
   * @details MSAA解決 → (FXAA) → 出力解像度へのアップスケールを行い、結果を
   *          実バックバッファへ書き込む。以降のUI(D2D)/ScreenFade等はこのバック
   *          バッファ上に直接描画される。
   */
  void ResolveSceneToBackbuffer();

  /** @brief フレーム終了（Present） */
  void EndFrame();

  /** @brief 現在のGPUフレーム内でtimestamp区間を開始します。 */
  void BeginGpuScope(std::string_view name);

  /** @brief 現在のGPUフレーム内でtimestamp区間を終了します。 */
  void EndGpuScope();

  /** @brief 非同期回収済みのGPU計測結果を取得します。 */
  std::vector<core::GpuFrameSample> ConsumeGpuProfileSamples();

  /**
   * @brief 数フレーム遅れで回収した直近のGPUフレーム時間（ミリ秒）。
   *        プロファイリング無効ビルドでも計測し、描画負荷の自動調整に使う。
   *        未計測なら0。
   */
  float GetLatestGpuFrameMs() const { return m_latestGpuFrameMs; }

  /** @brief ウィンドウ（バックバッファ）リサイズ */
  bool Resize(uint32_t width, uint32_t height);

  /** @brief Render Scale / MSAA / FXAA を変更し、内部レンダーターゲットを再生成する */
  void ApplyQualitySettings(const QualitySettings &settings);
  const QualitySettings &GetQualitySettings() const { return m_quality; }

  /**
   * @brief ポストプロセス（霧/色調補正/ビネット/ブルーム）パラメータを更新する。
   *        次回のResolveSceneToBackbuffer()から反映される。
   */
  void SetPostProcessParams(const PostProcessParams &params) {
    m_postProcessParams = params;
  }

  /** @brief 深度バッファがシェーダーから読める状態か（MSAA有効時はfalse） */
  bool IsDepthReadable() const { return m_depthSRV != nullptr; }

  /** @brief TAA（またはDLSS非対応時の代替TAA）が動作中か */
  bool IsTaaActive() const { return m_temporalMode == TemporalMode::Taa; }
  /** @brief DLSSが動作中か */
  bool IsDlssActive() const { return m_temporalMode == TemporalMode::Dlss; }
  /** @brief TAA/DLSSのどちらかが動作中か（ジッター・速度バッファを使う）*/
  bool IsTemporalActive() const { return m_temporalMode != TemporalMode::None; }
  /** @brief このPCでDLSSが使えるか（SDK組み込み・対応GPU・ドライバー）*/
  bool IsDlssSupported() const { return m_dlss.IsSupported(); }

  /**
   * @brief メインカメラの投影行列へ今フレームのサブピクセルジッターを加える。
   * @details 3Dシーン本体（RenderSystem/SkyboxRenderSystem）の描画用行列にだけ
   *          適用する。UI投影やレイキャスト、カリングには未ジッター行列を使うこと。
   *          TAA/DLSS無効時は何もしない。
   */
  void ApplyProjectionJitter(DirectX::XMMATRIX &projection) const;

  /**
   * @brief 今フレームのメインカメラのView*Projection（ジッター無し）を登録する。
   * @details 再投影と速度バッファに使う。登録されなかったフレームは履歴を破棄する。
   */
  void SetTemporalCameraViewProjection(const DirectX::XMMATRIX &viewProjection);

  /** @brief 速度バッファ出力用にシーンシェーダーへ渡す値（TemporalVelocity.hlsli）*/
  struct TemporalShaderConstants {
    DirectX::XMMATRIX prevViewProjection; /**< 前フレームのView*Projection（ジッター無し・未転置） */
    DirectX::XMFLOAT4 jitterUv;           /**< xy: ジッター(UV)、zw: 1/描画解像度 */
    bool velocityEnabled = false;         /**< 速度バッファへ書き込むか */
  };
  TemporalShaderConstants GetTemporalShaderConstants() const;

  /**
   * @brief シーンカラーに加えて速度バッファ(SV_Target1)を出力先へ追加する。
   * @details RenderSystemのメッシュ描画の間だけ有効にする（スカイボックス等の
   *          書き込まない描画は、クリア値=カメラ移動のみとして扱われる）。
   */
  void BindSceneVelocityTarget();
  /** @brief 出力先をシーンカラー1枚に戻す */
  void UnbindSceneVelocityTarget();

  /**
   * @brief テクスチャのミップLODバイアス。
   * @details 描画解像度より高い出力解像度へ復元する場合、出力解像度相当の
   *          細かさでテクスチャを読む（DLSS Programming Guide 3.5）。
   */
  float GetTextureMipBias() const;

  /** @brief VSync有効/無効を設定（Present時に反映） */
  void SetVSync(bool enabled) { m_vsyncEnabled = enabled; }
  bool GetVSync() const { return m_vsyncEnabled; }

  /**
   * @brief 排他的フルスクリーンの切り替え。
   * @param width/height 排他フルスクリーン時に使う解像度（enable=falseなら無視）
   * @return 成功ならtrue
   */
  bool SetFullscreenExclusive(bool enable, uint32_t width, uint32_t height);
  bool IsFullscreenExclusive() const { return m_isExclusiveFullscreen; }

  // アクセサ
  ID3D11Device *GetDevice() const { return m_device.Get(); }
  ID3D11DeviceContext *GetContext() const { return m_context.Get(); }
  IDXGISwapChain *GetSwapChain() const { return m_swapChain.Get(); }
  HRESULT GetDeviceRemovedReason() const {
    if (m_device) {
      return m_device->GetDeviceRemovedReason();
    }
    return E_FAIL;
  }
  D3D_DRIVER_TYPE GetDriverType() const { return m_driverType; }
  D3D_FEATURE_LEVEL GetFeatureLevel() const { return m_featureLevel; }
  const std::string &GetAdapterName() const { return m_adapterName; }
  uint64_t GetDedicatedVideoMemoryBytes() const {
    return m_dedicatedVideoMemoryBytes;
  }
  /**
   * @brief このプロセスのVRAM使用量と、OSが割り当てたVRAM予算を取得します。
   *        使用量が予算を超えるとWDDMが資源を退避し、GPU全体が数秒停滞する。
   * @return 取得できた場合true
   */
  bool QueryLocalVideoMemory(uint64_t &usageBytes,
                             uint64_t &budgetBytes) const;
  /** @brief 出力（バックバッファ）解像度 */
  uint32_t GetWidth() const { return m_width; }
  uint32_t GetHeight() const { return m_height; }
  /** @brief 内部描画解像度（Render Scale適用後。3D描画系はこちらを基準にする） */
  uint32_t GetRenderWidth() const { return m_renderWidth; }
  uint32_t GetRenderHeight() const { return m_renderHeight; }
  /** @brief アスペクト比。Render Scaleは縦横均等倍率のため出力解像度基準のままでよい */
  float GetAspectRatio() const {
    return static_cast<float>(m_width) / static_cast<float>(m_height);
  }

private:
  /** @brief スワップチェーンおよびD3D11デバイス・コンテキストを生成します。 */
  bool CreateSwapChainAndDevice(HWND hWnd);

  /** @brief バックバッファのレンダーターゲットビュー（RTV）を生成します。 */
  bool CreateRenderTargetView();

  /** @brief 深度ステンシルテクスチャおよびビュー（DSV）を生成します。 */
  bool CreateDepthStencilView();

  /** @brief シーン内部描画用の中間レンダーターゲットを生成します。 */
  bool CreateSceneRenderTargets();

  /** @brief ポストプロセス（FXAA、アップスケール等）用リソースを初期化します。 */
  bool InitializePostProcessResources();

  /** @brief シーン解像度に応じたビューポートを設定します。 */
  void SetupSceneViewport();

  /** @brief バックバッファ解像度に応じたビューポートを設定します。 */
  void SetupBackbufferViewport();

  /** @brief ラスタライザ・深度ステンシル・ブレンドステートの既定値を構築します。 */
  void SetupRenderState();

  /** @brief 使用中のGPUアダプタ情報を取得・記録します。 */
  void CaptureAdapterInfo();

  /** @brief GPUプロファイラ用のタイムスタンプクエリを初期化します。 */
  bool InitializeGpuProfilerQueries();

  /** @brief 完了したGPUタイムスタンプクエリの結果を回収・集計します。 */
  void ResolveGpuProfilerQueries();

  /** @brief 常時計測用のGPUフレームタイマーを初期化します。 */
  void InitializeGpuFrameTimer();
  /** @brief 完了したGPUフレームタイマーの結果を回収します。 */
  void ResolveGpuFrameTimer();
  /** @brief フルスクリーンパスで使う定数バッファの種類 */
  enum class FullscreenConstants { None, Fxaa, Upscale };
  void RunFullscreenPass(Shader &shader, ID3D11ShaderResourceView *srv,
                         ID3D11RenderTargetView *dstRTV, uint32_t dstWidth,
                         uint32_t dstHeight, FullscreenConstants constants);
  /** @brief 霧/色調補正/ビネット/ブルームのパス（シーンカラー+深度 → 出力） */
  void RunPostProcessPass(ID3D11ShaderResourceView *colorSRV,
                          ID3D11RenderTargetView *dstRTV, uint32_t dstWidth,
                          uint32_t dstHeight);

  // --- テンポラルAA（TAA/DLSS）: GraphicsDeviceTemporal.cpp ---
  enum class TemporalMode { None, Taa, Dlss };
  /** @brief 画質設定と対応状況から、描画解像度とテンポラル方式を決める */
  void DecideTemporalModeAndRenderSize();
  /** @brief 速度バッファ・履歴・DLSS出力など、テンポラル方式に必要な資源を作る */
  bool CreateTemporalTargets(const D3D11_TEXTURE2D_DESC &sceneColorDesc);
  void ReleaseTemporalTargets();
  /** @brief フレーム開始時のジッター決定と前フレーム行列の繰り越し */
  void BeginTemporalFrame();
  /** @brief 速度バッファ（物体の動き）と深度（カメラの動き）から完全な速度を作る */
  void RunVelocityResolvePass();
  /** @brief TAA/TAAU（出力解像度の履歴へ蓄積）。@return 出力解像度のSRV */
  ID3D11ShaderResourceView *RunTaaPass();
  /** @brief DLSS。失敗時はnullptr */
  ID3D11ShaderResourceView *RunDlssPass();
  /** @brief 描画・出力解像度を指定してフルスクリーン三角形を描く共通処理 */
  void DrawFullscreenTriangle(Shader &shader, ID3D11RenderTargetView *dstRTV,
                              uint32_t dstWidth, uint32_t dstHeight);

  struct GpuTimestampQueries {
    std::string name;
    ComPtr<ID3D11Query> start;
    ComPtr<ID3D11Query> end;
  };

  struct GpuFrameQueries {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> pipeline;
    ComPtr<ID3D11Query> frameStart;
    ComPtr<ID3D11Query> frameEnd;
    std::vector<GpuTimestampQueries> scopes;
    uint64_t frameIndex = 0;
    size_t usedScopeCount = 0;
    bool issued = false;
    bool pipelineIssued = false;
  };

private:
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_context;
  ComPtr<IDXGISwapChain> m_swapChain;
  ComPtr<ID3D11RenderTargetView> m_renderTargetView; /**< 実バックバッファ（出力解像度） */
  ComPtr<ID3D11DepthStencilView> m_depthStencilView; /**< シーン用深度（内部描画解像度・MSAA） */
  ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
  ComPtr<ID3D11ShaderResourceView> m_depthSRV; /**< 深度SRV。MSAA有効時はnullptr（未対応） */

  // レンダラーステート
  ComPtr<ID3D11RasterizerState> m_rasterizerState;
  ComPtr<ID3D11DepthStencilState> m_depthStencilState;

  // --- オフスクリーンシーンレンダーターゲット（内部描画解像度） ---
  ComPtr<ID3D11Texture2D> m_sceneColorTexMS;       /**< MSAA>1のときのみ使用 */
  ComPtr<ID3D11RenderTargetView> m_sceneColorRTVMS;
  ComPtr<ID3D11Texture2D> m_sceneColorTexResolved; /**< 常に存在（MSAA解決先 or 直接描画先） */
  ComPtr<ID3D11RenderTargetView> m_sceneColorRTVResolved;
  ComPtr<ID3D11ShaderResourceView> m_sceneColorSRVResolved;

  // FXAA有効時のみ使用する中間テクスチャ（内部描画解像度）
  ComPtr<ID3D11Texture2D> m_fxaaTex;
  ComPtr<ID3D11RenderTargetView> m_fxaaRTV;
  ComPtr<ID3D11ShaderResourceView> m_fxaaSRV;

  // ポストプロセス（霧/色調補正/ビネット/ブルーム）出力先。常時使用（内部描画解像度）
  ComPtr<ID3D11Texture2D> m_postProcessTex;
  ComPtr<ID3D11RenderTargetView> m_postProcessRTV;
  ComPtr<ID3D11ShaderResourceView> m_postProcessSRV;

  // --- テンポラルAA（TAA/DLSS） ---
  TemporalMode m_temporalMode = TemporalMode::None;
  // 速度バッファ（描画解像度）。rg=UV速度、a=1なら物体の動き込み、0ならカメラのみ
  ComPtr<ID3D11Texture2D> m_velocityTex;
  ComPtr<ID3D11RenderTargetView> m_velocityRTV;
  ComPtr<ID3D11ShaderResourceView> m_velocitySRV;
  // 解決済み速度（描画解像度, RG16F）。TAAとDLSSの共通入力
  ComPtr<ID3D11Texture2D> m_resolvedVelocityTex;
  ComPtr<ID3D11RenderTargetView> m_resolvedVelocityRTV;
  ComPtr<ID3D11ShaderResourceView> m_resolvedVelocitySRV;
  // TAA履歴（出力解像度）。毎フレーム書き込み先を入れ替えるピンポン方式
  std::array<ComPtr<ID3D11Texture2D>, 2> m_taaHistoryTex;
  std::array<ComPtr<ID3D11RenderTargetView>, 2> m_taaHistoryRTV;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> m_taaHistorySRV;
  size_t m_taaWriteIndex = 0;
  // DLSS出力（出力解像度, UAV対応）
  ComPtr<ID3D11Texture2D> m_dlssOutputTex;
  ComPtr<ID3D11ShaderResourceView> m_dlssOutputSRV;
  DlssUpscaler m_dlss;

  bool m_temporalHistoryValid = false;
  bool m_temporalCameraSetThisFrame = false;
  bool m_temporalPrevViewProjectionValid = false;
  uint32_t m_temporalFrameIndex = 0;
  DirectX::XMFLOAT2 m_jitterNdc{0.0f, 0.0f};
  DirectX::XMFLOAT4X4 m_temporalViewProjection{};
  DirectX::XMFLOAT4X4 m_temporalPrevViewProjection{};
  std::chrono::steady_clock::time_point m_lastTemporalFrameAt{};
  float m_temporalFrameTimeMs = 0.0f;
  ComPtr<ID3D11Buffer> m_taaConstantBuffer;
  ComPtr<ID3D11Buffer> m_velocityResolveConstantBuffer;
  ComPtr<ID3D11SamplerState> m_pointSampler;

  // アップスケール/FXAA/ポストプロセス用の共有リソース
  Shader m_upscaleShader;
  Shader m_fxaaShader;
  Shader m_postProcessShader;
  Shader m_taaShader;
  Shader m_velocityResolveShader;
  ComPtr<ID3D11Buffer> m_fullscreenVB; /**< 画面全体を覆う巨大三角形（POSITION+TEXCOORD0） */
  ComPtr<ID3D11SamplerState> m_linearSampler;
  ComPtr<ID3D11Buffer> m_fxaaConstantBuffer;
  ComPtr<ID3D11Buffer> m_upscaleConstantBuffer;      /**< テクセルサイズ+シャープ強度 */
  ComPtr<ID3D11Buffer> m_postProcessConstantBuffer;  /**< PostProcessParams本体 */
  ComPtr<ID3D11BlendState> m_postProcessBlendState;     /**< ブレンド無効 */
  ComPtr<ID3D11DepthStencilState> m_postProcessDepthState; /**< 深度テスト無効 */
  ComPtr<ID3D11RasterizerState> m_postProcessRasterizerState; /**< カリング無効 */

  PostProcessParams m_postProcessParams;
  QualitySettings m_quality;
  bool m_vsyncEnabled = true;
  bool m_isExclusiveFullscreen = false;

  uint32_t m_width = 0;  /**< 出力（バックバッファ）解像度 */
  uint32_t m_height = 0;
  uint32_t m_renderWidth = 0;  /**< 内部描画解像度 = 出力解像度 * renderScale */
  uint32_t m_renderHeight = 0;
  D3D_DRIVER_TYPE m_driverType = D3D_DRIVER_TYPE_UNKNOWN;
  D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
  std::string m_adapterName = "Unknown";
  uint64_t m_dedicatedVideoMemoryBytes = 0;
  std::wstring m_preferredAdapterName; /**< 空なら自動選択（高性能優先） */

  static constexpr size_t kGpuQueryBufferCount = 8;
  static constexpr uint64_t kPipelineStatisticsInterval = 8;
  std::array<GpuFrameQueries, kGpuQueryBufferCount> m_gpuQueryFrames;
  size_t m_gpuQueryWriteIndex = 0;
  GpuFrameQueries *m_currentGpuQueryFrame = nullptr;
  std::vector<size_t> m_gpuScopeStack;
  std::vector<core::GpuFrameSample> m_readyGpuSamples;
  bool m_gpuProfilerAvailable = false;

  struct GpuFrameTimerQueries {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> frameStart;
    ComPtr<ID3D11Query> frameEnd;
    bool issued = false;
  };
  static constexpr size_t kGpuFrameTimerCount = 4;
  std::array<GpuFrameTimerQueries, kGpuFrameTimerCount> m_gpuFrameTimers;
  size_t m_gpuFrameTimerWriteIndex = 0;
  GpuFrameTimerQueries *m_currentGpuFrameTimer = nullptr;
  bool m_gpuFrameTimerAvailable = false;
  float m_latestGpuFrameMs = 0.0f;
};

class ScopedGpuTimer {
public:
#ifdef WIKIGOLF_PROFILING
  ScopedGpuTimer(GraphicsDevice &graphics, std::string_view name)
      : m_graphics(graphics) {
    m_graphics.BeginGpuScope(name);
  }
  ~ScopedGpuTimer() { m_graphics.EndGpuScope(); }

  ScopedGpuTimer(const ScopedGpuTimer &) = delete;
  ScopedGpuTimer &operator=(const ScopedGpuTimer &) = delete;

private:
  GraphicsDevice &m_graphics;
#else
  ScopedGpuTimer(GraphicsDevice &, std::string_view) {}
#endif
};

} // namespace graphics

#pragma once
/**
 * @file DlssUpscaler.h
 * @brief NVIDIA DLSS Super Resolution（NGX SDK / D3D11）の薄いラッパー
 * @details SDKが libs/DLSS に無いビルドでは WIKIGOLF_DLSS が定義されず、
 *          常に「非対応」として振る舞う（呼び出し側はTAAへフォールバックする）。
 */

#include <cstdint>
#include <d3d11.h>

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace graphics {

class DlssUpscaler {
public:
  /** @brief DLSSの画質モード（core::DlssQuality と同じ並び）*/
  enum class Quality { Performance = 0, Balanced, Quality, Dlaa };

  /** @brief 1フレーム分の入力 */
  struct EvaluateParams {
    ID3D11Resource *color = nullptr;         ///< ジッター付きシーンカラー（描画解像度）
    ID3D11Resource *depth = nullptr;         ///< シーン深度（描画解像度）
    ID3D11Resource *motionVectors = nullptr; ///< RG16F 速度（描画解像度）
    ID3D11Resource *output = nullptr;        ///< 出力先（出力解像度・UAV対応）
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    Quality quality = Quality::Quality;
    float jitterOffsetX = 0.0f; ///< 描画解像度ピクセル単位のジッター
    float jitterOffsetY = 0.0f;
    float motionVectorScaleX = 1.0f; ///< 速度テクスチャ値 → 前フレームへ向かうピクセル量
    float motionVectorScaleY = 1.0f;
    float frameTimeMs = 0.0f;
    bool reset = false; ///< 履歴を捨てる（シーン切り替え・カメラカット）
  };

  DlssUpscaler() = default;
  /**
   * @brief NGXの終了は Shutdown() で明示的に行う。
   * @details 所有者の GraphicsDevice::Shutdown() から、D3D11デバイス解放前に呼ばれる。
   *          デストラクタでSDK関数を呼ばないことで、GraphicsDeviceを生成するだけの
   *          テストがDLSS実装をリンクせずに済む。
   */
  ~DlssUpscaler() = default;
  DlssUpscaler(const DlssUpscaler &) = delete;
  DlssUpscaler &operator=(const DlssUpscaler &) = delete;

  /** @brief SDKを組み込んだビルドか */
  static bool IsCompiledIn();

  /**
   * @brief NGXを初期化し、このGPU/ドライバーでDLSSが使えるか調べる。
   * @return DLSSが使える場合true（失敗しても描画は続行できる）
   */
  bool Initialize(ID3D11Device *device);
  void Shutdown();
  bool IsSupported() const { return m_supported; }

  /**
   * @brief 出力解像度と画質モードからDLSS推奨の描画解像度を求める。
   * @return 取得できた場合true。DLAAは出力解像度そのまま。
   */
  bool QueryRenderSize(uint32_t outputWidth, uint32_t outputHeight,
                       Quality quality, uint32_t &renderWidth,
                       uint32_t &renderHeight);

  /**
   * @brief DLSSを実行する。解像度や画質が前回と違えばフィーチャーを作り直す。
   * @return 成功した場合true
   */
  bool Evaluate(ID3D11DeviceContext *context, const EvaluateParams &params);

  /** @brief 作成済みのDLSSフィーチャーを破棄する（解像度変更時など）*/
  void ReleaseFeature();

private:
  bool CreateFeature(ID3D11DeviceContext *context, const EvaluateParams &params);

  ID3D11Device *m_device = nullptr;
  NVSDK_NGX_Parameter *m_parameters = nullptr;
  NVSDK_NGX_Handle *m_feature = nullptr;
  bool m_initialized = false;
  bool m_supported = false;
  bool m_parametersFromCapability = false;
  uint32_t m_featureRenderWidth = 0;
  uint32_t m_featureRenderHeight = 0;
  uint32_t m_featureOutputWidth = 0;
  uint32_t m_featureOutputHeight = 0;
  Quality m_featureQuality = Quality::Quality;
};

} // namespace graphics

#pragma once
/**
 * @file ScreenFade.h
 * @brief ScreenFade クラスおよび関連定義
*/

#include "../../core/GameContext.h"
#include "../../resources/ResourceManager.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace game::utils {

/**
 * @brief フェード種別
 * @details IrisClose: 外側から中心へ円が閉じていく。SetIrisHoldRadiusで
 *          途中の小さな円で一瞬溜めてから閉じる。
 */
enum class FadeType { Fade, CircleWipe, HexagonWipe, IrisClose };

/** @brief 画面フェード制御クラス*/
class ScreenFade {
public:
  ScreenFade() = default;
  ~ScreenFade();

  /**
   * @brief フェード用の初期化処理を行います。
*/
  void Initialize(core::GameContext &ctx);

  /**
   * @brief フェード用の終了処理を行います。
*/
  void Shutdown(core::GameContext &ctx);

  /**
   * @brief フェードイン（画面が開く、見えるようになる）
   * @param duration 秒数
   * @param type フェードタイプ
   * @param color フェード色 (通常は黒か白)
*/
  void FadeIn(float duration, FadeType type = FadeType::Fade,
              DirectX::XMFLOAT3 color = {0, 0, 0});

  /**
   * @brief フェードアウト（画面が閉じる、隠れる）
   * @param duration 秒数
   * @param type フェードタイプ
   * @param color フェード色
*/
  void FadeOut(float duration, FadeType type = FadeType::Fade,
               DirectX::XMFLOAT3 color = {0, 0, 0});

  /**
   * @brief 毎フレームのフェード更新処理を行います。
*/
  void Update(float dt);

  /**
   * @brief 描画処理を行います。
*/
  void Render(core::GameContext &ctx);

  /** @brief ワイプの中心を設定 (0.0~1.0)*/
  void SetCenter(float u, float v);

  /** @brief IrisCloseで溜める円の半径（画面高さ=1基準。0で溜めなし）*/
  void SetIrisHoldRadius(float radius) { m_irisHoldRadius = radius; }

  bool IsFading() const { return m_isFading; }
  bool IsFadeOutComplete() const { return m_progress >= 1.0f && !m_fadeIn; }

private:
  resources::MeshHandle m_mesh;
  resources::ShaderHandle m_shader;

  bool m_isFading = false;
  bool m_fadeIn = true;    // フェードイン状態のフラグ
  float m_progress = 0.0f; // フェードの進捗率
  float m_duration = 1.0f;
  float m_timer = 0.0f;

  FadeType m_currentType = FadeType::Fade;
  DirectX::XMFLOAT3 m_color = {0, 0, 0};
  DirectX::XMFLOAT2 m_center = {0.5f, 0.5f};
  float m_irisHoldRadius = 0.0f;

  Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbFade;
  Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;

  // 定数バッファ用構造体
  struct FadeCB {
    DirectX::XMFLOAT4 Color;
    DirectX::XMFLOAT4 Params1; // progress, type, aspect, smoothness
    DirectX::XMFLOAT4 Params2; // center.xy, irisHoldRadius, -
    DirectX::XMFLOAT4 Params3; // viewportSize.xy, -, -
  };


};

} // namespace game::utils

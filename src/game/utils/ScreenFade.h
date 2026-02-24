#pragma once

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace game::utils {

enum class FadeType { Fade, CircleWipe, HexagonWipe };

/// @brief 画面フェード制御クラス
class ScreenFade {
public:
  ScreenFade() = default;
  ~ScreenFade();

  void Initialize(core::GameContext &ctx);
  void Shutdown(core::GameContext &ctx);

  /// @brief フェードイン（画面が開く、見えるようになる）
  /// @param duration 秒数
  /// @param type フェードタイプ
  /// @param color フェード色 (通常は黒か白)
  void FadeIn(float duration, FadeType type = FadeType::Fade,
              DirectX::XMFLOAT3 color = {0, 0, 0});

  /// @brief フェードアウト（画面が閉じる、隠れる）
  /// @param duration 秒数
  /// @param type フェードタイプ
  /// @param color フェード色
  void FadeOut(float duration, FadeType type = FadeType::Fade,
               DirectX::XMFLOAT3 color = {0, 0, 0});

  void Update(float dt);
  void Render(core::GameContext &ctx);

  /// @brief ワイプの中心を設定 (0.0~1.0)
  void SetCenter(float u, float v);

  bool IsFading() const { return m_isFading; }
  bool IsFadeOutComplete() const { return m_progress >= 1.0f && !m_fadeIn; }

private:
  ecs::Entity m_fadeEntity = UINT32_MAX;

  bool m_isFading = false;
  bool m_fadeIn = true;    // true: 1->0, false: 0->1
  float m_progress = 0.0f; // 0.0(全表示) -> 1.0(全隠蔽)
  float m_duration = 1.0f;
  float m_timer = 0.0f;

  FadeType m_currentType = FadeType::Fade;
  DirectX::XMFLOAT3 m_color = {0, 0, 0};
  DirectX::XMFLOAT2 m_center = {0.5f, 0.5f};

  Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbFade;
  Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;

  // 定数バッファ用構造体
  struct FadeCB {
    DirectX::XMFLOAT4 Color;
    DirectX::XMFLOAT4 Params1; // x:progress, y:type(as float), z:aspect, w:smoothness
    DirectX::XMFLOAT4 Params2; // x:u, y:v, z:pad, w:pad
  };

  // 専用定数バッファとシェーダーは、
  // ここで直接管理せず、MeshRenderer+Materialシステムの仕組みに乗っかる方が楽だが、
  // 今回は特殊なConstantBufferが必要なので、MeshRenderer経由で描画しつつ、
  // Updateで CB を書き換える方式をとるか、
  // あるいは ScreenFade 自体が描画ロジックを持つか。
  // -> シンプルに MeshRenderer を使い、Update
  // でマテリアル(定数バッファ)を更新する形は
  //    今のECSだと少し手間（Shaderが標準のものしか使えない可能性がある）。
  //    今回は「UIの上に描画」したいので、2D描画として実装するか、
  //    あるいは最前面の3D Quadとして実装するか。
  //    UIImageは標準で BasicShader を使うので、カスタムシェーダーを使いにくい。
  //    よって、このクラス独自で MeshRenderer
  //    を構成し、カスタムシェーダーをアサインする。
};

} // namespace game::utils

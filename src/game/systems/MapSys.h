#pragma once
/**
 * @file MapSys.h
 * @brief MapSys クラスおよび関連システム
*/

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace game::components {
struct GolfGameState;
}

namespace core {
struct GameContext;
}

namespace game::systems {

struct MapRenderParams {
  DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
  float extent = 50.0f;          /**< フィールドの代表的な広さ（幅・奥行の最大）*/
  float zoom = 1.0f;             /**< ズーム倍率（1.0が基準）*/
  float heightScale = 2.0f;      /**< 俯瞰高さの倍率（extent * zoom * heightScale）*/
  float orthoPadding = 1.2f;     /**< 正射影の幅/高さに掛ける余裕倍率*/
  bool highlightBall = true;     /**< ボールを強調表示するか*/
  bool cullSkybox = true;        /**< スカイボックスを描画対象から除外するか*/
};

class MapSys {
public:
  MapSys() = default;
  ~MapSys() = default;

  /**
   * @brief ミニマップ描画用のレンダーターゲットとリソースを初期化します。
   * @param device Direct3D 11 デバイス
   * @param width レンダーターゲット幅
   * @param height レンダーターゲット高さ
   * @return 初期化成否
   */
  bool Initialize(ID3D11Device *device, int width, int height);

  /**
   * @brief ミニマップを描画します。
   * @param ctx ゲームコンテキスト
   * @param params 描画パラメータ
   */
  void Render(core::GameContext &ctx, const MapRenderParams &params);

  /**
   * @brief ミニマップを描画します（Renderのエイリアス）。
   * @param ctx ゲームコンテキスト
   * @param params 描画パラメータ
   */
  void RenderMinimap(core::GameContext &ctx,
                     const MapRenderParams &params = MapRenderParams()) {
    Render(ctx, params);
  }

  /**
   * @brief ミニマップテクスチャのSRVを取得します。
   * @return シェーダーリソースビュー
   */
  ID3D11ShaderResourceView *GetSRV() const { return m_srv.Get(); }

  /**
   * @brief 見下ろしビュー行列を計算します。
   * @param cx 注視点X
   * @param cz 注視点Z
   * @param h カメラ高度
   * @return ビュー行列
   */
  DirectX::XMMATRIX GetViewMatrix(float cx, float cz, float h);

  /**
   * @brief 正射影行列を計算します。
   * @param w 横幅
   * @param d 奥行き幅
   * @return 正射影行列
   */
  DirectX::XMMATRIX GetProjMatrix(float w, float d);

private:
  /** @brief レンダーターゲットを切り替えて描画を開始します。 */
  void BeginRender(ID3D11DeviceContext *ctx);

  /** @brief 以前のレンダーターゲットを復元して描画を終了します。 */
  void EndRender(ID3D11DeviceContext *ctx);

  int m_width = 200;
  int m_height = 200;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> m_rt;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> m_ds;
  D3D11_VIEWPORT m_vp;

  Microsoft::WRL::ComPtr<ID3D11Buffer> m_cb;
  Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samp;

  // インスタンシング用の動的構造化バッファ（t15にバインド）
  Microsoft::WRL::ComPtr<ID3D11Buffer> m_instancedBuffer;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_instancedSRV;
  size_t m_instancedBufferCapacity = 0;

  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_saveRTV;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_saveDSV;
  D3D11_VIEWPORT m_saveVP;

  /**
   * @brief インスタンシング描画用バッファの容量を確保します。
   * @param device Direct3D 11 デバイス
   * @param requiredCount 必要インスタンス数
   * @return 確保成否
   */
  bool EnsureInstanceBuffer(ID3D11Device *device, size_t requiredCount);
};

} // namespace game::systems

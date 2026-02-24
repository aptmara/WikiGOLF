#pragma once
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
  float extent = 50.0f;          ///< フィールドの代表的な広さ（幅・奥行の最大）
  float zoom = 1.0f;             ///< ズーム倍率（1.0が基準）
  float heightScale = 2.0f;      ///< 俯瞰高さの倍率（extent * zoom * heightScale）
  float orthoPadding = 1.2f;     ///< 正射影の幅/高さに掛ける余裕倍率
  bool highlightBall = true;     ///< ボールを強調表示するか
  bool cullSkybox = true;        ///< スカイボックスを描画対象から除外するか
};

class MapSys {
public:
  MapSys() = default;
  ~MapSys() = default;

  bool Initialize(ID3D11Device *device, int width, int height);
  void Render(core::GameContext &ctx, const MapRenderParams &params);
  void RenderMinimap(core::GameContext &ctx,
                     const MapRenderParams &params = MapRenderParams()) {
    Render(ctx, params);
  }
  ID3D11ShaderResourceView *GetSRV() const { return m_srv.Get(); }

  DirectX::XMMATRIX GetViewMatrix(float cx, float cz, float h);
  DirectX::XMMATRIX GetProjMatrix(float w, float d);

private:
  void BeginRender(ID3D11DeviceContext *ctx);
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

  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_saveRTV;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_saveDSV;
  D3D11_VIEWPORT m_saveVP;
};

} // namespace game::systems

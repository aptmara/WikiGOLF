#pragma once
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>


namespace core {
struct GameContext;
}

namespace game::systems {

class MapSys {
public:
  MapSys() = default;
  ~MapSys() = default;

  bool Initialize(ID3D11Device *device, int width, int height);
  void RenderMinimap(core::GameContext &ctx);
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

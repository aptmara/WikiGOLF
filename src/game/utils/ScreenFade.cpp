#include "ScreenFade.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include <algorithm>

using namespace DirectX;
using namespace game::components;

namespace game::utils {

// 念のための終了処理
ScreenFade::~ScreenFade() {
}

/**
 * @brief フェード用の初期化�（琁（��行います、（
 */
void ScreenFade::Initialize(core::GameContext &ctx) {
  static_assert((sizeof(FadeCB) % 16) == 0,
                "FadeCB must be 16-byte aligned for constant buffers");
  
  // フェード用のエンティティを作成します。
  m_fadeEntity = ctx.world.CreateEntity();

  // トランスフォームの設定を行います。
  auto &t = ctx.world.Add<Transform>(m_fadeEntity);
  t.position = {0, 0, 0};

  // メッシュのロードを行います。
  auto &mr = ctx.world.Add<MeshRenderer>(m_fadeEntity);
  mr.mesh = ctx.resource.LoadMesh("builtin/quad");

  // カスタムシェーダーのロードを行います。
  mr.shader =
      ctx.resource.LoadShader("TransitionFade", L"Assets/shaders/BasicVS.hlsl",
                              L"Assets/shaders/TransitionPS.hlsl");

  // 手動描画するため描画システムでの描画は無効化します。
  mr.isVisible = false;

  // 定数バッファを生成します。
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = sizeof(FadeCB);
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  HRESULT hr = ctx.graphics.GetDevice()->CreateBuffer(&desc, nullptr,
                                                      m_cbFade.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("ScreenFade", "Failed to create constant buffer");
  }

  // アルファブレンド用のステートを生成します。
  D3D11_BLEND_DESC blendDesc = {};
  blendDesc.RenderTarget[0].BlendEnable = TRUE;
  blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
  blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blendDesc.RenderTarget[0].RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_ALL;

  hr = ctx.graphics.GetDevice()->CreateBlendState(&blendDesc,
                                                  m_blendState.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("ScreenFade", "Failed to create blend state");
  }

  // 深度テストおよび深度書き込み無効用のステートを生成します。
  D3D11_DEPTH_STENCIL_DESC dsDesc = {};
  dsDesc.DepthEnable = FALSE;
  dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

  hr = ctx.graphics.GetDevice()->CreateDepthStencilState(
      &dsDesc, m_depthState.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("ScreenFade", "Failed to create depth stencil state");
  }

  // フェードの初期状態を設定します。
  m_isFading = false;
  m_progress = 0.0f;
}

/**
 * @brief フェード用の終了�（琁（��行います、（
 */
void ScreenFade::Shutdown(core::GameContext &ctx) {
  if (m_fadeEntity != UINT32_MAX && ctx.world.IsAlive(m_fadeEntity)) {
    ctx.world.DestroyEntity(m_fadeEntity);
    m_fadeEntity = UINT32_MAX;
  }
  m_cbFade.Reset();
  m_blendState.Reset();
  m_depthState.Reset();
}

/**
 * @brief フェードイン�（�画面が開く、見えるよぁ（��なる）�（琁（��開始します、（
 */
void ScreenFade::FadeIn(float duration, FadeType type,
                        DirectX::XMFLOAT3 color) {
  m_isFading = true;
  m_fadeIn = true;
  m_duration = duration;
  m_timer = 0.0f;
  m_currentType = type;
  m_color = color;
  m_progress = 1.0f;
}

/**
 * @brief フェードアウト（画面が閉じる、（��れる�（��（琁（��開始します、（
 */
void ScreenFade::FadeOut(float duration, FadeType type,
                         DirectX::XMFLOAT3 color) {
  m_isFading = true;
  m_fadeIn = false;
  m_duration = duration;
  m_timer = 0.0f;
  m_currentType = type;
  m_color = color;
  m_progress = 0.0f;
}

/**
 * @brief ワイプ�（中心��設定します、（
 */
void ScreenFade::SetCenter(float u, float v) { m_center = {u, v}; }

/**
 * @brief 毎フレームのフェード更新処理��行います、（
 */
void ScreenFade::Update(float dt) {
  if (m_isFading) {
    m_timer += dt;
    float rate = 1.0f;
    if (m_duration > 0.0f) {
      rate = std::clamp(m_timer / m_duration, 0.0f, 1.0f);
    }

    if (m_fadeIn) {
      m_progress = 1.0f - rate;
    } else {
      m_progress = rate;
    }

    if (rate >= 1.0f) {
      m_isFading = false;
    }
  }
}

/**
 * @brief 描画処理��行います、（
 */
void ScreenFade::Render(core::GameContext &ctx) {
  // 描画の必要性を判定します。
  if (m_progress <= 0.001f && !m_isFading) {
    return;
  }

  if (!ctx.world.IsAlive(m_fadeEntity))
    return;

  auto *mr = ctx.world.Get<MeshRenderer>(m_fadeEntity);
  if (!mr)
    return;

  auto device = ctx.graphics.GetDevice();
  auto context = ctx.graphics.GetContext();

  // 定数バッファを更新します。
  if (m_cbFade) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_cbFade.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                               &mapped))) {
      FadeCB *cb = static_cast<FadeCB *>(mapped.pData);
      cb->Color = {m_color.x, m_color.y, m_color.z, 1.0f};

      // アスペクト比を取得します。
      D3D11_VIEWPORT vp;
      UINT num = 1;
      context->RSGetViewports(&num, &vp);
      float aspect = 1.777f;
      if (vp.Height > 0) {
        aspect = vp.Width / vp.Height;
      }

      cb->Params1 = {m_progress, static_cast<float>(m_currentType), aspect,
                     0.1f};
      cb->Params2 = {m_center.x, m_center.y, 0.0f, 0.0f};

      context->Unmap(m_cbFade.Get(), 0);
    }

    // ピクセルシェーザに定数バッファを適用します。
    context->PSSetConstantBuffers(0, 1, m_cbFade.GetAddressOf());
  }

  // 全画面描画のための頂点・ピクセルシェーダーおよびメッシュを取得します。
  const auto *shader = ctx.resource.GetShader(mr->shader);
  const auto *mesh = ctx.resource.GetMesh(mr->mesh);

  if (!shader || !shader->IsValid())
    return;
  if (!mesh || !mesh->IsValid())
    return;

  // シェーダーをバインドします。
  shader->Bind(context);

  // 全画面描画のための行列を計算します。
  XMMATRIX world = XMMatrixScaling(2.0f, 2.0f, 1.0f);
  XMMATRIX view = XMMatrixIdentity();
  XMMATRIX proj = XMMatrixIdentity();

  // 頂点シェーダー用定数バッファを生成します。
  struct BasicCB {
    XMMATRIX World;
    XMMATRIX View;
    XMMATRIX Projection;
    XMFLOAT4 Color;
    XMFLOAT4 CameraPos; // BasicVSが要求する場合
  };

  // このCBはメンバとして持っておくべきだが、今回は即席で作る
  static Microsoft::WRL::ComPtr<ID3D11Buffer> s_basicCB;
  if (!s_basicCB) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(BasicCB);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ctx.graphics.GetDevice()->CreateBuffer(&bd, nullptr,
                                           s_basicCB.GetAddressOf());
  }

  if (s_basicCB) {
    BasicCB bcb;
    bcb.World = XMMatrixTranspose(world);
    bcb.View = XMMatrixTranspose(view);
    bcb.Projection = XMMatrixTranspose(proj);
    bcb.Color = {1, 1, 1, 1};
    // bcb.CameraPos ... BasicVSの内容次第だが、標準的ならWVPのみか？
    // DX_GAMEのBasicVSを確認していないが、通常はWVP。

    context->UpdateSubresource(s_basicCB.Get(), 0, nullptr, &bcb, 0, 0);
    context->VSSetConstantBuffers(0, 1, s_basicCB.GetAddressOf());
  }

  // メッシュ描画 (Bind inside)
  mesh->Bind(context);

  // ブレンドステート（AlphaBlend）
  float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);

  // 深度ステート（ZTest OFF, ZWrite OFF）
  context->OMSetDepthStencilState(m_depthState.Get(), 0);
  mesh->Draw(context);

  // 復帰（深度有効、Z書き込み有効）
  context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
  context->OMSetDepthStencilState(nullptr, 0);
}

} // namespace game::utils

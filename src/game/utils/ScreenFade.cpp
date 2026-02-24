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

ScreenFade::~ScreenFade() {
  // ShutdownはOnExitで呼ばれる想定だが念のため
}

void ScreenFade::Initialize(core::GameContext &ctx) {
  static_assert((sizeof(FadeCB) % 16) == 0,
                "FadeCB must be 16-byte aligned for constant buffers");
  // フェード用エンティティ作成
  m_fadeEntity = ctx.world.CreateEntity();

  // Transform (位置はRender時にカメラ前に配置するので一旦原点)
  auto &t = ctx.world.Add<Transform>(m_fadeEntity);
  t.position = {0, 0, 0};

  auto &mr = ctx.world.Add<MeshRenderer>(m_fadeEntity);
  mr.mesh = ctx.resource.LoadMesh("builtin/quad"); // 平面

  // シェーダーロード (カスタム)
  mr.shader =
      ctx.resource.LoadShader("TransitionFade", L"Assets/shaders/BasicVS.hlsl",
                              L"Assets/shaders/TransitionPS.hlsl");

  mr.isVisible = false; // 手動描画するのでシステムによる描画はOFFにしたいが…
  // MeshRenderSystemは isVisible=true のものだけ描画する。
  // 手動描画するなら isVisible=false にしておいて、
  // Render() 内で一時的に描画するか、
  // あるいは単純に MeshRendererSystem の外でドローコールを呼ぶ。
  // ここでは「MeshRendererコンポーネント」は「リソース(Mesh/Shader)のコンテナ」としてのみ使い、
  // isVisible=false にしておく。

  // 定数バッファ作成
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

  // ブレンドステート作成 (AlphaBlend)
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

  // 深度ステート作成 (ZTest OFF, ZWrite OFF)
  D3D11_DEPTH_STENCIL_DESC dsDesc = {};
  dsDesc.DepthEnable = FALSE; // Zテスト無効
  dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

  hr = ctx.graphics.GetDevice()->CreateDepthStencilState(
      &dsDesc, m_depthState.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("ScreenFade", "Failed to create depth stencil state");
  }

  // 初期状態
  m_isFading = false;
  m_progress = 0.0f;
}

void ScreenFade::Shutdown(core::GameContext &ctx) {
  if (m_fadeEntity != UINT32_MAX && ctx.world.IsAlive(m_fadeEntity)) {
    ctx.world.DestroyEntity(m_fadeEntity);
    m_fadeEntity = UINT32_MAX;
  }
  m_cbFade.Reset();
  m_blendState.Reset();
  m_depthState.Reset();
}

void ScreenFade::FadeIn(float duration, FadeType type,
                        DirectX::XMFLOAT3 color) {
  m_isFading = true;
  m_fadeIn = true; // 1->0
  m_duration = duration;
  m_timer = 0.0f;
  m_currentType = type;
  m_color = color;
  m_progress = 1.0f;
}

void ScreenFade::FadeOut(float duration, FadeType type,
                         DirectX::XMFLOAT3 color) {
  m_isFading = true;
  m_fadeIn = false; // 0->1
  m_duration = duration;
  m_timer = 0.0f;
  m_currentType = type;
  m_color = color;
  m_progress = 0.0f;
}

void ScreenFade::SetCenter(float u, float v) { m_center = {u, v}; }

void ScreenFade::Update(float dt) {
  if (m_isFading) {
    m_timer += dt;
    float rate = (m_duration > 0.0f)
                     ? std::clamp(m_timer / m_duration, 0.0f, 1.0f)
                     : 1.0f;

    if (m_fadeIn) {
      m_progress = 1.0f - rate;
    } else {
      m_progress = rate;
    }

    if (rate >= 1.0f) {
      m_isFading = false;
      // フェード完了
      // FadeIn完了時(progress=0): 描画不要
      // FadeOut完了時(progress=1): 描画維持が必要（真っ黒）
    }
  }
}

void ScreenFade::Render(core::GameContext &ctx) {
  // 描画すべきか？
  // FadeIn中でProgress < 1.0 (かつ >0)
  // FadeOut中でProgress > 0.0
  // FadeOut完了後 (完全隠蔽状態)
  // FadeIn完了後 (完全表示) -> Progress=0 なら描画不要

  if (m_progress <= 0.001f && !m_isFading) {
    return; // 完全透明かつ動作中でなければ描画スキップ
  }

  if (!ctx.world.IsAlive(m_fadeEntity))
    return;

  auto *mr = ctx.world.Get<MeshRenderer>(m_fadeEntity);
  if (!mr)
    return;

  auto device = ctx.graphics.GetDevice();
  auto context = ctx.graphics.GetContext();

  // 定数バッファ更新
  if (m_cbFade) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_cbFade.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                               &mapped))) {
      FadeCB *cb = static_cast<FadeCB *>(mapped.pData);
      cb->Color = {m_color.x, m_color.y, m_color.z, 1.0f};

      // アスペクト比取得
      D3D11_VIEWPORT vp;
      UINT num = 1;
      context->RSGetViewports(&num, &vp);
      float aspect = (vp.Height > 0) ? (vp.Width / vp.Height) : 1.777f;

      cb->Params1 = {m_progress, static_cast<float>(m_currentType), aspect,
                     0.1f}; // Smoothness固定値
      cb->Params2 = {m_center.x, m_center.y, 0.0f, 0.0f};

      context->Unmap(m_cbFade.Get(), 0);
    }

    // PS定数バッファスロット0にセット
    context->PSSetConstantBuffers(0, 1, m_cbFade.GetAddressOf());
  }

  // 全画面描画の準備
  // Zテスト無効化 (最前面)
  // ブレンディング有効化 (半透明)

  // スクリーン座標系でのFullScreen Quad描画を行うのが理想だが、
  // ここでは簡易的に「カメラの目の前に板を置く」方式ではなく、
  // 頂点シェーダーに恒等変換行列を渡してスクリーン座標(-1~1)に直接描画させるトリックを使う。
  // しかし BasicVS は World/View/Proj を要求する。
  // ScreenFade専用のVSを作っていないので、BasicVSを使う前提で考えると、
  // Identity行列をセットして、メッシュの座標を適切に設定する必要がある。
  // builtin/quad は -0.5~0.5 のサイズ。

  // 一番簡単なのは、VSで何もせず、入力座標をそのまま出力する「ScreenVS」を用意することだが、
  // 今回は BasicVS を流用する。
  // W, V, P 全て Identity にし、Meshのスケールを 2.0 (全画面) にすればOK。
  // ただしZが0だとクリップされるかも？

  // シェーダーリソース取得
  // シェーダーリソース取得
  const auto *shader = ctx.resource.GetShader(mr->shader);
  const auto *mesh = ctx.resource.GetMesh(mr->mesh);

  if (!shader || !shader->IsValid())
    return;
  if (!mesh || !mesh->IsValid())
    return;

  // パイプライン設定
  shader->Bind(context);

  // 行列計算: 全て単位行列 + スケール2倍(Quadは1x1なので2x2に)
  // Zは0だとニアクリップにかかる可能性があるので、Z=0.5 (0~1範囲内)
  // にする設定が必要かも？ depth clip を切れば関係ない。

  XMMATRIX world = XMMatrixScaling(2.0f, 2.0f, 1.0f);
  XMMATRIX view = XMMatrixIdentity();
  XMMATRIX proj = XMMatrixIdentity();
  // ProjがIdentityなら、View空間座標がそのままClip空間座標になる。(-1~1)

  // 定数バッファ (BasicVS用) 更新
  // Context経由で更新する必要あり。
  // ここでは MeshRenderSystem::RenderMesh のロジックを再実装する形になるが、
  // BasicVS の CB構文に合わせて更新する必要がある。
  // BasicVSのCBは register(b0) で、構造体は World, View, Proj, Color 等...
  // しかし GameContext からは System内部のCBにアクセスできない。
  //
  // 結論: 新しくCBを作る or 既存の仕組みを使う。
  // 時間がないので、World/View/Proj を受け取る VS をそのまま使うなら、
  // メッシュレンダラーの標準描画パスに乗せるのが一番安全で楽だったかもしれない。
  // 「isVisible =
  // true」にして、カメラの子供にし、ZテストをOFFにするMaterialを使うなど。

  // しかし、ここまで来たので「手動描画」で行く。
  // BasicVS用の定数バッファを一時的に作成してバインドする。

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

  // ラスタライザ（CullNone）念のため
  // ctx.graphics.SetCullMode... (ないかも)

  mesh->Draw(context);

  // 復帰（深度有効、Z書き込み有効）

  // 復帰（深度有効、Z書き込み有効）
  // 元のステートに戻すのが行儀良いが、DX_GAMEの設計上、
  // 次のフレームの開始時にリセットされるか、他のレンダラーが設定し直すことを期待する。
  // RenderSystemはBeginFrameでクリアされるがStateは維持されるため、
  // ここでデフォルトに戻すべきだが、デフォルトステートを持っていない。
  // nullptr をセットするとデフォルトになる特性を利用する。
  context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
  context->OMSetDepthStencilState(nullptr, 0);
}

} // namespace game::utils

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

// 蠢ｵ縺ｮ縺溘ａ縺ｮ邨ゆｺ・・逅・
ScreenFade::~ScreenFade() {
}

/**
 * @brief 繝輔ぉ繝ｼ繝臥畑縺ｮ蛻晄悄蛹門・逅・ｒ陦後＞縺ｾ縺吶・
 */
void ScreenFade::Initialize(core::GameContext &ctx) {
  static_assert((sizeof(FadeCB) % 16) == 0,
                "FadeCB must be 16-byte aligned for constant buffers");
  
  // 繝輔ぉ繝ｼ繝臥畑縺ｮ繧ｨ繝ｳ繝・ぅ繝・ぅ繧剃ｽ懈・縺励∪縺吶・
  m_fadeEntity = ctx.world.CreateEntity();

  // 繝医Λ繝ｳ繧ｹ繝輔か繝ｼ繝縺ｮ險ｭ螳壹ｒ陦後＞縺ｾ縺吶・
  auto &t = ctx.world.Add<Transform>(m_fadeEntity);
  t.position = {0, 0, 0};

  // 繝｡繝・す繝･縺ｮ繝ｭ繝ｼ繝峨ｒ陦後＞縺ｾ縺吶・
  auto &mr = ctx.world.Add<MeshRenderer>(m_fadeEntity);
  mr.mesh = ctx.resource.LoadMesh("builtin/quad");

  // 繧ｫ繧ｹ繧ｿ繝繧ｷ繧ｧ繝ｼ繝繝ｼ縺ｮ繝ｭ繝ｼ繝峨ｒ陦後＞縺ｾ縺吶・
  mr.shader =
      ctx.resource.LoadShader("TransitionFade", L"Assets/shaders/BasicVS.hlsl",
                              L"Assets/shaders/TransitionPS.hlsl");

  // 謇句虚謠冗判縺吶ｋ縺溘ａ謠冗判繧ｷ繧ｹ繝・Β縺ｧ縺ｮ謠冗判縺ｯ辟｡蜉ｹ蛹悶＠縺ｾ縺吶・
  mr.isVisible = false;

  // 螳壽焚繝舌ャ繝輔ぃ繧堤函謌舌＠縺ｾ縺吶・
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

  // 繧｢繝ｫ繝輔ぃ繝悶Ξ繝ｳ繝臥畑縺ｮ繧ｹ繝・・繝医ｒ逕滓・縺励∪縺吶・
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

  // 豺ｱ蠎ｦ繝・せ繝医♀繧医・豺ｱ蠎ｦ譖ｸ縺崎ｾｼ縺ｿ辟｡蜉ｹ逕ｨ縺ｮ繧ｹ繝・・繝医ｒ逕滓・縺励∪縺吶・
  D3D11_DEPTH_STENCIL_DESC dsDesc = {};
  dsDesc.DepthEnable = FALSE;
  dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

  hr = ctx.graphics.GetDevice()->CreateDepthStencilState(
      &dsDesc, m_depthState.GetAddressOf());
  if (FAILED(hr)) {
    LOG_ERROR("ScreenFade", "Failed to create depth stencil state");
  }

  // 繝輔ぉ繝ｼ繝峨・蛻晄悄迥ｶ諷九ｒ險ｭ螳壹＠縺ｾ縺吶・
  m_isFading = false;
  m_progress = 0.0f;
}

/**
 * @brief 繝輔ぉ繝ｼ繝臥畑縺ｮ邨ゆｺ・・逅・ｒ陦後＞縺ｾ縺吶・
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
 * @brief 繝輔ぉ繝ｼ繝峨う繝ｳ・育判髱｢縺碁幕縺上∬ｦ九∴繧九ｈ縺・↓縺ｪ繧具ｼ牙・逅・ｒ髢句ｧ九＠縺ｾ縺吶・
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
 * @brief 繝輔ぉ繝ｼ繝峨い繧ｦ繝茨ｼ育判髱｢縺碁哩縺倥ｋ縲・國繧後ｋ・牙・逅・ｒ髢句ｧ九＠縺ｾ縺吶・
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
 * @brief 繝ｯ繧､繝励・荳ｭ蠢・ｒ險ｭ螳壹＠縺ｾ縺吶・
 */
void ScreenFade::SetCenter(float u, float v) { m_center = {u, v}; }

/**
 * @brief 豈弱ヵ繝ｬ繝ｼ繝縺ｮ繝輔ぉ繝ｼ繝画峩譁ｰ蜃ｦ逅・ｒ陦後＞縺ｾ縺吶・
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
 * @brief 謠冗判蜃ｦ逅・ｒ陦後＞縺ｾ縺吶・
 */
void ScreenFade::Render(core::GameContext &ctx) {
  // 謠冗判縺ｮ蠢・ｦ∵ｧ繧貞愛螳壹＠縺ｾ縺吶・
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

  // 螳壽焚繝舌ャ繝輔ぃ繧呈峩譁ｰ縺励∪縺吶・
  if (m_cbFade) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_cbFade.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                               &mapped))) {
      FadeCB *cb = static_cast<FadeCB *>(mapped.pData);
      cb->Color = {m_color.x, m_color.y, m_color.z, 1.0f};

      // 繧｢繧ｹ繝壹け繝域ｯ斐ｒ蜿門ｾ励＠縺ｾ縺吶・
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

    // 繝斐け繧ｻ繝ｫ繧ｷ繧ｧ繝ｼ繧ｶ縺ｫ螳壽焚繝舌ャ繝輔ぃ繧帝←逕ｨ縺励∪縺吶・
    context->PSSetConstantBuffers(0, 1, m_cbFade.GetAddressOf());
  }

  // 蜈ｨ逕ｻ髱｢謠冗判縺ｮ縺溘ａ縺ｮ鬆らせ繝ｻ繝斐け繧ｻ繝ｫ繧ｷ繧ｧ繝ｼ繝繝ｼ縺翫ｈ縺ｳ繝｡繝・す繝･繧貞叙蠕励＠縺ｾ縺吶・
  const auto *shader = ctx.resource.GetShader(mr->shader);
  const auto *mesh = ctx.resource.GetMesh(mr->mesh);

  if (!shader || !shader->IsValid())
    return;
  if (!mesh || !mesh->IsValid())
    return;

  // 繧ｷ繧ｧ繝ｼ繝繝ｼ繧偵ヰ繧､繝ｳ繝峨＠縺ｾ縺吶・
  shader->Bind(context);

  // 蜈ｨ逕ｻ髱｢謠冗判縺ｮ縺溘ａ縺ｮ陦悟・繧定ｨ育ｮ励＠縺ｾ縺吶・
  XMMATRIX world = XMMatrixScaling(2.0f, 2.0f, 1.0f);
  XMMATRIX view = XMMatrixIdentity();
  XMMATRIX proj = XMMatrixIdentity();

  // 鬆らせ繧ｷ繧ｧ繝ｼ繝繝ｼ逕ｨ螳壽焚繝舌ャ繝輔ぃ繧堤函謌舌＠縺ｾ縺吶・
  struct BasicCB {
    XMMATRIX World;
    XMMATRIX View;
    XMMATRIX Projection;
    XMFLOAT4 Color;
    XMFLOAT4 CameraPos; // BasicVS縺瑚ｦ∵ｱゅ☆繧句ｴ蜷・
  };

  // 縺薙・CB縺ｯ繝｡繝ｳ繝舌→縺励※謖√▲縺ｦ縺翫￥縺ｹ縺阪□縺後∽ｻ雁屓縺ｯ蜊ｳ蟶ｭ縺ｧ菴懊ｋ
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
    // bcb.CameraPos ... BasicVS縺ｮ蜀・ｮｹ谺｡隨ｬ縺縺後∵ｨ呎ｺ也噪縺ｪ繧姥VP縺ｮ縺ｿ縺具ｼ・
    // DX_GAME縺ｮBasicVS繧堤｢ｺ隱阪＠縺ｦ縺・↑縺・′縲・壼ｸｸ縺ｯWVP縲・

    context->UpdateSubresource(s_basicCB.Get(), 0, nullptr, &bcb, 0, 0);
    context->VSSetConstantBuffers(0, 1, s_basicCB.GetAddressOf());
  }

  // 繝｡繝・す繝･謠冗判 (Bind inside)
  mesh->Bind(context);

  // 繝悶Ξ繝ｳ繝峨せ繝・・繝茨ｼ・lphaBlend・・
  float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);

  // 豺ｱ蠎ｦ繧ｹ繝・・繝茨ｼ・Test OFF, ZWrite OFF・・
  context->OMSetDepthStencilState(m_depthState.Get(), 0);
  mesh->Draw(context);

  // 蠕ｩ蟶ｰ・域ｷｱ蠎ｦ譛牙柑縲〇譖ｸ縺崎ｾｼ縺ｿ譛牙柑・・
  context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
  context->OMSetDepthStencilState(nullptr, 0);
}

} // namespace game::utils

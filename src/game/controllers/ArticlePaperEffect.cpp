/**
 * @file ArticlePaperEffect.cpp
 * @brief ArticlePaperEffect の実装
*/

#include "ArticlePaperEffect.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../../graphics/Mesh.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../utils/GoalNoticeRules.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace game::controllers {

namespace {

// 地面に埋まらず、かつ手前の物に遮られない距離を近い順に試す
constexpr float kPaperMaxDistance = 17.0f;
constexpr float kPaperMinDistance = 8.0f;
constexpr float kPaperDistanceStep = 1.0f;
constexpr float kPaperGroundClearance = 0.4f;
constexpr float kPaperOverscan = 1.01f; ///< 画面の縁が見えないよう少しだけ大きくする
constexpr float kVirtualWidth = 1280.0f;  ///< UI の仮想解像度（TextRenderer と同じ）
constexpr float kVirtualHeight = 720.0f;

/**
 * @brief 幅1・高さ1で、下辺の中央を原点とする板を作ります。
 * @details 下辺を軸に倒すため、原点を下辺に置きます。
*/
resources::MeshHandle CreatePaperMesh(core::GameContext &ctx) {
  const DirectX::XMFLOAT3 normal = {0.0f, 0.0f, -1.0f};
  const DirectX::XMFLOAT4 white = {1.0f, 1.0f, 1.0f, 1.0f};
  std::vector<graphics::Vertex> vertices = {
      {{-0.5f, 0.0f, 0.0f}, normal, {0.0f, 1.0f}, white},
      {{-0.5f, 1.0f, 0.0f}, normal, {0.0f, 0.0f}, white},
      {{0.5f, 1.0f, 0.0f}, normal, {1.0f, 0.0f}, white},
      {{0.5f, 0.0f, 0.0f}, normal, {1.0f, 1.0f}, white},
  };
  const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
  return ctx.resource.CreateDynamicMesh("ArticlePaper", vertices, indices);
}

resources::ShaderHandle LoadPaperShader(core::GameContext &ctx) {
  return ctx.resource.LoadShader("ArticlePaper", L"Assets/shaders/TexturedVS.hlsl",
                                 L"Assets/shaders/ArticlePaperPS.hlsl");
}

} // namespace

bool ArticlePaperEffect::Spawn(core::GameContext &ctx, const Setup &setup) {
  Shutdown(ctx);
  if (setup.tiles.empty()) {
    return false;
  }

  using namespace DirectX;
  m_eye = setup.cameraPosition;
  const XMVECTOR eye = XMLoadFloat3(&setup.cameraPosition);
  const XMVECTOR view = XMVector3Normalize(
      XMVectorSubtract(XMLoadFloat3(&setup.focusPosition), eye));
  XMFLOAT3 viewDirection;
  XMStoreFloat3(&viewDirection, view);

  // 回転軸はX軸。傾きがカメラの俯角と等しいとき紙はカメラと正対する
  m_standAngle = std::asin(std::clamp(-viewDirection.y, -1.0f, 1.0f));
  const XMVECTOR paperUp = XMVectorSet(0.0f, std::cos(m_standAngle),
                                       std::sin(m_standAngle), 0.0f);

  for (float distance = kPaperMaxDistance; distance >= kPaperMinDistance;
       distance -= kPaperDistanceStep) {
    m_distance = distance;
    m_visibleHeight = 2.0f * distance * std::tan(setup.cameraFov * 0.5f);
    m_visibleWidth = m_visibleHeight * setup.cameraAspect;
    m_paperHeight = m_visibleHeight * kPaperOverscan;
    m_paperWidth = m_visibleWidth * kPaperOverscan;
    const XMVECTOR center = XMVectorAdd(eye, XMVectorScale(view, distance));
    XMStoreFloat3(&m_hinge, XMVectorSubtract(
                                center, XMVectorScale(paperUp, m_paperHeight * 0.5f)));
    if (!setup.groundHeight) {
      break;
    }
    const float halfWidth = m_paperWidth * 0.5f;
    const float ground = std::max({setup.groundHeight(m_hinge.x, m_hinge.z),
                                   setup.groundHeight(m_hinge.x - halfWidth, m_hinge.z),
                                   setup.groundHeight(m_hinge.x + halfWidth, m_hinge.z)});
    if (m_hinge.y >= ground + kPaperGroundClearance) {
      break;
    }
  }

  std::vector<utils::ArticlePaperTileSize> tileSizes;
  tileSizes.reserve(setup.tiles.size());
  for (const Tile &tile : setup.tiles) {
    tileSizes.push_back({tile.width, tile.height});
  }
  const float contentWidth =
      m_paperWidth * (1.0f - 2.0f * utils::kArticlePaperSideMargin);
  const auto strips =
      utils::BuildArticlePaperStrips(contentWidth, m_paperHeight, tileSizes);

  const auto mesh = CreatePaperMesh(ctx);
  const auto shader = LoadPaperShader(ctx);
  for (const utils::ArticlePaperStrip &strip : strips) {
    const ecs::Entity entity = m_entityOwner.Create(ctx.world);
    auto &transform = ctx.world.Add<components::Transform>(entity);
    transform.scale = {m_paperWidth, strip.height, 1.0f};

    auto &renderer = ctx.world.Add<components::MeshRenderer>(entity);
    renderer.mesh = mesh;
    renderer.shader = shader;
    if (strip.tileIndex >= 0) {
      renderer.textureSRV = setup.tiles[strip.tileIndex].texture;
      renderer.hasTexture = true;
    }
    renderer.isTransparent = true;
    renderer.blendMode = components::BlendMode::Alpha;
    // TexturedVS の FadeFactor と EffectIntensity へ、切り出し範囲と余白を渡す
    renderer.customFlags = {strip.verticalFraction,
                            utils::kArticlePaperSideMargin, 0.0f, 0.0f};
    m_strips.push_back({entity, {0.0f, strip.bottomOffset, 0.0f}});
  }

  Update(ctx, 0.0f, 1.0f);
  return true;
}

ArticlePaperEffect::Anchor
ArticlePaperEffect::ResolveScreenPoint(float virtualX, float virtualY,
                                       float forward) const {
  Anchor anchor;
  if (m_distance <= 0.0f) {
    return anchor;
  }
  // 紙はカメラと正対しているので、画面上の位置は紙の上で線形に対応する。
  // カメラ側へ浮かせた物は、同じ画面位置に見えるよう中心へ寄せて縮める
  const float depthScale = std::max(0.05f, 1.0f - forward / m_distance);
  const float onPaperX =
      (virtualX / kVirtualWidth - 0.5f) * m_visibleWidth;
  const float onPaperY =
      m_paperHeight * 0.5f +
      (0.5f - virtualY / kVirtualHeight) * m_visibleHeight;

  // 紙の中心からの距離を縮め、カメラ側へ forward だけ寄せる
  const float centerY = m_paperHeight * 0.5f;
  anchor.local = {onPaperX * depthScale,
                  centerY + (onPaperY - centerY) * depthScale, forward};
  anchor.metersPerPixel =
      m_visibleHeight / kVirtualHeight * depthScale;
  return anchor;
}

void ArticlePaperEffect::AddBand(core::GameContext &ctx, float virtualTop,
                                 float virtualBottom,
                                 const DirectX::XMFLOAT3 &color,
                                 float forward) {
  if (m_strips.empty()) {
    return;
  }
  const Anchor bottom = ResolveScreenPoint(
      kVirtualWidth * 0.5f, virtualBottom, forward);
  const Anchor top = ResolveScreenPoint(
      kVirtualWidth * 0.5f, virtualTop, forward);

  const ecs::Entity entity = m_entityOwner.Create(ctx.world);
  auto &transform = ctx.world.Add<components::Transform>(entity);
  // 帯は画面の幅いっぱいに敷く（紙のはみ出し分も含める）
  transform.scale = {m_paperWidth, top.local.y - bottom.local.y, 1.0f};
  auto &renderer = ctx.world.Add<components::MeshRenderer>(entity);
  // Spawn で作ったメッシュを使う（同名で作り直すとキャッシュが置き換わるため）
  renderer.mesh = ctx.resource.FindMesh("ArticlePaper");
  renderer.shader = LoadPaperShader(ctx);
  renderer.color = {color.x, color.y, color.z, 1.0f};
  renderer.isTransparent = true;
  renderer.blendMode = components::BlendMode::Alpha;
  // FadeFactor 0 は記事を貼らない白紙。色は color で付ける
  renderer.customFlags = {0.0f, 0.0f, 0.0f, 0.0f};
  m_strips.push_back({entity, bottom.local});
}

void ArticlePaperEffect::Attach(ecs::Entity entity,
                                const DirectX::XMFLOAT3 &local) {
  m_attached.push_back({entity, local});
}

void ArticlePaperEffect::SetAttachedLocal(ecs::Entity entity,
                                          const DirectX::XMFLOAT3 &local) {
  for (AttachedEntity &attached : m_attached) {
    if (attached.entity == entity) {
      attached.local = local;
      return;
    }
  }
}

DirectX::XMFLOAT3 ArticlePaperEffect::ToWorld(const DirectX::XMFLOAT3 &local,
                                              float angle) const {
  // X軸回りに angle 回した紙の右・上・カメラ側の向き
  const float upY = std::cos(angle);
  const float upZ = std::sin(angle);
  const float towardCameraY = std::sin(angle);
  const float towardCameraZ = -std::cos(angle);
  return {m_hinge.x + local.x,
          m_hinge.y + upY * local.y + towardCameraY * local.z,
          m_hinge.z + upZ * local.y + towardCameraZ * local.z};
}

void ArticlePaperEffect::Update(core::GameContext &ctx, float fallProgress,
                                float opacity) {
  const float angle = utils::CalculatePaperAngle(fallProgress, m_standAngle);
  DirectX::XMFLOAT4 rotation;
  DirectX::XMStoreFloat4(
      &rotation, DirectX::XMQuaternionRotationAxis(
                     DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), angle));

  for (const StripEntity &strip : m_strips) {
    if (auto *transform = ctx.world.Get<components::Transform>(strip.entity)) {
      transform->rotation = rotation;
      transform->position = ToWorld(strip.local, angle);
    }
    if (auto *renderer =
            ctx.world.Get<components::MeshRenderer>(strip.entity)) {
      renderer->color.w = opacity;
      renderer->isVisible = opacity > 0.01f;
    }
  }
  for (const AttachedEntity &attached : m_attached) {
    if (!ctx.world.IsAlive(attached.entity)) {
      continue;
    }
    if (auto *transform =
            ctx.world.Get<components::Transform>(attached.entity)) {
      transform->rotation = rotation;
      transform->position = ToWorld(attached.local, angle);
    }
    if (auto *renderer =
            ctx.world.Get<components::MeshRenderer>(attached.entity)) {
      renderer->color.w = opacity;
    }
  }
}

void ArticlePaperEffect::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_strips.clear();
  m_attached.clear();
  m_distance = 0.0f;
}

} // namespace game::controllers

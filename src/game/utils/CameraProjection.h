#pragma once
/**
 * @file CameraProjection.h
 * @brief 仮想解像度(UI)座標と3Dカメラ投影との相互変換
 * @details UI・マウス座標は仮想解像度(1280x720)を縦横同一倍率で拡大し
 *          余白を中央寄せ(レターボックス/ピラーボックス)した座標系で扱う
 *          のに対し、3D描画はクライアント領域いっぱいのビューポートで行う。
 *          この2つを取り違えると、16:9以外のウィンドウでは横方向に無視でき
 *          ないズレが出る（画面端ほど大きい）。さらに投影行列のアスペクト比は
 *          RenderSystemと同様に「現在の実描画サイズ」から都度作らないと、
 *          ウィンドウリサイズ後にカメラの保存値が古いままズレる。
 *          変換規則をここへ一本化し、レイキャスト（画面→ワールド）と
 *          マーカー配置（ワールド→画面）の両方から使う。
*/

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../../ecs/World.h"
#include "../../graphics/GraphicsDevice.h"
#include "../components/Camera.h"
#include "../components/Transform.h"
#include <DirectXMath.h>
#include <algorithm>

namespace game::utils {

/** @brief UIの仮想解像度基準値（TextRenderer/Inputと同じ値）*/
constexpr float kVirtualScreenWidth = 1280.0f;
constexpr float kVirtualScreenHeight = 720.0f;

/**
 * @brief 仮想解像度座標と実クライアント座標(ピクセル)の対応付け。
 * @details client = offset + virtual * scale。TextRenderer::
 *          ComputeVirtualToScreenTransform()と同じ式。
*/
struct ViewportMapping {
  float clientWidth = kVirtualScreenWidth;
  float clientHeight = kVirtualScreenHeight;
  float scale = 1.0f;
  float offsetX = 0.0f;
  float offsetY = 0.0f;

  static ViewportMapping FromClientSize(float width, float height) {
    ViewportMapping mapping;
    if (width <= 0.0f || height <= 0.0f) {
      return mapping;
    }
    mapping.clientWidth = width;
    mapping.clientHeight = height;
    mapping.scale = (std::min)(width / kVirtualScreenWidth,
                               height / kVirtualScreenHeight);
    if (mapping.scale <= 0.0f) {
      mapping.scale = 1.0f;
    }
    mapping.offsetX = (width - kVirtualScreenWidth * mapping.scale) * 0.5f;
    mapping.offsetY = (height - kVirtualScreenHeight * mapping.scale) * 0.5f;
    return mapping;
  }

  DirectX::XMFLOAT2 VirtualToClient(float vx, float vy) const {
    return {offsetX + vx * scale, offsetY + vy * scale};
  }

  DirectX::XMFLOAT2 ClientToVirtual(float cx, float cy) const {
    return {(cx - offsetX) / scale, (cy - offsetY) / scale};
  }

  float AspectRatio() const { return clientWidth / clientHeight; }
};

/** @brief ワールド空間のレイ（始点と正規化方向）*/
struct ScreenRay {
  DirectX::XMFLOAT3 origin{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 direction{0.0f, 0.0f, 1.0f};
};

/**
 * @brief 仮想解像度座標から、そのピクセルを通るワールド空間のレイを作ります。
*/
inline ScreenRay BuildRayFromVirtualScreen(const ViewportMapping &mapping,
                                           const DirectX::XMMATRIX &view,
                                           const DirectX::XMMATRIX &proj,
                                           float virtualX, float virtualY) {
  using namespace DirectX;

  const XMFLOAT2 client = mapping.VirtualToClient(virtualX, virtualY);
  const XMMATRIX world = XMMatrixIdentity();

  const XMVECTOR nearPoint = XMVector3Unproject(
      XMVectorSet(client.x, client.y, 0.0f, 0.0f), 0.0f, 0.0f,
      mapping.clientWidth, mapping.clientHeight, 0.0f, 1.0f, proj, view, world);
  const XMVECTOR farPoint = XMVector3Unproject(
      XMVectorSet(client.x, client.y, 1.0f, 0.0f), 0.0f, 0.0f,
      mapping.clientWidth, mapping.clientHeight, 0.0f, 1.0f, proj, view, world);

  ScreenRay ray;
  XMStoreFloat3(&ray.origin, nearPoint);
  XMStoreFloat3(&ray.direction,
                XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint)));
  return ray;
}

/**
 * @brief ワールド座標をUI配置用の仮想解像度座標へ投影します。
 * @return カメラの前方かつ描画範囲内ならtrue
*/
inline bool ProjectWorldToVirtualScreen(const ViewportMapping &mapping,
                                        const DirectX::XMMATRIX &view,
                                        const DirectX::XMMATRIX &proj,
                                        const DirectX::XMFLOAT3 &worldPos,
                                        float &outVirtualX,
                                        float &outVirtualY) {
  using namespace DirectX;

  const XMVECTOR projected = XMVector3Project(
      XMLoadFloat3(&worldPos), 0.0f, 0.0f, mapping.clientWidth,
      mapping.clientHeight, 0.0f, 1.0f, proj, view, XMMatrixIdentity());

  XMFLOAT3 client;
  XMStoreFloat3(&client, projected);
  if (client.z < 0.0f || client.z > 1.0f) {
    return false; // カメラの後方、またはFar超え
  }

  const XMFLOAT2 virtualPos = mapping.ClientToVirtual(client.x, client.y);
  outVirtualX = virtualPos.x;
  outVirtualY = virtualPos.y;
  return true;
}

/** @brief 現在の実描画サイズから仮想解像度との対応付けを作ります。*/
inline ViewportMapping GetViewportMapping(core::GameContext &ctx) {
  return ViewportMapping::FromClientSize(
      static_cast<float>(ctx.graphics.GetWidth()),
      static_cast<float>(ctx.graphics.GetHeight()));
}

/**
 * @brief 実描画に使われているビュー/投影行列を取得します。
 * @details アスペクト比はカメラコンポーネントの保存値ではなく現在の実描画
 *          サイズから作る（RenderSystemと同じ扱い）。
 * @return カメラEntityがTransform/Cameraを持っていればtrue
*/
inline bool GetCameraMatrices(core::GameContext &ctx, ecs::Entity cameraEntity,
                              DirectX::XMMATRIX &outView,
                              DirectX::XMMATRIX &outProj) {
  auto *transform = ctx.world.Get<game::components::Transform>(cameraEntity);
  auto *camera = ctx.world.Get<game::components::Camera>(cameraEntity);
  if (!transform || !camera) {
    return false;
  }
  outView = camera->GetViewMatrix(*transform);
  outProj = DirectX::XMMatrixPerspectiveFovLH(
      camera->fov, ctx.graphics.GetAspectRatio(), camera->nearZ, camera->farZ);
  return true;
}

/** @brief ワールド座標をUI配置用の仮想解像度座標へ投影します（ctx版）。*/
inline bool ProjectWorldToVirtualScreen(core::GameContext &ctx,
                                        ecs::Entity cameraEntity,
                                        const DirectX::XMFLOAT3 &worldPos,
                                        float &outVirtualX,
                                        float &outVirtualY) {
  DirectX::XMMATRIX view;
  DirectX::XMMATRIX proj;
  if (!GetCameraMatrices(ctx, cameraEntity, view, proj)) {
    return false;
  }
  return ProjectWorldToVirtualScreen(GetViewportMapping(ctx), view, proj,
                                     worldPos, outVirtualX, outVirtualY);
}

} // namespace game::utils

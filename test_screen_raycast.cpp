/**
 * @file test_screen_raycast.cpp
 * @brief 画面座標→ワールドのレイ構築と、地形へのレイマーチングを検証します。
 */

#include "src/game/utils/ScreenRaycast.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK_TRUE(condition, message)                                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                               \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                 \
  } while (0)

#define CHECK_CLOSE(actual, expected, eps, message)                            \
  do {                                                                         \
    if (std::fabs((actual) - (expected)) > (eps)) {                            \
      std::cerr << "[FAIL] " << message << " (expected " << (expected)         \
                << ", got " << (actual) << ")\n";                              \
      std::exit(1);                                                            \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                                 \
  } while (0)

namespace {

using namespace DirectX;
using game::utils::HeightFieldBounds;
using game::utils::ViewportMapping;

/** @brief 全域を覆う十分広い探索範囲*/
HeightFieldBounds WideBounds() {
  return HeightFieldBounds{-500.0f, 500.0f, -500.0f, 500.0f};
}

/** @brief 高さ0の平面*/
float FlatGround(float, float) { return 0.0f; }

/** @brief +Z方向へ上っていく斜面*/
float Slope(float, float z) { return z * 0.25f; }

void TestViewportMapping() {
  // 16:9ちょうどなら仮想座標と実座標は単純な等倍スケール。
  {
    const auto mapping = ViewportMapping::FromClientSize(2560.0f, 1440.0f);
    const auto client = mapping.VirtualToClient(1280.0f, 720.0f);
    CHECK_CLOSE(client.x, 2560.0f, 0.01f, "16:9 maps right edge to right edge");
    CHECK_CLOSE(client.y, 1440.0f, 0.01f, "16:9 maps bottom edge to bottom");
  }

  // 16:9より横長（ピラーボックス）では、UIは中央の16:9領域にだけ乗る。
  // 実描画は画面いっぱいなので、仮想座標のまま逆投影すると横にずれる。
  {
    const auto mapping = ViewportMapping::FromClientSize(1920.0f, 1041.0f);
    const auto right = mapping.VirtualToClient(1280.0f, 0.0f);
    CHECK_TRUE(right.x < 1920.0f,
               "pillarboxed layout keeps a margin at the right edge");
    const auto roundTrip = mapping.ClientToVirtual(right.x, right.y);
    CHECK_CLOSE(roundTrip.x, 1280.0f, 0.01f,
                "virtual -> client -> virtual round trips");
  }

  // 縦長（レターボックス）でも中央基準は保たれる。
  {
    const auto mapping = ViewportMapping::FromClientSize(1280.0f, 1024.0f);
    const auto center = mapping.VirtualToClient(640.0f, 360.0f);
    CHECK_CLOSE(center.x, 640.0f, 0.01f, "letterboxed center stays centered X");
    CHECK_CLOSE(center.y, 512.0f, 0.01f, "letterboxed center stays centered Y");
  }
}

void TestScreenRay() {
  // 原点から+Zを向き、少し見下ろしていないカメラ。
  const XMMATRIX view = XMMatrixLookToLH(XMVectorSet(0.0f, 10.0f, 0.0f, 1.0f),
                                         XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
                                         XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

  // 非16:9のウィンドウ。投影行列も実アスペクト比で作る（RenderSystemと同じ）。
  const float clientWidth = 1920.0f;
  const float clientHeight = 1041.0f;
  const auto mapping = ViewportMapping::FromClientSize(clientWidth, clientHeight);
  const XMMATRIX proj = XMMatrixPerspectiveFovLH(
      XM_PIDIV4, mapping.AspectRatio(), 0.01f, 1000.0f);

  // 画面中央はカメラの正面。
  {
    const auto ray =
        game::utils::BuildRayFromVirtualScreen(mapping, view, proj, 640.0f, 360.0f);
    CHECK_CLOSE(ray.direction.x, 0.0f, 0.001f, "screen center looks straight X");
    CHECK_CLOSE(ray.direction.y, 0.0f, 0.001f, "screen center looks straight Y");
    CHECK_CLOSE(ray.direction.z, 1.0f, 0.001f, "screen center looks forward");
  }

  // 投影して戻すと元の仮想座標に一致する（レイキャストとマーカー配置の整合）。
  {
    const XMFLOAT3 worldPos{3.0f, 10.0f, 25.0f};
    float virtualX = 0.0f;
    float virtualY = 0.0f;
    CHECK_TRUE(game::utils::ProjectWorldToVirtualScreen(
                   mapping, view, proj, worldPos, virtualX, virtualY),
               "a point in front of the camera projects into the view");

    const auto ray = game::utils::BuildRayFromVirtualScreen(mapping, view, proj,
                                                            virtualX, virtualY);
    // レイ上にその点が乗っているか（始点からの方向が一致するか）で確認する。
    const XMVECTOR toPoint = XMVector3Normalize(XMVectorSubtract(
        XMLoadFloat3(&worldPos), XMLoadFloat3(&ray.origin)));
    const float alignment = XMVectorGetX(
        XMVector3Dot(toPoint, XMLoadFloat3(&ray.direction)));
    CHECK_CLOSE(alignment, 1.0f, 0.001f,
                "unprojected ray passes through the projected point");
  }
}

void TestMarching() {
  XMFLOAT3 hit{0.0f, 0.0f, 0.0f};

  // 見下ろしたレイは平面と交差する。
  {
    const XMFLOAT3 origin{0.0f, 10.0f, 0.0f};
    XMFLOAT3 direction{0.0f, -1.0f, 1.0f};
    XMStoreFloat3(&direction, XMVector3Normalize(XMLoadFloat3(&direction)));
    CHECK_TRUE(game::utils::MarchRayToHeightField(origin, direction, FlatGround,
                                                  WideBounds(), 1000.0f, 0.5f,
                                                  hit),
               "downward ray hits flat ground");
    CHECK_CLOSE(hit.z, 10.0f, 0.05f, "flat ground hit lands at the right spot");
    CHECK_CLOSE(hit.y, 0.0f, 0.01f, "flat ground hit sits on the surface");
  }

  // 水平よりわずかに上向きでも、せり上がる地形とは交差する（遠方の丘を狙う）。
  {
    const XMFLOAT3 origin{0.0f, 1.0f, 0.0f};
    XMFLOAT3 direction{0.0f, 0.02f, 1.0f};
    XMStoreFloat3(&direction, XMVector3Normalize(XMLoadFloat3(&direction)));
    CHECK_TRUE(game::utils::MarchRayToHeightField(origin, direction, Slope,
                                                  WideBounds(), 1000.0f, 0.5f,
                                                  hit),
               "slightly upward ray still hits a rising slope");
    // y = 1 + 0.02z, 地表 = 0.25z が一致する点 -> z ≈ 4.35
    CHECK_CLOSE(hit.z, 4.348f, 0.05f, "rising slope hit solved accurately");
  }

  // 空へ抜けるレイは交差しない。
  {
    const XMFLOAT3 origin{0.0f, 10.0f, 0.0f};
    XMFLOAT3 direction{0.0f, 1.0f, 0.2f};
    XMStoreFloat3(&direction, XMVector3Normalize(XMLoadFloat3(&direction)));
    CHECK_TRUE(!game::utils::MarchRayToHeightField(origin, direction, FlatGround,
                                                   WideBounds(), 1000.0f, 0.5f,
                                                   hit),
               "ray into the sky reports no hit");
  }

  // フィールド外へ抜けるレイは、範囲外の地表（高さ未定義）に刺さらない。
  {
    const HeightFieldBounds narrow{-5.0f, 5.0f, -5.0f, 5.0f};
    const XMFLOAT3 origin{0.0f, 10.0f, 0.0f};
    XMFLOAT3 direction{0.0f, -1.0f, 20.0f};
    XMStoreFloat3(&direction, XMVector3Normalize(XMLoadFloat3(&direction)));
    CHECK_TRUE(!game::utils::MarchRayToHeightField(origin, direction, FlatGround,
                                                   narrow, 1000.0f, 0.5f, hit),
               "ray leaving the field does not hit outside terrain bounds");
  }

  // 範囲外から入ってくるレイ（マップビューでカメラが場外にある場合）も拾える。
  {
    const HeightFieldBounds field{-50.0f, 50.0f, -50.0f, 50.0f};
    const XMFLOAT3 origin{0.0f, 60.0f, -120.0f};
    XMFLOAT3 direction{0.0f, -0.5f, 1.0f};
    XMStoreFloat3(&direction, XMVector3Normalize(XMLoadFloat3(&direction)));
    CHECK_TRUE(game::utils::MarchRayToHeightField(origin, direction, FlatGround,
                                                  field, 1000.0f, 0.5f, hit),
               "ray entering the field from outside still hits");
    CHECK_CLOSE(hit.z, 0.0f, 0.05f, "entering ray hits at the right spot");
  }

  // カメラが斜面へめり込んでいても、地表より上に出てから交差判定する。
  {
    const XMFLOAT3 origin{0.0f, -2.0f, 0.0f}; // 地表(0)より下から出発
    XMFLOAT3 direction{0.0f, 1.0f, 4.0f};
    XMStoreFloat3(&direction, XMVector3Normalize(XMLoadFloat3(&direction)));
    CHECK_TRUE(!game::utils::MarchRayToHeightField(origin, direction, FlatGround,
                                                   WideBounds(), 100.0f, 0.5f,
                                                   hit),
               "ray starting underground does not report a bogus hit");
  }
}

} // namespace

int main() {
  TestViewportMapping();
  TestScreenRay();
  TestMarching();
  std::cout << "All screen raycast tests passed.\n";
  return 0;
}

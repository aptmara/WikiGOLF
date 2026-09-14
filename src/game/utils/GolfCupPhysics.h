#pragma once
/**
 * @file GolfCupPhysics.h
 * @brief カップ（穴）の形状定義と、カップ内部にある球の接触計算
 * @details 地形の穴抜き描画・カップ内壁メッシュ・物理・カップイン判定が
 *          すべて同じ寸法を参照するよう、ここに集約する。
*/

#include "GameplayPhysicsConstants.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::physics {

/** @brief カップ開口部の見た目上の半径です。地形の穴抜き・内壁メッシュと一致します。*/
inline constexpr float kGolfCupRadius = 0.25f;
/** @brief カップの見た目上の深さ（縁から底まで）です。*/
inline constexpr float kGolfCupDepth = 0.34f;
/**
 * @brief 見た目のボール半径と当たり判定半径の差です。
 * @details ボールは当たり判定より大きく描画されるため、カップ内の壁・底・ピンを
 *          この分だけ内側に寄せ、見た目上めり込まないようにします。
*/
inline constexpr float kGolfCupBallVisualPadding =
    kBallVisualScale * 0.5f - kBallRadius;
/** @brief 内壁・縁に当たったときの反発係数です。*/
inline constexpr float kGolfCupWallRestitution = 0.18f;
/** @brief 内壁・縁・ピンに当たったときに残る接線速度の割合です。*/
inline constexpr float kGolfCupWallTangentialRetention = 0.72f;
/** @brief カップ底に当たったときの反発係数です。*/
inline constexpr float kGolfCupFloorRestitution = 0.05f;
/** @brief カップ底からこの高さ以内にボール下端があればカップインとみなします。*/
inline constexpr float kGolfCupInFloorTolerance = 0.03f;
/** @brief カップ判定を行う縦方向の余裕です（縁より上側）。*/
inline constexpr float kGolfCupVerticalQueryAbove = 6.0f;

/**
 * @brief 1つのカップの形状です（寸法は見た目基準）。
*/
struct GolfCupShape {
  float centerX = 0.0f;
  float centerZ = 0.0f;
  float rimY = 0.0f;               ///< 縁（地表）の高さ
  float radius = kGolfCupRadius;   ///< 開口部の見た目半径
  float depth = kGolfCupDepth;     ///< 見た目の深さ
  float pinRadius = 0.0f;          ///< ピンの見た目半径（0ならピン無し）
  float pinHeight = 0.0f;          ///< 縁から上のピンの高さ
};

/** @brief 当たり判定上の開口半径を返します。*/
inline float PhysicsCupRadius(const GolfCupShape &cup) {
  return (std::max)(cup.radius - kGolfCupBallVisualPadding, 0.01f);
}

/** @brief 当たり判定上の底の高さを返します。*/
inline float PhysicsCupFloorY(const GolfCupShape &cup) {
  return cup.rimY - (std::max)(cup.depth - kGolfCupBallVisualPadding, 0.01f);
}

/** @brief 当たり判定上のピン半径を返します（ピン無しなら0）。*/
inline float PhysicsCupPinRadius(const GolfCupShape &cup) {
  return cup.pinRadius > 0.0f ? cup.pinRadius + kGolfCupBallVisualPadding
                              : 0.0f;
}

/** @brief 中心からの水平距離を返します。*/
inline float HorizontalDistanceToCup(const GolfCupShape &cup, float x,
                                     float z) {
  const float dx = x - cup.centerX;
  const float dz = z - cup.centerZ;
  return std::sqrt(dx * dx + dz * dz);
}

/**
 * @brief 球の中心がカップ開口部の真上（または内部）にあるかを返します。
 * @details 真上にある間は地形の支えが無く、代わりにカップの接触で支えます。
*/
inline bool IsOverCupOpening(const GolfCupShape &cup, float x, float y,
                             float z) {
  if (y < PhysicsCupFloorY(cup) - 1.0f ||
      y > cup.rimY + kGolfCupVerticalQueryAbove) {
    return false;
  }
  return HorizontalDistanceToCup(cup, x, z) < PhysicsCupRadius(cup);
}

/** @brief カップ接触の種類です。*/
enum class GolfCupContactKind { Floor, Wall, Rim, Pin };

/** @brief カップ内部の球に働く接触1件です。*/
struct GolfCupContact {
  GolfCupContactKind kind = GolfCupContactKind::Floor;
  DirectX::XMFLOAT3 normal = {0.0f, 1.0f, 0.0f}; ///< 球を押し戻す向き
  float penetration = 0.0f;                      ///< めり込み量（正）
};

/**
 * @brief カップ内部側にある球（中心 p・半径 r）の接触を求めます。
 * @details
 * - 底: 水平な床。
 * - 内壁: 縁より下では、中心から外へ向かう水平方向の壁。中心が開口半径の
 *   外に抜けていても（高速時のすり抜け）内側へ押し戻す。
 * - 縁: 縁より上で開口部の真上にある場合、縁の円周との接触。ボールが
 *   縁に乗ると内側下向きへ転がり落ちる。
 * - ピン: 中心の細い柱。
 * @param outContacts 出力先（4件分の領域が必要）
 * @return 接触件数
*/
inline int ComputeCupInteriorContacts(const GolfCupShape &cup,
                                      const DirectX::XMFLOAT3 &p, float r,
                                      GolfCupContact *outContacts) {
  int count = 0;
  const float cupRadius = PhysicsCupRadius(cup);
  const float floorY = PhysicsCupFloorY(cup);
  const float dx = p.x - cup.centerX;
  const float dz = p.z - cup.centerZ;
  const float d = std::sqrt(dx * dx + dz * dz);
  const bool hasDirection = d > 1.0e-5f;
  const float dirX = hasDirection ? dx / d : 1.0f;
  const float dirZ = hasDirection ? dz / d : 0.0f;

  const float floorPenetration = floorY + r - p.y;
  if (floorPenetration > 0.0f) {
    GolfCupContact &c = outContacts[count++];
    c.kind = GolfCupContactKind::Floor;
    c.normal = {0.0f, 1.0f, 0.0f};
    c.penetration = floorPenetration;
  }

  if (p.y <= cup.rimY) {
    const float wallPenetration = d - (cupRadius - r);
    if (wallPenetration > 0.0f) {
      GolfCupContact &c = outContacts[count++];
      c.kind = GolfCupContactKind::Wall;
      c.normal = {-dirX, 0.0f, -dirZ};
      c.penetration = wallPenetration;
    }
  } else if (d < cupRadius) {
    const float inward = cupRadius - d;
    const float above = p.y - cup.rimY;
    const float edgeDistance = std::sqrt(inward * inward + above * above);
    if (edgeDistance < r && edgeDistance > 1.0e-6f) {
      GolfCupContact &c = outContacts[count++];
      c.kind = GolfCupContactKind::Rim;
      c.normal = {-dirX * inward / edgeDistance, above / edgeDistance,
                  -dirZ * inward / edgeDistance};
      c.penetration = r - edgeDistance;
    }
  }

  const float pinRadius = PhysicsCupPinRadius(cup);
  if (pinRadius > 0.0f && p.y < cup.rimY + cup.pinHeight) {
    const float pinPenetration = pinRadius + r - d;
    if (pinPenetration > 0.0f) {
      GolfCupContact &c = outContacts[count++];
      c.kind = GolfCupContactKind::Pin;
      c.normal = {dirX, 0.0f, dirZ};
      c.penetration = pinPenetration;
    }
  }
  return count;
}

/**
 * @brief 球がカップ底に接している（接地マージン内）かを返します。
*/
inline bool IsRestingOnCupFloor(const GolfCupShape &cup,
                                const DirectX::XMFLOAT3 &p, float r) {
  const float gap = p.y - r - PhysicsCupFloorY(cup);
  return gap < (std::max)(0.002f, r * 0.15f);
}

/**
 * @brief ボールがカップの底まで落ち切ったか（カップイン成立）を返します。
*/
inline bool IsBallSettledInCup(const GolfCupShape &cup,
                               const DirectX::XMFLOAT3 &p, float r) {
  if (HorizontalDistanceToCup(cup, p.x, p.z) >= PhysicsCupRadius(cup)) {
    return false;
  }
  const float bottom = p.y - r;
  const float floorY = PhysicsCupFloorY(cup);
  return bottom >= floorY - 0.05f &&
         bottom <= floorY + kGolfCupInFloorTolerance;
}

} // namespace game::physics

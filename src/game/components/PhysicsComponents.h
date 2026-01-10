#pragma once
/**
 * @file PhysicsComponents.h
 * @brief 物理演算に関連するコンポーネント定義
 */

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace game::components {

/**
 * @brief 剛体コンポーネント
 *
 * 物理演算（移動、衝突応答）の対象となるエンティティに付与します。
 */
struct RigidBody {
  DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};     ///< 現在の速度ベクトル
  DirectX::XMFLOAT3 acceleration = {0.0f, 0.0f, 0.0f}; ///< 現在の加速度ベクトル
  float mass = 1.0f;                                   ///< 質量 (kg想定)
  float drag = 0.01f;           ///< 空気抵抗係数 (0.0 - 1.0)
  float rollingFriction = 0.5f; ///< 転がり抵抗係数 (接地時の減速)
  float restitution = 0.5f;     ///< 反発係数 (0.0: 非弾性 - 1.0: 完全弾性)
  DirectX::XMFLOAT3 angularVelocity = {0.0f, 0.0f,
                                       0.0f}; ///< 角速度ベクトル (rad/s)
  float spinDecay = 0.5f; ///< スピン減衰係数 (空気抵抗による減速)
  bool isStatic =
      false; ///< 静的オブジェクトフラグ (trueの場合、物理演算で移動しない)
};

/**
 * @brief コライダーの種類
 */
enum class ColliderType {
  Sphere,  ///< 球体コライダー
  Box,     ///< 矩形（AABB）コライダー
  Cylinder ///< 円柱コライダー
};

/**
 * @brief 衝突判定用コンポーネント
 */
struct Collider {
  ColliderType type = ColliderType::Sphere; ///< コライダーの形状
  float radius = 0.5f;                      ///< 半径 (Sphere用)
  DirectX::XMFLOAT3 size = {1.0f, 1.0f,
                            1.0f}; ///< ハーフサイズ (Box用の各軸の半分の長さ)
  DirectX::XMFLOAT3 offset = {0.0f, 0.0f, 0.0f}; ///< 中心からのオフセット位置
};

/**
 * @brief 衝突イベント
 */
struct CollisionEvent {
  uint32_t entityA;
  uint32_t entityB;
};

/**
 * @brief フレームごとの衝突イベントリスト (Global Resource)
 */
struct CollisionEvents {
  std::vector<CollisionEvent> events;
};

} // namespace game::components

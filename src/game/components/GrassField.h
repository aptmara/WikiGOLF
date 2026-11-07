#pragma once
/**
 * @file GrassField.h
 * @brief 手続き的に生やす芝の元データと、ボールによる踏み跡・衝撃の入力
 */

#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace game::components {

/** @brief 層ごとのタイル情報（x: 最低の高さ、y: 最高の高さ、z: 層の最大密度、w: 予備） */
struct GrassTileGrid {
  float tileSize = 1.0f;
  int resolutionX = 0;
  int resolutionZ = 0;
  std::vector<DirectX::XMFLOAT4> tiles;
};

/**
 * @brief コース構築時に地形から作る芝の元データ
 * @details テクスチャの向きは地形の高さマップと同じ（行 0 がフィールドの奥 = z 最大）。
 *          タイルはフィールドの最小の角を原点に、z が増える向きに並ぶ。
 */
struct GrassFieldData {
  float minX = 0.0f;
  float minZ = 0.0f;
  float width = 0.0f;
  float depth = 0.0f;
  /** 地形の高さに足すワールド y */
  float heightOffset = 0.0f;
  int heightResolutionX = 0;
  int heightResolutionZ = 0;
  std::vector<float> heights;
  int fieldResolutionX = 0;
  int fieldResolutionZ = 0;
  /** RGBA8（R: ラフの丈係数、G: 短い芝の重み、B: グリーンの重み、A: 生えてよい度合い） */
  std::vector<uint32_t> fieldTexels;
  GrassTileGrid layers[2];
};

/** @brief ECS グローバル。version が変わると GPU 資源を作り直す */
struct GrassFieldSource {
  uint64_t version = 0;
  std::shared_ptr<const GrassFieldData> data;
};

/** @brief 芝の踏み跡を付けるボールの状態（毎フレーム更新する ECS グローバル） */
struct GrassTrampleInput {
  bool touching = false;
  DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT2 velocity = {0.0f, 0.0f};
  /** ボールが空中にいるか（着地の瞬間を見分ける） */
  bool airborne = false;
  /** 空中で最後に測った落下の速さ[m/s] */
  float fallSpeed = 0.0f;
};

/** @brief 芝を揺らす出来事の種類（GrassFieldVS.hlsl の GRASS_IMPULSE_* と同じ値） */
enum class GrassImpulseKind : uint32_t {
  Swing = 0,   /**< ショット：ボールの周りの葉が打つ向きへなびく */
  Landing = 1, /**< 着地：落ちた所から外へ押し倒す輪が広がる */
  CupIn = 2,   /**< カップイン：カップから波が何重にも広がる */
};

/** @brief 芝を揺らす出来事1つ */
struct GrassImpulse {
  GrassImpulseKind kind = GrassImpulseKind::Landing;
  DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
  /** なびく向き（水平、Swing のみ） */
  DirectX::XMFLOAT2 direction = {0.0f, 0.0f};
  /** 強さ（0〜1） */
  float strength = 1.0f;
  /** 起きた時刻（GameContext::time） */
  float startTime = 0.0f;
};

/** @brief 芝を揺らす出来事の一覧（ECS グローバル。いっぱいなら古いものから上書きする） */
struct GrassImpulseEvents {
  static constexpr uint32_t kCapacity = 8;
  std::array<GrassImpulse, kCapacity> items{};
  uint32_t count = 0;
  uint32_t next = 0;
};

} // namespace game::components

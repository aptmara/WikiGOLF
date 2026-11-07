#pragma once
/**
 * @file GrassFieldBuilder.h
 * @brief 地形の高さとマテリアルから、手続き的に生やす芝の元データを作る
 */

#include "../components/GrassField.h"
#include <cstdint>
#include <memory>

namespace game::systems {

/** @brief 地形データの読み取り用の参照（行 0 がフィールドの奥 = z 最大） */
struct GrassFieldTerrainView {
  const float *heights = nullptr;
  const uint8_t *materials = nullptr; /**< TerrainMaterial の値 */
  int resolutionX = 0;
  int resolutionZ = 0;
  float width = 0.0f;
  float depth = 0.0f;
};

struct GrassFieldBuildOptions {
  /** 地形の高さに足すワールド y */
  float heightOffset = 0.0f;
  /** 芝の生えるマテリアルを区別せず、全面をラフにする（タイトル画面用） */
  bool everythingIsRough = false;
};

/** @brief フィールドテクスチャの1テクセルの値（0〜1） */
struct GrassFieldTexel {
  float rough = 0.0f;   /**< ラフの丈係数（ラフ 1、セミラフ帯 0.55 以下） */
  float turf = 0.0f;    /**< 短い芝の重み */
  float green = 0.0f;   /**< グリーンの重み */
  float allowed = 0.0f; /**< 生えてよい度合い */
};

GrassFieldTexel DecodeGrassFieldTexel(uint32_t packed);

/** @brief ラフ層の密度（GrassFieldCommon.hlsli と同じ式） */
float GrassRoughLayerDensity(const GrassFieldTexel &texel);
/** @brief 短い芝の層の密度（GrassFieldCommon.hlsli と同じ式） */
float GrassTurfLayerDensity(const GrassFieldTexel &texel);

/** @brief セミラフの丈係数。フェアウェイ側で、ラフからの距離[m]に応じて下がる */
float GrassSemiRoughFactor(float distanceToRough);

/** @brief 地形から芝の元データを作る。地形が不正なら nullptr */
std::shared_ptr<components::GrassFieldData>
BuildGrassField(const GrassFieldTerrainView &terrain,
                const GrassFieldBuildOptions &options = {});

/** @brief ワールド座標のフィールドテクセルを読む（最近傍）。範囲外は 0 */
GrassFieldTexel SampleGrassFieldTexel(const components::GrassFieldData &field,
                                      float x, float z);

/** @brief シェーダーと同じ双線形補間で地面の高さを返す（heightOffset 込み） */
float SampleGrassFieldHeight(const components::GrassFieldData &field, float x,
                             float z);

} // namespace game::systems

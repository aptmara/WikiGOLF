#pragma once
/**
 * @file Entity.h
 * @brief エンティティID定義
 * 
 * 32bit Entity ID = [Generation (16bit)][Index (16bit)]
 * - Index: エンティティ配列のインデックス（0-65535）
 * - Generation: 再利用時のバージョン番号（0-65535）
 */

#include <cstdint>
#include <limits>

namespace ecs {

/// @brief エンティティID型（32bit）
using Entity = uint32_t;

/// @brief 無効なエンティティを表す定数
constexpr Entity NULL_ENTITY = (std::numeric_limits<Entity>::max)();

/// @brief エンティティIDからインデックス部分を取得
/// @param entity エンティティID
/// @return インデックス（下位16bit）
inline constexpr uint16_t GetEntityIndex(Entity entity) noexcept {
    return static_cast<uint16_t>(entity & 0xFFFF);
}

/// @brief エンティティIDからジェネレーション部分を取得
/// @param entity エンティティID
/// @return ジェネレーション（上位16bit）
inline constexpr uint16_t GetEntityGeneration(Entity entity) noexcept {
    return static_cast<uint16_t>(entity >> 16);
}

/// @brief インデックスとジェネレーションからエンティティIDを生成
/// @param index インデックス
/// @param generation ジェネレーション
/// @return エンティティID
inline constexpr Entity MakeEntity(uint16_t index, uint16_t generation) noexcept {
    return (static_cast<Entity>(generation) << 16) | static_cast<Entity>(index);
}

/// @brief エンティティが有効かどうかを判定
/// @param entity エンティティID
/// @return 有効ならtrue
inline constexpr bool IsValidEntity(Entity entity) noexcept {
    return entity != NULL_ENTITY;
}

} // namespace ecs

#pragma once
/**
 * @file Entity.h
 * @brief エンティティID定義
 *
 * 32bit Entity ID = [Generation (14bit)][Index (18bit)]
 * - Index: エンティティ配列のインデックス（0-262143）
 * - Generation: 再利用時のバージョン番号（0-16382）
 *
 * 0xFFFFFFFF は NULL_ENTITY として予約するため、Generation の全ビット1は
 * 使用しません。
*/

#include <cstdint>
#include <limits>

namespace ecs {

/** @brief エンティティID型（32bit）*/
using Entity = uint32_t;

/** @brief エンティティのIndex部分に割り当てるビット数*/
constexpr uint32_t ENTITY_INDEX_BITS = 18;
/** @brief エンティティのGeneration部分に割り当てるビット数*/
constexpr uint32_t ENTITY_GENERATION_BITS = 14;
/** @brief Index部分のビットマスク*/
constexpr uint32_t ENTITY_INDEX_MASK = (1u << ENTITY_INDEX_BITS) - 1u;
/** @brief Generation部分のビットマスク*/
constexpr uint32_t ENTITY_GENERATION_MASK =
    (1u << ENTITY_GENERATION_BITS) - 1u;
/** @brief 同時に確保できるEntityスロット数*/
constexpr uint32_t MAX_ENTITY_COUNT = ENTITY_INDEX_MASK + 1u;
/** @brief NULL_ENTITYとの衝突を避けるために使用可能な最大Generation*/
constexpr uint16_t MAX_ENTITY_GENERATION =
    static_cast<uint16_t>(ENTITY_GENERATION_MASK - 1u);

static_assert(ENTITY_INDEX_BITS + ENTITY_GENERATION_BITS == 32);

/** @brief 無効なエンティティを表す定数*/
constexpr Entity NULL_ENTITY = (std::numeric_limits<Entity>::max)();

/**
 * @brief エンティティIDからインデックス部分を取得
 * @param entity エンティティID
 * @return インデックス（下位18bit）
*/
inline constexpr uint32_t GetEntityIndex(Entity entity) noexcept {
    return entity & ENTITY_INDEX_MASK;
}

/**
 * @brief エンティティIDからジェネレーション部分を取得
 * @param entity エンティティID
 * @return ジェネレーション（上位14bit）
*/
inline constexpr uint16_t GetEntityGeneration(Entity entity) noexcept {
    return static_cast<uint16_t>((entity >> ENTITY_INDEX_BITS) &
                                 ENTITY_GENERATION_MASK);
}

/**
 * @brief インデックスとジェネレーションからエンティティIDを生成
 * @param index インデックス
 * @param generation ジェネレーション
 * @return エンティティID
*/
inline constexpr Entity MakeEntity(uint32_t index, uint16_t generation) noexcept {
    return (static_cast<Entity>(generation) << ENTITY_INDEX_BITS) |
           (index & ENTITY_INDEX_MASK);
}

/**
 * @brief エンティティが有効かどうかを判定
 * @param entity エンティティID
 * @return 有効ならtrue
*/
inline constexpr bool IsValidEntity(Entity entity) noexcept {
    return entity != NULL_ENTITY;
}

} // namespace ecs

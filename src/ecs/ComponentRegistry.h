#pragma once
/**
 * @file ComponentRegistry.h
 * @brief コンポーネント型をランタイムIDにマッピング
 * 
 * std::type_indexを使用せず、テンプレートインスタンス化による
 * 軽量なID生成を行う。
 */

#include <cstdint>
#include <atomic>

namespace ecs {

/// @brief コンポーネント型ID
using ComponentTypeId = uint32_t;

namespace detail {

/// @brief グローバルなコンポーネントID カウンター
inline std::atomic<ComponentTypeId> g_nextComponentId{0};

/// @brief 型ごとに一意のIDを生成するヘルパー
template<typename T>
struct ComponentTypeIdGenerator {
    static ComponentTypeId Id() noexcept {
        static const ComponentTypeId id = g_nextComponentId.fetch_add(1);
        return id;
    }
};

} // namespace detail

/// @brief コンポーネント型からIDを取得
/// @tparam T コンポーネント型
/// @return コンポーネント型ID
template<typename T>
inline ComponentTypeId GetComponentTypeId() noexcept {
    return detail::ComponentTypeIdGenerator<std::remove_cvref_t<T>>::Id();
}

} // namespace ecs

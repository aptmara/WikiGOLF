#pragma once
/**
 * @file ResourceHandle.h
 * @brief リソースハンドル定義
 */

#include <cstdint>
#include <limits>

namespace core {

/// @brief リソースハンドルの基底定数
constexpr uint32_t INVALID_RESOURCE_INDEX = (std::numeric_limits<uint32_t>::max)();

/// @brief 厳密な型チェック付きリソースハンドル
/// @tparam T リソース型（Mesh, Textureなど）
template<typename T>
struct ResourceHandle {
    uint32_t index;
    uint32_t generation;

    /// @brief 無効なハンドルを生成
    static ResourceHandle Invalid() {
        return { INVALID_RESOURCE_INDEX, 0 };
    }

    /// @brief ハンドルが有効かチェック（インデックスが無効値でないか）
    /// @note 注: これだけでは「実在するか」は保証されない。ResourceManagerでのチェックが必要。
    bool IsValid() const {
        return index != INVALID_RESOURCE_INDEX;
    }

    friend bool operator==(const ResourceHandle& lhs, const ResourceHandle& rhs) {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend bool operator!=(const ResourceHandle& lhs, const ResourceHandle& rhs) {
        return !(lhs == rhs);
    }
};

} // namespace core

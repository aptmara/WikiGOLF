#pragma once
/**
 * @file BrushCache.h
 * @brief SolidColorBrush のキャッシュ管理
 */

#include <d2d1.h>
#include <wrl/client.h>
#include <unordered_map>
#include <DirectXMath.h>

namespace graphics {

using Microsoft::WRL::ComPtr;

/// @brief D2D ブラシのキャッシュ
/// @details 同一色のブラシを使い回すことでパフォーマンスを向上
class BrushCache {
public:
    /// @brief 初期化
    /// @param target D2D レンダーターゲット
    void Initialize(ID2D1RenderTarget* target) {
        m_target = target;
        m_cache.clear();
    }

    /// @brief キャッシュクリア
    void Clear() {
        m_cache.clear();
        m_target = nullptr;
    }

    /// @brief 指定色のブラシを取得（キャッシュがなければ作成）
    /// @param color RGBA 色
    /// @return ブラシへのポインタ（失敗時 nullptr）
    ID2D1SolidColorBrush* GetBrush(const D2D1_COLOR_F& color) {
        if (!m_target) return nullptr;

        uint32_t key = ColorToKey(color);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            return it->second.Get();
        }

        // 新規作成
        ComPtr<ID2D1SolidColorBrush> brush;
        HRESULT hr = m_target->CreateSolidColorBrush(color, &brush);
        if (FAILED(hr)) {
            return nullptr;
        }

        m_cache[key] = brush;
        return brush.Get();
    }

    /// @brief DirectX::XMFLOAT4 から取得するオーバーロード
    ID2D1SolidColorBrush* GetBrush(const DirectX::XMFLOAT4& color) {
        D2D1_COLOR_F d2dColor = {color.x, color.y, color.z, color.w};
        return GetBrush(d2dColor);
    }

private:
    /// @brief RGBA を 32bit キーにエンコード
    static uint32_t ColorToKey(const D2D1_COLOR_F& color) {
        uint8_t r = static_cast<uint8_t>(color.r * 255.0f);
        uint8_t g = static_cast<uint8_t>(color.g * 255.0f);
        uint8_t b = static_cast<uint8_t>(color.b * 255.0f);
        uint8_t a = static_cast<uint8_t>(color.a * 255.0f);
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               static_cast<uint32_t>(b);
    }

    ID2D1RenderTarget* m_target = nullptr;
    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> m_cache;
};

} // namespace graphics

#pragma once
/**
 * @file UIText.h
 * @brief UIテキストコンポーネント（拡張版）
 */

#include <string>
#include <DirectXMath.h>
#include "../../graphics/TextStyle.h"

namespace game::components {

/// @brief UIテキストコンポーネント
/// @details 画面上にテキストを描画するためのデータ。レイヤーで描画順を制御可能。
struct UIText {
    std::wstring text;          ///< 表示テキスト
    float x = 0.0f;             ///< X座標（スクリーン座標）
    float y = 0.0f;             ///< Y座標（スクリーン座標）
    float width = 0.0f;         ///< 幅（0 = 画面幅を使用）
    float height = 0.0f;        ///< 高さ（0 = 自動）

    graphics::TextStyle style;  ///< テキストスタイル（色、サイズ、影など）

    bool visible = true;        ///< 可視性（false なら描画スキップ）
    int layer = 0;              ///< レイヤー（大きいほど前面に描画）

    /// @brief 簡易コンストラクタ
    static UIText Create(const std::wstring& text, float x, float y, const graphics::TextStyle& style = graphics::TextStyle::Default()) {
        UIText ui;
        ui.text = text;
        ui.x = x;
        ui.y = y;
        ui.style = style;
        return ui;
    }

    /// @brief FPS表示用プリセット
    static UIText FPS(float x = 10.0f, float y = 10.0f) {
        UIText ui;
        ui.text = L"FPS: --";
        ui.x = x;
        ui.y = y;
        ui.style = graphics::TextStyle::FPS();
        ui.layer = 100; // 最前面
        return ui;
    }
};

} // namespace game::components

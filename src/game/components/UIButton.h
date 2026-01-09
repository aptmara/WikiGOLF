#pragma once
/**
 * @file UIButton.h
 * @brief クリック可能なUIボタンコンポーネント
 */

#include <string>
#include <DirectXMath.h>
#include "../../graphics/TextStyle.h"

namespace game::components {

/// @brief UIボタンの状態
enum class ButtonState {
    Normal,
    Hovered,
    Pressed,
    Disabled
};

/// @brief UIボタンコンポーネント
struct UIButton {
    // ヒット領域
    float x = 0.0f;
    float y = 0.0f;
    float width = 200.0f;
    float height = 50.0f;

    // 状態
    ButtonState state = ButtonState::Normal;

    // スタイル（状態ごとの背景色）
    DirectX::XMFLOAT4 normalColor = {0.2f, 0.2f, 0.3f, 1.0f};
    DirectX::XMFLOAT4 hoverColor = {0.3f, 0.3f, 0.5f, 1.0f};
    DirectX::XMFLOAT4 pressedColor = {0.1f, 0.1f, 0.2f, 1.0f};
    DirectX::XMFLOAT4 disabledColor = {0.1f, 0.1f, 0.1f, 0.5f};

    // テキストラベル
    std::wstring label;
    graphics::TextStyle textStyle = graphics::TextStyle::Default();

    // クリック時のアクション識別子
    std::string action;

    // 可視性
    bool visible = true;

    /// @brief 現在の状態に応じた背景色を取得
    DirectX::XMFLOAT4 GetCurrentColor() const {
        switch (state) {
            case ButtonState::Hovered: return hoverColor;
            case ButtonState::Pressed: return pressedColor;
            case ButtonState::Disabled: return disabledColor;
            default: return normalColor;
        }
    }

    /// @brief ヘルパー: ボタン作成
    static UIButton Create(const std::wstring& label, const std::string& action,
                           float x, float y, float w = 200.0f, float h = 50.0f) {
        UIButton btn;
        btn.label = label;
        btn.action = action;
        btn.x = x;
        btn.y = y;
        btn.width = w;
        btn.height = h;
        btn.textStyle.align = graphics::TextAlign::Center;
        btn.textStyle.fontSize = 24.0f;
        return btn;
    }
};

} // namespace game::components

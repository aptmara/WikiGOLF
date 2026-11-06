#pragma once
/**
 * @file HudStyles.h
 * @brief WikiGolf HUDで共有する外観規則
*/

namespace graphics {
struct TextStyle;
}

namespace game::controllers::hud {

/**
 * @brief 標準パネルの背景、枠線、影を設定します。
 * @param style 設定対象のテキストスタイルです。
*/
void ApplySurfaceStyle(graphics::TextStyle &style);

/**
 * @brief 指定した角丸半径で標準パネルの外観を設定します。
 * @param style 設定対象のテキストスタイルです。
 * @param radius パネルの角丸半径です。
*/
void ApplySurfaceStyle(graphics::TextStyle &style, float radius);

/**
 * @brief 選択中の行に使う強調スタイルを設定します。
 * @param style 設定対象のテキストスタイルです。
*/
void ApplyActiveRowStyle(graphics::TextStyle &style);

/**
 * @brief 通常の選択行に使うスタイルを設定します。
 * @param style 設定対象のテキストスタイルです。
*/
void ApplyRowStyle(graphics::TextStyle &style);

/** @brief 右上の記事表示の見出し（現在/ゴール）のスタイルを設定します。*/
void ApplyCourseInfoLabelStyle(graphics::TextStyle &style);

/** @brief 右上の記事表示の記事名のスタイルを設定します。*/
void ApplyCourseInfoNameStyle(graphics::TextStyle &style);

/** @brief 右上の記事表示の帰属表示のスタイルを設定します。*/
void ApplyCourseInfoAttributionStyle(graphics::TextStyle &style);

} // namespace game::controllers::hud

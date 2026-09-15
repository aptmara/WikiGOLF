#pragma once
/**
 * @file SettingsScene.h
 * @brief 表示・画質設定（「画面」: ウィンドウモード/解像度/VSync/FPS上限/FPS表示、
 *        「画質」: 画質テンプレート/描画解像度/アンチエイリアス/利用GPU）を
 *        変更する設定画面
*/

#include "../../core/GameContext.h"
#include "../../core/Scene.h"
#include <array>
#include <vector>

namespace game::scenes {

/** @brief タイトル画面などの上に重ねて表示する設定オーバーレイ*/
class SettingsScene : public core::Scene {
public:
  const char *GetName() const override { return "SettingsScene"; }
  bool BlocksUnderlyingInput() const override { return true; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;
  void Render(core::GameContext &ctx) override;

private:
  /**
   * @brief 設定項目の並び順。UIの表示順・action番号(prev0/next0等)にそのまま対応する。
   * @details WindowMode〜ShowFpsが「画面」セクション、GraphicsPreset以降が
   *          「画質」セクション（境界は kFirstQualityRow）。
   */
  enum class RowId {
    // 画面
    WindowMode = 0,
    Resolution,
    VSync,
    FpsLimit,
    ShowFps,
    // 画質
    GraphicsPreset,
    RenderScale,
    AntiAliasing,
    Gpu,
    Count,
  };
  static constexpr size_t kRowCount = static_cast<size_t>(RowId::Count);
  static constexpr size_t kFirstQualityRow =
      static_cast<size_t>(RowId::GraphicsPreset);

  /** @brief セクション見出し（「画面」「画質」）を生成する*/
  void CreateSectionHeader(core::GameContext &ctx, const std::wstring &label,
                           float y);

  ecs::Entity CreateArrowButton(core::GameContext &ctx, const std::wstring &label,
                               const std::string &action, float x, float y,
                               float width, float height);

  /** @brief 1設定項目分の行（ラベル・◀・値表示・▶）を生成する*/
  void CreateSettingRow(core::GameContext &ctx, size_t rowIndex,
                        const std::wstring &label, float y);

  /** @brief 現在の設定値に合わせて各行の値表示・有効/無効を更新する*/
  void RefreshDisplay(core::GameContext &ctx);

  std::array<ecs::Entity, kRowCount> m_prevButtons{};
  std::array<ecs::Entity, kRowCount> m_nextButtons{};
  std::array<ecs::Entity, kRowCount> m_valueTexts{};
  ecs::Entity m_antiAliasingHint = 0; ///< 選択中のアンチエイリアス方式の説明文
  ecs::Entity m_closeButton = 0;
  std::vector<ecs::Entity> m_hiddenUnderlyingButtons;
};

} // namespace game::scenes

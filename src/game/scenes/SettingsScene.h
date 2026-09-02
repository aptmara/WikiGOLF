#pragma once
/**
 * @file SettingsScene.h
 * @brief 表示・画質設定（ウィンドウモード/解像度/Render Scale/VSync/FPS上限/
 *        FXAA/MSAA/TAA）を変更する設定画面
 */

#include "../../core/GameContext.h"
#include "../../core/Scene.h"
#include <array>

namespace game::scenes {

/// @brief タイトル画面などの上に重ねて表示する設定オーバーレイ
class SettingsScene : public core::Scene {
public:
  const char *GetName() const override { return "SettingsScene"; }
  bool BlocksUnderlyingInput() const override { return true; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;

private:
  /// @brief 設定項目の並び順。UIの生成順・action番号(prev0/next0等)にそのまま対応する。
  enum class RowId {
    WindowMode = 0,
    Resolution,
    GraphicsPreset,
    RenderScale,
    VSync,
    FpsLimit,
    Fxaa,
    Msaa,
    Taa,
    ShowFps,
    Gpu,
    Count,
  };
  static constexpr size_t kRowCount = static_cast<size_t>(RowId::Count);

  ecs::Entity CreateArrowButton(core::GameContext &ctx, const std::wstring &label,
                               const std::string &action, float x, float y,
                               float width, float height);

  /// @brief 1設定項目分の行（ラベル・◀・値表示・▶）を生成する
  void CreateSettingRow(core::GameContext &ctx, size_t rowIndex,
                        const std::wstring &label, float y);

  /// @brief 現在の設定値に合わせて各行の値表示・有効/無効を更新する
  void RefreshDisplay(core::GameContext &ctx);

  std::array<ecs::Entity, kRowCount> m_prevButtons{};
  std::array<ecs::Entity, kRowCount> m_nextButtons{};
  std::array<ecs::Entity, kRowCount> m_valueTexts{};
  ecs::Entity m_closeButton = 0;
};

} // namespace game::scenes

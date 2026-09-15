/**
 * @file SettingsScene.cpp
 * @brief SettingsScene の実装
*/

#include "SettingsScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/DisplaySettings.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../graphics/GraphicsDevice.h"
#include "../components/UIButton.h"
#include "../components/UIText.h"
#include "ModalSceneRender.h"
#include <cstdlib>
#include <string>

namespace game::scenes {

namespace {
constexpr int kOverlayLayer = 900;
constexpr float kPanelX = 240.0f;
constexpr float kPanelY = 20.0f;
constexpr float kPanelWidth = 800.0f;
constexpr float kPanelHeight = 680.0f;

constexpr float kSectionX = kPanelX + 40.0f;
constexpr float kSectionWidth = kPanelWidth - 80.0f;
constexpr float kLabelWidth = 220.0f;
constexpr float kArrowWidth = 44.0f;
constexpr float kValueWidth = kSectionWidth - kLabelWidth - kArrowWidth * 2.0f;
constexpr float kRowHeight = 36.0f;
constexpr float kRowStep = 43.0f;
constexpr float kSectionHeaderHeight = 30.0f;
constexpr float kSectionHeaderGap = 6.0f;  ///< 見出しと最初の行の間
constexpr float kSectionSpacing = 12.0f;   ///< 前セクション末尾と次の見出しの間
constexpr float kContentTopY = kPanelY + 74.0f;
constexpr float kHintHeight = 22.0f;
constexpr float kCloseHeight = 48.0f;

/** @brief 「画面」セクションの見出しY */
constexpr float kDisplayHeaderY = kContentTopY;
/** @brief 「画質」セクションの見出しY（画面セクションの行数から決まる） */
constexpr float QualityHeaderY(size_t displayRowCount) {
  return kDisplayHeaderY + kSectionHeaderHeight + kSectionHeaderGap +
         static_cast<float>(displayRowCount) * kRowStep + kSectionSpacing;
}

const DirectX::XMFLOAT4 kNormalColor = {0.12f, 0.19f, 0.30f, 1.0f};
const DirectX::XMFLOAT4 kHoverColor = {0.75f, 0.58f, 0.18f, 1.0f};
const DirectX::XMFLOAT4 kPressedColor = {0.92f, 0.75f, 0.28f, 1.0f};

std::wstring FormatWindowMode(core::WindowMode mode) {
  switch (mode) {
  case core::WindowMode::Borderless:
    return L"ボーダーレス";
  case core::WindowMode::Fullscreen:
    return L"フルスクリーン";
  case core::WindowMode::Windowed:
  default:
    return L"ウィンドウ";
  }
}

std::wstring FormatResolution(int width, int height) {
  return std::to_wstring(width) + L" x " + std::to_wstring(height);
}

std::wstring FormatPercent(float ratio) {
  return std::to_wstring(static_cast<int>(ratio * 100.0f + 0.5f)) + L"%";
}

std::wstring FormatOnOff(bool value) { return value ? L"ON" : L"OFF"; }

/** @brief DLSS選択中の「描画解像度」表示（倍率ではなく画質モード名で出す） */
std::wstring FormatDlssQuality(float renderScale) {
  switch (core::DlssQualityFromRenderScale(renderScale)) {
  case core::DlssQuality::Performance:
    return L"DLSS パフォーマンス";
  case core::DlssQuality::Balanced:
    return L"DLSS バランス";
  case core::DlssQuality::Dlaa:
    return L"DLAA（100%）";
  case core::DlssQuality::Quality:
  default:
    return L"DLSS 品質";
  }
}

std::wstring FormatFpsLimit(int fps) {
  return fps <= 0 ? L"無制限" : std::to_wstring(fps);
}

std::wstring FormatAntiAliasing(core::AntiAliasingMode mode) {
  switch (mode) {
  case core::AntiAliasingMode::Fxaa:
    return L"FXAA";
  case core::AntiAliasingMode::Taa:
    return L"TAA";
  case core::AntiAliasingMode::Dlss:
    return L"DLSS";
  case core::AntiAliasingMode::Msaa2:
    return L"MSAA 2x";
  case core::AntiAliasingMode::Msaa4:
    return L"MSAA 4x";
  case core::AntiAliasingMode::Msaa8:
    return L"MSAA 8x";
  case core::AntiAliasingMode::Off:
  default:
    return L"OFF";
  }
}

const wchar_t *DescribeAntiAliasing(core::AntiAliasingMode mode,
                                    bool dlssAvailable) {
  switch (mode) {
  case core::AntiAliasingMode::Fxaa:
    return L"FXAA：軽量。輪郭のギザギザを画面処理でなめらかにします";
  case core::AntiAliasingMode::Taa:
    return L"TAA：複数フレームを合成し、ちらつきを抑えます（描画解像度<100%で高画質化）";
  case core::AntiAliasingMode::Dlss:
    return dlssAvailable
               ? L"DLSS：AIで低解像度から高画質に復元します（NVIDIA RTX）"
               : L"DLSS：このPCでは使えないため、TAAで代替しています";
  case core::AntiAliasingMode::Msaa2:
  case core::AntiAliasingMode::Msaa4:
  case core::AntiAliasingMode::Msaa8:
    return L"※MSAA選択中はTAA/DLSSと距離フォグが無効になります（輪郭は高品質・重め）";
  case core::AntiAliasingMode::Off:
  default:
    return L"アンチエイリアスなし（最も軽量）";
  }
}

const wchar_t *FormatGraphicsPresetName(core::GraphicsPreset preset) {
  switch (preset) {
  case core::GraphicsPreset::Low:
    return L"LOW";
  case core::GraphicsPreset::Medium:
    return L"MEDIUM";
  case core::GraphicsPreset::High:
    return L"HIGH";
  case core::GraphicsPreset::ExHigh:
    return L"EXHIGH";
  case core::GraphicsPreset::Ultra:
    return L"ULTRA";
  case core::GraphicsPreset::Custom:
    return L"CUSTOM";
  case core::GraphicsPreset::Auto:
  default:
    return L"AUTO";
  }
}

std::wstring FormatGraphicsPreset(core::GraphicsPreset selected,
                                  core::GraphicsPreset effective) {
  if (selected == core::GraphicsPreset::Auto) {
    return std::wstring(L"AUTO (") + FormatGraphicsPresetName(effective) +
           L")";
  }
  return FormatGraphicsPresetName(selected);
}

std::wstring Utf8ToWString(const std::string &value) {
  if (value.empty()) {
    return L"";
  }
  const int required =
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (required <= 1) {
    return L"";
  }
  std::wstring result(static_cast<size_t>(required), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), required);
  result.pop_back();
  return result;
}

std::wstring FormatGpu(const std::string &selectedName,
                      const std::string &activeName) {
  if (selectedName.empty()) {
    return L"自動（" + Utf8ToWString(activeName) + L"）";
  }
  return Utf8ToWString(selectedName);
}
} // namespace

ecs::Entity SettingsScene::CreateArrowButton(core::GameContext &ctx,
                                             const std::wstring &label,
                                             const std::string &action,
                                             float x, float y, float width,
                                             float height) {
  auto entity = CreateEntity(ctx.world);
  auto &button = ctx.world.Add<components::UIButton>(entity);
  button = components::UIButton::Create(label, action, x, y, width, height);
  button.textStyle.fontSize = 20.0f;
  button.normalColor = kNormalColor;
  button.hoverColor = kHoverColor;
  button.pressedColor = kPressedColor;
  button.disabledColor = {0.10f, 0.10f, 0.12f, 0.5f};
  button.visible = true;
  return entity;
}

void SettingsScene::CreateSectionHeader(core::GameContext &ctx,
                                        const std::wstring &label, float y) {
  auto entity = CreateEntity(ctx.world);
  auto &header = ctx.world.Add<components::UIText>(entity);
  // 先頭の空白は背景帯の左端から文字を少し離すため
  header.text = L"  " + label;
  header.x = kSectionX;
  header.y = y;
  header.width = kSectionWidth;
  header.height = kSectionHeaderHeight;
  header.style.fontSize = 18.0f;
  header.style.valign = graphics::TextVAlign::Middle;
  header.style.color = {1.0f, 0.86f, 0.5f, 1.0f};
  header.style.bgColor = {0.07f, 0.13f, 0.23f, 1.0f};
  header.style.cornerRadius = 6.0f;
  header.visible = true;
  header.layer = kOverlayLayer + 2;
}

void SettingsScene::CreateSettingRow(core::GameContext &ctx, size_t rowIndex,
                                     const std::wstring &label, float y) {
  auto labelEntity = CreateEntity(ctx.world);
  auto &labelText = ctx.world.Add<components::UIText>(labelEntity);
  labelText.text = label;
  labelText.x = kSectionX;
  labelText.y = y;
  labelText.width = kLabelWidth;
  labelText.height = kRowHeight;
  labelText.style.fontSize = 20.0f;
  labelText.style.valign = graphics::TextVAlign::Middle;
  labelText.style.color = {0.92f, 0.94f, 0.97f, 1.0f};
  labelText.visible = true;
  labelText.layer = kOverlayLayer + 2;

  const float prevX = kSectionX + kLabelWidth;
  const float valueX = prevX + kArrowWidth;
  const float nextX = valueX + kValueWidth;

  const std::string prevAction = "prev" + std::to_string(rowIndex);
  const std::string nextAction = "next" + std::to_string(rowIndex);

  m_prevButtons[rowIndex] = CreateArrowButton(ctx, L"◀", prevAction, prevX, y,
                                              kArrowWidth, kRowHeight);
  m_nextButtons[rowIndex] = CreateArrowButton(ctx, L"▶", nextAction, nextX, y,
                                              kArrowWidth, kRowHeight);

  m_valueTexts[rowIndex] = CreateEntity(ctx.world);
  auto &valueText = ctx.world.Add<components::UIText>(m_valueTexts[rowIndex]);
  valueText.text = L"";
  valueText.x = valueX;
  valueText.y = y;
  valueText.width = kValueWidth;
  valueText.height = kRowHeight;
  // GPU名は長くなりがちなため、その行だけ小さめのフォントにする
  valueText.style.fontSize =
      (rowIndex == static_cast<size_t>(RowId::Gpu)) ? 14.0f : 22.0f;
  valueText.style.align = graphics::TextAlign::Center;
  valueText.style.valign = graphics::TextVAlign::Middle;
  valueText.style.color = {0.95f, 0.95f, 0.95f, 1.0f};
  valueText.visible = true;
  valueText.layer = kOverlayLayer + 2;
}

void SettingsScene::OnEnter(core::GameContext &ctx) {
  ctx.input.SetMouseCursorVisible(true);
  ctx.input.SetMouseCursorLocked(false);

  auto panelEntity = CreateEntity(ctx.world);
  auto &panel = ctx.world.Add<components::UIText>(panelEntity);
  panel.text = L"";
  panel.x = kPanelX;
  panel.y = kPanelY;
  panel.width = kPanelWidth;
  panel.height = kPanelHeight;
  panel.style.bgColor = {0.025f, 0.06f, 0.12f, 0.98f};
  panel.style.borderColor = {0.8f, 0.68f, 0.28f, 1.0f};
  panel.style.borderWidth = 2.0f;
  panel.style.cornerRadius = 18.0f;
  panel.visible = true;
  panel.layer = kOverlayLayer + 1;

  auto titleEntity = CreateEntity(ctx.world);
  auto &title = ctx.world.Add<components::UIText>(titleEntity);
  title.text = L"設定";
  title.x = kPanelX;
  title.y = kPanelY + 20.0f;
  title.width = kPanelWidth;
  title.height = 50.0f;
  title.style.fontFamily = "Times New Roman";
  title.style.fontSize = 34.0f;
  title.style.align = graphics::TextAlign::Center;
  title.style.color = {1.0f, 0.9f, 0.55f, 1.0f};
  title.visible = true;
  title.layer = kOverlayLayer + 2;

  // RowIdの並び順どおりに表示する（kFirstQualityRowでセクションを分ける）
  const wchar_t *const kRowLabels[kRowCount] = {
      L"ウィンドウモード", L"解像度",     L"VSync",
      L"FPS上限",         L"FPS表示",    L"画質テンプレート",
      L"描画解像度",       L"アンチエイリアス", L"利用GPU",
  };

  const float qualityHeaderY = QualityHeaderY(kFirstQualityRow);
  CreateSectionHeader(ctx, L"画面", kDisplayHeaderY);
  CreateSectionHeader(ctx, L"画質", qualityHeaderY);

  const float displayRowsY =
      kDisplayHeaderY + kSectionHeaderHeight + kSectionHeaderGap;
  const float qualityRowsY =
      qualityHeaderY + kSectionHeaderHeight + kSectionHeaderGap;
  for (size_t index = 0; index < kRowCount; ++index) {
    float y = displayRowsY + static_cast<float>(index) * kRowStep;
    if (index >= kFirstQualityRow) {
      y = qualityRowsY + static_cast<float>(index - kFirstQualityRow) * kRowStep;
    }
    CreateSettingRow(ctx, index, kRowLabels[index], y);
  }

  auto createHint = [&](const std::wstring &text, float y) {
    auto entity = CreateEntity(ctx.world);
    auto &hint = ctx.world.Add<components::UIText>(entity);
    hint.text = text;
    hint.x = kSectionX;
    hint.y = y;
    hint.width = kSectionWidth;
    hint.height = kHintHeight;
    hint.style.fontSize = 13.0f;
    hint.style.align = graphics::TextAlign::Center;
    hint.style.valign = graphics::TextVAlign::Middle;
    hint.style.color = {0.6f, 0.66f, 0.74f, 1.0f};
    hint.visible = true;
    hint.layer = kOverlayLayer + 2;
    return entity;
  };

  const float hintsY = qualityRowsY +
                       static_cast<float>(kRowCount - kFirstQualityRow) * kRowStep;
  m_antiAliasingHint = createHint(L"", hintsY);
  createHint(L"※GPU設定の変更は次回起動時に反映されます", hintsY + kHintHeight);

  const float closeY = hintsY + kHintHeight * 2.0f + 12.0f;
  m_closeButton = CreateArrowButton(ctx, L"閉じる", "close", kSectionX, closeY,
                                    kSectionWidth, kCloseHeight);
  if (auto *btn = ctx.world.Get<components::UIButton>(m_closeButton)) {
    btn->textStyle.fontSize = 24.0f;
  }

  ctx.world.Query<components::UIButton>().Each(
      [&](ecs::Entity entity, components::UIButton &button) {
        if (!OwnsEntity(entity) && button.visible) {
          m_hiddenUnderlyingButtons.push_back(entity);
          button.visible = false;
        }
      });

  RefreshDisplay(ctx);
}

void SettingsScene::RefreshDisplay(core::GameContext &ctx) {
  if (!ctx.displaySettings) {
    return;
  }
  const auto &data = ctx.displaySettings->GetData();
  const bool isBorderless = (data.mode == core::WindowMode::Borderless);

  auto setValue = [&](RowId id, const std::wstring &text) {
    if (auto *t = ctx.world.Get<components::UIText>(m_valueTexts[static_cast<size_t>(id)])) {
      t->text = text;
    }
  };

  setValue(RowId::WindowMode, FormatWindowMode(data.mode));
  setValue(RowId::Resolution, isBorderless
                                  ? FormatResolution(ctx.displaySettings->GetCurrentWidth(),
                                                     ctx.displaySettings->GetCurrentHeight())
                                  : FormatResolution(data.windowedWidth, data.windowedHeight));
  setValue(RowId::GraphicsPreset,
           FormatGraphicsPreset(data.graphicsPreset,
                                ctx.displaySettings->GetEffectiveGraphicsPreset()));
  setValue(RowId::RenderScale, data.dlssEnabled
                                   ? FormatDlssQuality(data.renderScale)
                                   : FormatPercent(data.renderScale));
  setValue(RowId::VSync, FormatOnOff(data.vsync));
  setValue(RowId::FpsLimit, FormatFpsLimit(data.fpsLimit));
  const core::AntiAliasingMode antiAliasing =
      ctx.displaySettings->GetAntiAliasingMode();
  setValue(RowId::AntiAliasing, FormatAntiAliasing(antiAliasing));
  setValue(RowId::ShowFps, FormatOnOff(data.showFps));
  setValue(RowId::Gpu, FormatGpu(data.gpuAdapterName, ctx.graphics.GetAdapterName()));

  if (auto *hint = ctx.world.Get<components::UIText>(m_antiAliasingHint)) {
    const bool dlssAvailable = ctx.displaySettings->IsDlssAvailable();
    hint->text = DescribeAntiAliasing(antiAliasing, dlssAvailable);
    // 他の機能が無効になる・代替で動いている選択は注意色で目立たせる
    const bool isMsaa = antiAliasing == core::AntiAliasingMode::Msaa2 ||
                        antiAliasing == core::AntiAliasingMode::Msaa4 ||
                        antiAliasing == core::AntiAliasingMode::Msaa8;
    const bool isDlssFallback =
        antiAliasing == core::AntiAliasingMode::Dlss && !dlssAvailable;
    hint->style.color = (isMsaa || isDlssFallback)
                            ? DirectX::XMFLOAT4{1.0f, 0.72f, 0.3f, 1.0f}
                            : DirectX::XMFLOAT4{0.6f, 0.66f, 0.74f, 1.0f};
  }

  // 解像度はBorderless中はモニタ解像度に固定されるため矢印を無効化する
  if (auto *t = ctx.world.Get<components::UIText>(m_valueTexts[static_cast<size_t>(RowId::Resolution)])) {
    t->style.color = isBorderless ? DirectX::XMFLOAT4{0.55f, 0.58f, 0.62f, 1.0f}
                                  : DirectX::XMFLOAT4{0.95f, 0.95f, 0.95f, 1.0f};
  }
}

void SettingsScene::OnUpdate(core::GameContext &ctx) {
  if (!ctx.sceneManager || !ctx.displaySettings) {
    return;
  }

  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.45f);
    }
    ctx.sceneManager->PopScene();
    return;
  }

  const bool isBorderless =
      (ctx.displaySettings->GetData().mode == core::WindowMode::Borderless);

  // Borderless中は解像度がモニタ解像度に固定されるため矢印を無効化する。
  // UIButtonSystemはDisabled状態のボタンを触らないため、他モードへ戻した際は
  // ここで明示的にNormalへ戻してやる必要がある。
  auto syncResolutionArrowDisabled = [&](ecs::Entity entity) {
    auto *btn = ctx.world.Get<components::UIButton>(entity);
    if (!btn) {
      return;
    }
    if (isBorderless) {
      btn->state = components::ButtonState::Disabled;
    } else if (btn->state == components::ButtonState::Disabled) {
      btn->state = components::ButtonState::Normal;
    }
  };
  syncResolutionArrowDisabled(m_prevButtons[static_cast<size_t>(RowId::Resolution)]);
  syncResolutionArrowDisabled(m_nextButtons[static_cast<size_t>(RowId::Resolution)]);

  bool settingsChanged = false;
  bool closeRequested = false;

  ctx.world.Query<components::UIButton>().Each(
      [&](ecs::Entity entity, components::UIButton &button) {
        if (!OwnsEntity(entity) || !button.visible ||
            button.state != components::ButtonState::Pressed ||
            !ctx.input.GetMouseButtonDown(0)) {
          return;
        }

        if (button.action == "close") {
          closeRequested = true;
          return;
        }

        const bool isPrev = button.action.rfind("prev", 0) == 0;
        const bool isNext = button.action.rfind("next", 0) == 0;
        if (!isPrev && !isNext) {
          return;
        }
        const int direction = isNext ? 1 : -1;
        const int rowIndex = std::atoi(button.action.c_str() + 4);
        auto *ds = ctx.displaySettings;

        switch (static_cast<RowId>(rowIndex)) {
        case RowId::WindowMode:
          ds->CycleWindowMode(direction);
          break;
        case RowId::Resolution:
          if (!isBorderless) {
            ds->CycleResolution(direction);
          }
          break;
        case RowId::GraphicsPreset:
          ds->CycleGraphicsPreset(direction);
          break;
        case RowId::RenderScale:
          ds->CycleRenderScale(direction);
          break;
        case RowId::VSync:
          ds->SetVSync(!ds->GetData().vsync);
          break;
        case RowId::FpsLimit:
          ds->CycleFpsLimit(direction);
          break;
        case RowId::AntiAliasing:
          ds->CycleAntiAliasing(direction);
          break;
        case RowId::ShowFps:
          ds->SetShowFps(!ds->GetData().showFps);
          break;
        case RowId::Gpu:
          ds->CycleGpu(direction);
          break;
        default:
          break;
        }
        settingsChanged = true;
      });

  if (settingsChanged) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.4f);
    }
    RefreshDisplay(ctx);
  }

  if (closeRequested) {
    if (ctx.audio) {
      ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.45f);
    }
    ctx.sceneManager->PopScene();
  }
}

void SettingsScene::Render(core::GameContext &ctx) {
  RenderModalScene(ctx, *this, {0.18f, 0.18f, 0.18f, 0.62f});
}

void SettingsScene::OnExit(core::GameContext &ctx) {
  for (const ecs::Entity entity : m_hiddenUnderlyingButtons) {
    if (auto *button = ctx.world.Get<components::UIButton>(entity)) {
      button->visible = true;
    }
  }
  m_hiddenUnderlyingButtons.clear();
  DestroyAllEntities(ctx);
}

} // namespace game::scenes

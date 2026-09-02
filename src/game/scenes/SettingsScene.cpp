#include "SettingsScene.h"
#include "../../audio/AudioSystem.h"
#include "../../core/DisplaySettings.h"
#include "../../core/Input.h"
#include "../../core/SceneManager.h"
#include "../../graphics/GraphicsDevice.h"
#include "../components/UIButton.h"
#include "../components/UIText.h"
#include <cstdlib>
#include <string>

namespace game::scenes {

namespace {
constexpr int kOverlayLayer = 900;
constexpr float kPanelX = 240.0f;
constexpr float kPanelY = 10.0f;
constexpr float kPanelWidth = 800.0f;
constexpr float kPanelHeight = 700.0f;

constexpr float kSectionX = kPanelX + 40.0f;
constexpr float kSectionWidth = kPanelWidth - 80.0f;
constexpr float kLabelWidth = 220.0f;
constexpr float kArrowWidth = 44.0f;
constexpr float kValueWidth = kSectionWidth - kLabelWidth - kArrowWidth * 2.0f;
constexpr float kRowHeight = 38.0f;
constexpr float kRowStep = 45.0f;
constexpr float kFirstRowY = kPanelY + 70.0f;
constexpr float kCloseY = kFirstRowY + 11.0f * kRowStep + 28.0f;

const DirectX::XMFLOAT4 kNormalColor = {0.15f, 0.21f, 0.34f, 0.96f};
const DirectX::XMFLOAT4 kHoverColor = {0.24f, 0.36f, 0.56f, 0.98f};
const DirectX::XMFLOAT4 kPressedColor = {0.12f, 0.18f, 0.28f, 1.0f};

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

std::wstring FormatFpsLimit(int fps) {
  return fps <= 0 ? L"無制限" : std::to_wstring(fps);
}

std::wstring FormatMsaa(int samples) {
  return samples <= 1 ? L"OFF" : (std::to_wstring(samples) + L"x");
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
  labelText.style.color = {0.86f, 0.90f, 0.98f, 1.0f};
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
  auto overlayEntity = CreateEntity(ctx.world);
  auto &overlay = ctx.world.Add<components::UIText>(overlayEntity);
  overlay.text = L"";
  overlay.x = 0.0f;
  overlay.y = 0.0f;
  overlay.width = 1280.0f;
  overlay.height = 720.0f;
  overlay.style.bgColor = {0.02f, 0.03f, 0.06f, 0.68f};
  overlay.visible = true;
  overlay.layer = kOverlayLayer;
  overlay.fullScreenCover = true; // レターボックス余白も含めて画面全体を暗転させる

  auto panelEntity = CreateEntity(ctx.world);
  auto &panel = ctx.world.Add<components::UIText>(panelEntity);
  panel.text = L"";
  panel.x = kPanelX;
  panel.y = kPanelY;
  panel.width = kPanelWidth;
  panel.height = kPanelHeight;
  panel.style.bgColor = {0.08f, 0.12f, 0.20f, 0.94f};
  panel.style.borderColor = {0.72f, 0.82f, 0.95f, 0.95f};
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
  title.style.fontSize = 30.0f;
  title.style.align = graphics::TextAlign::Center;
  title.style.color = {1.0f, 0.97f, 0.90f, 1.0f};
  title.style.hasShadow = true;
  title.visible = true;
  title.layer = kOverlayLayer + 2;

  const struct { RowId id; const wchar_t *label; } kRows[] = {
      {RowId::WindowMode, L"ウィンドウモード"},
      {RowId::Resolution, L"解像度"},
      {RowId::GraphicsPreset, L"画質テンプレート"},
      {RowId::RenderScale, L"描画解像度"},
      {RowId::VSync, L"VSync"},
      {RowId::FpsLimit, L"FPS上限"},
      {RowId::Fxaa, L"FXAA"},
      {RowId::Msaa, L"MSAA"},
      {RowId::Taa, L"TAA"},
      {RowId::ShowFps, L"FPS表示"},
      {RowId::Gpu, L"利用GPU"},
  };
  for (const auto &row : kRows) {
    const size_t index = static_cast<size_t>(row.id);
    CreateSettingRow(ctx, index, row.label, kFirstRowY + index * kRowStep);
  }

  auto gpuHintEntity = CreateEntity(ctx.world);
  auto &gpuHint = ctx.world.Add<components::UIText>(gpuHintEntity);
  gpuHint.text = L"※GPU設定の変更は次回起動時に反映されます";
  gpuHint.x = kSectionX;
  gpuHint.y = kFirstRowY + static_cast<float>(kRowCount) * kRowStep - 4.0f;
  gpuHint.width = kSectionWidth;
  gpuHint.height = 22.0f;
  gpuHint.style.fontSize = 13.0f;
  gpuHint.style.align = graphics::TextAlign::Center;
  gpuHint.style.color = {0.6f, 0.66f, 0.74f, 1.0f};
  gpuHint.visible = true;
  gpuHint.layer = kOverlayLayer + 2;

  m_closeButton = CreateArrowButton(ctx, L"閉じる", "close", kSectionX, kCloseY,
                                    kSectionWidth, 48.0f);
  if (auto *btn = ctx.world.Get<components::UIButton>(m_closeButton)) {
    btn->textStyle.fontSize = 24.0f;
  }

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
  setValue(RowId::RenderScale, FormatPercent(data.renderScale));
  setValue(RowId::VSync, FormatOnOff(data.vsync));
  setValue(RowId::FpsLimit, FormatFpsLimit(data.fpsLimit));
  setValue(RowId::Fxaa, FormatOnOff(data.fxaaEnabled));
  setValue(RowId::Msaa, FormatMsaa(data.msaaSamples));
  setValue(RowId::Taa, data.taaEnabled ? L"ON（未実装）" : L"OFF");
  setValue(RowId::ShowFps, FormatOnOff(data.showFps));
  setValue(RowId::Gpu, FormatGpu(data.gpuAdapterName, ctx.graphics.GetAdapterName()));

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
      [&](ecs::Entity, components::UIButton &button) {
        if (!button.visible ||
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
        case RowId::Fxaa:
          ds->SetFxaaEnabled(!ds->GetData().fxaaEnabled);
          break;
        case RowId::Msaa:
          ds->CycleMsaa(direction);
          break;
        case RowId::Taa:
          ds->SetTaaEnabled(!ds->GetData().taaEnabled);
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

} // namespace game::scenes

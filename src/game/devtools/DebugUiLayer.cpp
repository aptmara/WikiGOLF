#include "DebugUiLayer.h"
#include "DebugInputCaptureRules.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace game::debug {

DebugUiLayer *DebugUiLayer::s_activeLayer = nullptr;

bool DebugUiLayer::Initialize(HWND window, ID3D11Device *device,
                              ID3D11DeviceContext *context) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  if (!ImGui_ImplWin32_Init(window) || !ImGui_ImplDX11_Init(device, context)) {
    Shutdown();
    return false;
  }
  m_initialized = true;
  s_activeLayer = this;
  return true;
}

void DebugUiLayer::Shutdown() {
  if (!ImGui::GetCurrentContext()) {
    return;
  }
  if (m_initialized) {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
  }
  ImGui::DestroyContext();
  m_initialized = false;
  if (s_activeLayer == this) {
    s_activeLayer = nullptr;
  }
}

void DebugUiLayer::BeginFrame() {
  if (!m_initialized) {
    return;
  }
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
  if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
    m_overlay.Toggle();
  }
}

void DebugUiLayer::Render(core::GameContext &ctx,
                          DebugTimeController &time) {
  if (!m_initialized) {
    return;
  }
  m_colliderRenderer.Draw(ctx, m_overlay.GetColliderSettings());
  m_overlay.Draw(ctx, time);
  ImGui::Render();
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool DebugUiLayer::IsPauseToggleRequested() const {
  return m_initialized && ImGui::IsKeyPressed(ImGuiKey_F5, false);
}

bool DebugUiLayer::IsFrameStepRequested() const {
  return m_initialized && ImGui::IsKeyPressed(ImGuiKey_F6, false);
}

bool DebugUiLayer::IsTimeScaleCycleRequested() const {
  return m_initialized && ImGui::IsKeyPressed(ImGuiKey_F7, false);
}

LRESULT DebugUiLayer::ProcessWindowMessage(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  if (!ImGui::GetCurrentContext()) {
    return 0;
  }
  const LRESULT result =
      ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
  if (s_activeLayer && s_activeLayer->m_overlay.IsVisible() &&
      IsGameInputMessage(message)) {
    return 1;
  }
  return result;
}

} // namespace game::debug

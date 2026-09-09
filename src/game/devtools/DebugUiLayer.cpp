#include "DebugUiLayer.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace game::debug {

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
}

void DebugUiLayer::BeginFrame() {
  if (!m_initialized) {
    return;
  }
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
}

void DebugUiLayer::Render() {
  if (!m_initialized) {
    return;
  }
  ImGui::Render();
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

LRESULT DebugUiLayer::ProcessWindowMessage(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  if (!ImGui::GetCurrentContext()) {
    return 0;
  }
  return ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
}

} // namespace game::debug

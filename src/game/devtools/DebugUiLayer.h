#pragma once

#include <Windows.h>
#include <d3d11.h>

namespace game::debug {

class DebugUiLayer {
public:
  bool Initialize(HWND window, ID3D11Device *device,
                  ID3D11DeviceContext *context);
  void Shutdown();
  void BeginFrame();
  void Render();
  bool IsPauseToggleRequested() const;
  bool IsFrameStepRequested() const;
  bool IsTimeScaleCycleRequested() const;

  static LRESULT ProcessWindowMessage(HWND window, UINT message,
                                      WPARAM wParam, LPARAM lParam);

private:
  bool m_initialized = false;
};

} // namespace game::debug

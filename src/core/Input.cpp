#include "Input.h"
#include "Logger.h"
#include <windowsx.h>

namespace core {

void Input::Initialize() {
  m_keys.fill(false);
  m_keysDown.fill(false);
  m_keysUp.fill(false);
  m_mouseButtons.fill(false);
  m_mouseButtonsDown.fill(false);
  m_mouseButtonsUp.fill(false);
  m_mousePosition = {0, 0};
  m_cursorVisible = true;
  m_cursorLocked = false;
}

void Input::Update() {
  // デバッグログ（マウスボタン状態）
  if (m_mouseButtonsDown[0])
    LOG_DEBUG("Input", "LMB Down");
  if (m_mouseButtonsDown[1])
    LOG_DEBUG("Input", "RMB Down");

  // イベントフラグをリセット（次フレームの前に）
  m_keysDown.fill(false);
  m_keysUp.fill(false);
  m_mouseButtonsDown.fill(false);
  m_mouseButtonsUp.fill(false);
}

void Input::ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  // --- キーボード ---
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (wParam < 256 && !m_keys[wParam]) {
      m_keysDown[wParam] = true; // 押された瞬間
    }
    if (wParam < 256) {
      m_keys[wParam] = true;
    }
    break;

  case WM_KEYUP:
  case WM_SYSKEYUP:
    if (wParam < 256) {
      m_keys[wParam] = false;
      m_keysUp[wParam] = true; // 離された瞬間
    }
    break;

  // --- マウス移動 ---
  case WM_MOUSEMOVE:
    m_mousePosition.x = GET_X_LPARAM(lParam);
    m_mousePosition.y = GET_Y_LPARAM(lParam);

    if (m_cursorLocked) {
      // 射影ロジックやキャプチャ用にウィンドウ中央に戻す
      // ここではカーソルが動かないようにクリップする
      RECT clipRect;
      if (GetClipCursor(&clipRect)) {
        // すでにクリップされていれば何もしない
      } else {
        // 画面全体を 0 で初期化してからウィンドウ領域をクリップするのは
        // 呼び出し側のウィンドウハンドルが必要なので、簡易的に
        // カーソルは非表示にして中央にセットする
        // 注意: 正確なウィンドウ領域でのロックは呼び出し側が行うことを想定
        SetCursorPos(0, 0);
      }
    }
    break;

  // --- マウスボタン ---
  case WM_LBUTTONDOWN:
    m_mouseButtons[0] = true;
    m_mouseButtonsDown[0] = true;
    break;
  case WM_LBUTTONUP:
    m_mouseButtons[0] = false;
    m_mouseButtonsUp[0] = true;
    break;
  case WM_RBUTTONDOWN:
    m_mouseButtons[1] = true;
    m_mouseButtonsDown[1] = true;
    break;
  case WM_RBUTTONUP:
    m_mouseButtons[1] = false;
    m_mouseButtonsUp[1] = true;
    break;
  case WM_MBUTTONDOWN:
    m_mouseButtons[2] = true;
    m_mouseButtonsDown[2] = true;
    break;
  case WM_MBUTTONUP:
    m_mouseButtons[2] = false;
    m_mouseButtonsUp[2] = true;
    break;
  }
}

bool Input::GetKey(int key) const {
  if (key < 0 || key >= 256)
    return false;
  return m_keys[key];
}

bool Input::GetKeyDown(int key) const {
  if (key < 0 || key >= 256)
    return false;
  return m_keysDown[key];
}

bool Input::GetKeyUp(int key) const {
  if (key < 0 || key >= 256)
    return false;
  return m_keysUp[key];
}

bool Input::GetMouseButton(int button) const {
  if (button < 0 || button >= 3)
    return false;
  return m_mouseButtons[button];
}

bool Input::GetMouseButtonDown(int button) const {
  if (button < 0 || button >= 3)
    return false;
  return m_mouseButtonsDown[button];
}

bool Input::GetMouseButtonUp(int button) const {
  if (button < 0 || button >= 3)
    return false;
  return m_mouseButtonsUp[button];
}

void Input::SetMouseCursorVisible(bool visible) {
  m_cursorVisible = visible;
  ShowCursor(visible);
}

void Input::SetMouseCursorLocked(bool locked) {
  m_cursorLocked = locked;
  if (locked) {
    // クリップは画面全体ではなく、呼び出し元がウィンドウ領域を指定する想定
    // ここでは簡易的にカーソルを中心に固定
    POINT p = {0, 0};
    SetCursorPos(p.x, p.y);
    // Hide cursor when locked
    ShowCursor(FALSE);
  } else {
    // Release clipping
    ClipCursor(nullptr);
    ShowCursor(TRUE);
  }
}

} // namespace core

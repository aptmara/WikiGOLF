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
  m_scrollDelta = 0.0f;
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
  m_scrollDelta = 0.0f;
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
    m_mouseButtonsUp[2] = true;
    break;

  case WM_MOUSEWHEEL:
    // ホイールの回転量 (WHEEL_DELTA = 120 単位)
    // 正: 奥（上）, 負: 手前（下）
    {
      float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
      m_scrollDelta += delta;
    }
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

float Input::GetMouseScrollDelta() const { return m_scrollDelta; }

void Input::SetMouseCursorVisible(bool visible) {
  m_cursorVisible = visible;
  ShowCursor(visible);
}

void Input::SetMouseCursorLocked(bool locked) {
  // 状態が変わらなければ何もしない（毎フレーム呼ばれる対策）
  if (m_cursorLocked == locked) {
    // ただしロック中は毎フレームカーソルを中央に戻す
    if (locked) {
      HWND hwnd = GetActiveWindow();
      if (hwnd) {
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        POINT topLeft = {clientRect.left, clientRect.top};
        POINT bottomRight = {clientRect.right, clientRect.bottom};
        ClientToScreen(hwnd, &topLeft);
        ClientToScreen(hwnd, &bottomRight);

        int centerX = (topLeft.x + bottomRight.x) / 2;
        int centerY = (topLeft.y + bottomRight.y) / 2;
        SetCursorPos(centerX, centerY);

        // マウス位置も中央に更新（デルタ計算のため）
        m_mousePosition.x = (clientRect.right - clientRect.left) / 2;
        m_mousePosition.y = (clientRect.bottom - clientRect.top) / 2;
      }
    }
    return;
  }

  m_cursorLocked = locked;

  if (locked) {
    // ウィンドウハンドルを取得（アクティブウィンドウ）
    HWND hwnd = GetActiveWindow();
    if (hwnd) {
      // ウィンドウのクライアント領域を取得
      RECT clientRect;
      GetClientRect(hwnd, &clientRect);

      // クライアント座標をスクリーン座標に変換
      POINT topLeft = {clientRect.left, clientRect.top};
      POINT bottomRight = {clientRect.right, clientRect.bottom};
      ClientToScreen(hwnd, &topLeft);
      ClientToScreen(hwnd, &bottomRight);

      // クリップ領域をウィンドウ内に制限
      RECT clipRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
      ClipCursor(&clipRect);

      // カーソルをウィンドウ中心に移動
      int centerX = (topLeft.x + bottomRight.x) / 2;
      int centerY = (topLeft.y + bottomRight.y) / 2;
      SetCursorPos(centerX, centerY);

      // マウス位置も中央に初期化
      m_mousePosition.x = (clientRect.right - clientRect.left) / 2;
      m_mousePosition.y = (clientRect.bottom - clientRect.top) / 2;
    }

    // カーソルを非表示
    ShowCursor(FALSE);
  } else {
    // クリップ解除
    ClipCursor(nullptr);
    ShowCursor(TRUE);
  }
}

} // namespace core

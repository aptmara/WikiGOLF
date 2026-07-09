#pragma once
#include <DirectXMath.h>
#include <Windows.h>
#include <array>
#include <string>

namespace core {

class Input {
public:
  /// @brief 初期化
  void Initialize();

  /// @brief ウィンドウの解像度を設定
  void SetResolution(int width, int height) {
    m_windowWidth = width;
    m_windowHeight = height;
  }

  /// @brief フレームごとの更新処理（イベントフラグをリセット）
  void Update();

  /// @brief Win32メッセージ処理用ハンドラ
  void ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam);

  // --- キーボード入力 ---

  /// @brief キーが押されているか
  bool GetKey(int key) const;

  /// @brief キーが押された瞬間か（このフレームで）
  bool GetKeyDown(int key) const;

  /// @brief キーが離された瞬間か
  bool GetKeyUp(int key) const;

  // --- マウス入力 ---

  /// @brief マウスボタンが押されているか (0:左, 1:右, 2:中)
  bool GetMouseButton(int button) const;

  /// @brief マウスボタンが押された瞬間か（このフレームで）
  bool GetMouseButtonDown(int button) const;

  /// @brief マウスボタンが離された瞬間か
  bool GetMouseButtonUp(int button) const;

  /// @brief マウス位置取得
  DirectX::XMINT2 GetMousePosition() const { return m_mousePosition; }

  /// @brief マウスホイール変位取得
  float GetMouseScrollDelta() const;

  /// @brief マウスカーソルの表示/非表示
  void SetMouseCursorVisible(bool visible);

  /// @brief マウスカーソルのロック（ウィンドウ内に制限）
  void SetMouseCursorLocked(bool locked);

  // --- テキスト入力・クリップボード ---

  /// @brief このフレームで入力された文字（WM_CHARベース）を取得
  const std::wstring& GetInputChars() const { return m_inputChars; }

  /// @brief バックスペースが押されたか
  bool GetBackspacePressed() const { return m_backspacePressed; }

  /// @brief クリップボードから文字列を取得する
  std::wstring GetClipboardText() const;

private:
  std::array<bool, 256> m_keys;
  std::array<bool, 256> m_keysDown; ///< このフレームで押された
  std::array<bool, 256> m_keysUp;   ///< このフレームで離された

  std::array<bool, 3> m_mouseButtons;     ///< 現在押されているか
  std::array<bool, 3> m_mouseButtonsDown; ///< このフレームで押された
  std::array<bool, 3> m_mouseButtonsUp;   ///< このフレームで離された

  DirectX::XMINT2 m_mousePosition;
  float m_scrollDelta = 0.0f;

  // テキスト入力バッファ
  std::wstring m_inputChars;
  bool m_backspacePressed = false;

  // カーソル状態管理
  bool m_cursorVisible = true;
  bool m_cursorLocked = false;

  int m_windowWidth = 1280;
  int m_windowHeight = 720;
};

} // namespace core

#pragma once

#include <Windows.h>

namespace game::debug {

inline bool IsGameInputMessage(UINT message) {
  switch (message) {
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_CHAR:
  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MOUSEWHEEL:
    return true;
  default:
    return false;
  }
}

} // namespace game::debug

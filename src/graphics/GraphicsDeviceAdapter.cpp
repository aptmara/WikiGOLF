/**
 * @file GraphicsDevice.cpp
 * @brief DirectX11デバイス・コンテキスト管理の実装
 */

#include "GraphicsDevice.h"
#include "../core/Logger.h"
#include <algorithm>
#include <cstring>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <limits>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace graphics {
namespace {

std::string WideToUtf8(const wchar_t *value) {
  if (!value || value[0] == L'\0') {
    return "Unknown";
  }
  const int required =
      WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (required <= 1) {
    return "Unknown";
  }
  std::string result(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr,
                      nullptr);
  result.pop_back();
  return result;
}



} // namespace

bool GraphicsDevice::CreateSwapChainAndDevice(HWND hWnd) {
  auto driverTypeToStr = [](D3D_DRIVER_TYPE type) {
    switch (type) {
    case D3D_DRIVER_TYPE_HARDWARE:
      return "HARDWARE";
    case D3D_DRIVER_TYPE_WARP:
      return "WARP";
    case D3D_DRIVER_TYPE_REFERENCE:
      return "REFERENCE";
    default:
      return "UNKNOWN";
    }
  };

  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  swapChainDesc.BufferCount = 2;
  swapChainDesc.BufferDesc.Width = m_width;
  swapChainDesc.BufferDesc.Height = m_height;
  swapChainDesc.BufferDesc.Format =
      DXGI_FORMAT_B8G8R8A8_UNORM; // D2D互換フォーマット
  swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
  swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.OutputWindow = hWnd;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.SampleDesc.Quality = 0;
  swapChainDesc.Windowed = TRUE;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL; // D2D互換を優先

  UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // D2D互換に必須
#ifdef _DEBUG
  createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };

  D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
  auto resetCreatedObjects = [&]() {
    m_context.Reset();
    m_device.Reset();
    m_swapChain.Reset();
  };
  auto tryCreateForAdapter = [&](IDXGIAdapter1 *adapter) {
    resetCreatedObjects();
    return D3D11CreateDeviceAndSwapChain(
        adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, createDeviceFlags,
        featureLevels,
        _countof(featureLevels), D3D11_SDK_VERSION, &swapChainDesc,
        &m_swapChain, &m_device, &featureLevel, &m_context);
  };

  HRESULT hr = E_FAIL;
  ComPtr<IDXGIFactory1> factory;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    // 明示的にGPUが指定されていれば、名前が一致するアダプタでの作成を最優先で試す。
    // 見つからない/作成に失敗した場合は下の自動選択（高性能優先）へフォールバックする。
    if (!m_preferredAdapterName.empty()) {
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT enumResult = factory->EnumAdapters1(index, &adapter);
        if (enumResult == DXGI_ERROR_NOT_FOUND || FAILED(enumResult)) {
          break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
          continue;
        }
        if (m_preferredAdapterName != description.Description) {
          continue;
        }
        hr = tryCreateForAdapter(adapter.Get());
        if (SUCCEEDED(hr)) {
          LOG_INFO("GraphicsDevice", "Using preferred GPU: '{}'",
                   WideToUtf8(description.Description));
        } else {
          LOG_WARN("GraphicsDevice",
                   "Preferred GPU creation failed (hr={:08X}); falling back to "
                   "automatic selection",
                   static_cast<uint32_t>(hr));
        }
        break;
      }
    }

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(hr)) {
      // 優先GPUでの作成に成功しているため、自動選択ループはスキップする。
    } else if (SUCCEEDED(factory.As(&factory6))) {
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT enumResult = factory6->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter));
        if (enumResult == DXGI_ERROR_NOT_FOUND) {
          break;
        }
        if (FAILED(enumResult)) {
          break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
          continue;
        }
        hr = tryCreateForAdapter(adapter.Get());
        if (SUCCEEDED(hr)) {
          break;
        }
      }
    } else {
      for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT enumResult = factory->EnumAdapters1(index, &adapter);
        if (enumResult == DXGI_ERROR_NOT_FOUND) {
          break;
        }
        if (FAILED(enumResult)) {
          break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
          continue;
        }
        hr = tryCreateForAdapter(adapter.Get());
        if (SUCCEEDED(hr)) {
          break;
        }
      }
    }
  }

  if (SUCCEEDED(hr)) {
    m_driverType = D3D_DRIVER_TYPE_HARDWARE;
  } else {
    LOG_WARN("GraphicsDevice",
             "Hardware device creation failed (hr={:08X}); falling back to WARP",
             static_cast<uint32_t>(hr));
    resetCreatedObjects();
    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
        featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
        &swapChainDesc, &m_swapChain, &m_device, &featureLevel, &m_context);
    m_driverType = D3D_DRIVER_TYPE_WARP;
  }

  if (FAILED(hr)) {
    LOG_ERROR("GraphicsDevice", "Device creation failed (hr={:08X})",
              static_cast<uint32_t>(hr));
    return false;
  }

  m_featureLevel = featureLevel;
  CaptureAdapterInfo();
  const char* driverStr = "WARP";
  if (m_driverType == D3D_DRIVER_TYPE_HARDWARE) {
    driverStr = "HARDWARE";
  }
  LOG_INFO("GraphicsDevice",
           "Device created. Driver={}, Adapter='{}', DedicatedVRAM={:.0f}MB, "
           "FeatureLevel=0x{:04X}",
           driverStr, m_adapterName,
           static_cast<double>(m_dedicatedVideoMemoryBytes) / (1024.0 * 1024.0),
           static_cast<uint32_t>(featureLevel));

  // D3D11でのマルチスレッド保護を有効化
  // デバイスコンテキストからマルチスレッド保護インターフェースを取得
  Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
  if (SUCCEEDED(m_context.As(&multithread))) {
    multithread->SetMultithreadProtected(TRUE);
    LOG_INFO("GraphicsDevice", "Enabled D3D11Multithread Protection on context");
  } else {
    // 古いWindows向けにD3D10のマルチスレッド保護も試行
    Microsoft::WRL::ComPtr<ID3D10Multithread> mt10;
    if (SUCCEEDED(m_device.As(&mt10))) {
      mt10->SetMultithreadProtected(TRUE);
      LOG_INFO("GraphicsDevice", "Enabled D3D10Multithread Protection on device");
    }
  }

  if (m_device) {
    HRESULT reason = m_device->GetDeviceRemovedReason();
    if (reason != S_OK) {
      LOG_ERROR("GraphicsDevice", "Device already removed (reason={:08X})",
                static_cast<uint32_t>(reason));
      return false;
    }
  }

  return true;
}

std::vector<AdapterInfo> GraphicsDevice::EnumerateAdapters() {
  std::vector<AdapterInfo> result;

  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return result;
  }

  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> adapter;
    const HRESULT enumResult = factory->EnumAdapters1(index, &adapter);
    if (enumResult == DXGI_ERROR_NOT_FOUND || FAILED(enumResult)) {
      break;
    }
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(adapter->GetDesc1(&description)) ||
        (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
      continue;
    }
    AdapterInfo info;
    info.name = description.Description;
    info.dedicatedVideoMemoryBytes =
        static_cast<uint64_t>(description.DedicatedVideoMemory);
    result.push_back(std::move(info));
  }

  return result;
}

void GraphicsDevice::CaptureAdapterInfo() {
  m_adapterName = "Unknown";
  m_dedicatedVideoMemoryBytes = 0;
  if (!m_device) {
    return;
  }

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  DXGI_ADAPTER_DESC description{};
  if (SUCCEEDED(m_device.As(&dxgiDevice)) &&
      SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
      SUCCEEDED(adapter->GetDesc(&description))) {
    m_adapterName = WideToUtf8(description.Description);
    m_dedicatedVideoMemoryBytes =
        static_cast<uint64_t>(description.DedicatedVideoMemory);
  }
}



} // namespace graphics

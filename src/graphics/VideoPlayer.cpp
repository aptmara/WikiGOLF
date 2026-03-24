#include "VideoPlayer.h"
#include <windows.h>
#include <shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace graphics {

using Microsoft::WRL::ComPtr;

VideoPlayer::VideoPlayer() {}

VideoPlayer::~VideoPlayer() {
  Stop();
}

void VideoPlayer::Stop() {
  m_stopThread = true;
  if (m_decodeThread.joinable()) {
    m_decodeThread.join();
  }
  m_reader.Reset();
  m_texture.Reset();
  m_srv.Reset();
}

bool VideoPlayer::Initialize(ID3D11Device* device, const std::string& filePath) {
  if (!device) return false;
  m_device = device;
  m_stopThread = false;
  m_isFinished = false;
  m_currentPlaybackTime = 0;
  while(!m_frameQueue.empty()) m_frameQueue.pop();

  static bool mfInitialized = false;
  if (!mfInitialized) {
    if (FAILED(MFStartup(MF_VERSION))) {
      LOG_ERROR("VideoPlayer", "MFStartup failed");
      return false;
    }
    mfInitialized = true;
  }

  // Convert path to wide string
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, &filePath[0], (int)filePath.size(), NULL, 0);
  std::wstring wpath(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &filePath[0], (int)filePath.size(), &wpath[0], size_needed);

  ComPtr<IMFAttributes> attributes;
  MFCreateAttributes(&attributes, 1);
  attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

  HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), attributes.Get(), &m_reader);
  if (FAILED(hr)) {
    LOG_ERROR("VideoPlayer", "Failed to create source reader for: {}", filePath);
    return false;
  }

  // Set output media type to RGB32
  ComPtr<IMFMediaType> mediaType;
  MFCreateMediaType(&mediaType);
  mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  
  hr = m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mediaType.Get());
  if (FAILED(hr)) {
    LOG_ERROR("VideoPlayer", "Failed to set media type to RGB32");
    return false;
  }

  // Get current media type to extract width, height, and stride
  ComPtr<IMFMediaType> currentType;
  hr = m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);
  if (FAILED(hr)) {
    LOG_ERROR("VideoPlayer", "Failed to get current media type");
    return false;
  }

  UINT32 width = 0, height = 0;
  MFGetAttributeSize(currentType.Get(), MF_MT_FRAME_SIZE, &width, &height);
  m_width = width;
  m_height = height;

  UINT32 tempStride = 0;
  hr = currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &tempStride);
  m_stride = (LONG)tempStride;
  if (FAILED(hr)) {
    // Fallback if stride is not set (usually width * 4 for RGB32)
    m_stride = m_width * 4;
  }

  if (!CreateTexture(m_width, m_height)) {
    return false;
  }

  // Start decode thread
  m_decodeThread = std::thread(&VideoPlayer::DecodeThreadFunc, this);
  
  LOG_INFO("VideoPlayer", "Initialized streaming for video: {} ({}x{})", filePath, m_width, m_height);
  return true;
}

bool VideoPlayer::CreateTexture(int width, int height) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; 
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_texture);
  if (FAILED(hr)) {
    LOG_ERROR("VideoPlayer", "Failed to create texture");
    return false;
  }

  hr = m_device->CreateShaderResourceView(m_texture.Get(), nullptr, &m_srv);
  if (FAILED(hr)) {
    LOG_ERROR("VideoPlayer", "Failed to create SRV");
    return false;
  }

  return true;
}

void VideoPlayer::Update(ID3D11DeviceContext* context, float dt) {
  if (!m_texture || !context) return;

  // dt is in seconds. Convert to 100-nanosecond units (1s = 10,000,000 units)
  m_currentPlaybackTime += (LONGLONG)(dt * 10000000LL);

  ComPtr<IMFSample> latestSample;
  LONGLONG latestTimestamp = -1;

  // Extract frames from queue that are due to be displayed
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_frameQueue.empty()) {
      auto& frame = m_frameQueue.front();
      if (frame.timestamp <= m_currentPlaybackTime) {
        latestSample = frame.sample;
        latestTimestamp = frame.timestamp;
        m_frameQueue.pop();
      } else {
        break; // Next frame is in the future
      }
    }
  }

  // If we got a new frame to display, upload to D3D11 Texture
  if (latestSample) {
    ComPtr<IMFMediaBuffer> buffer;
    latestSample->ConvertToContiguousBuffer(&buffer);
    if (buffer) {
      BYTE* data = nullptr;
      DWORD length = 0;
      if (FAILED(buffer->Lock(&data, nullptr, &length))) {
        return;
      }

      // RGB32 can be top-down or bottom-up
      // If stride is positive, it's top-down. If negative, it's bottom-up.
      int stride = (int)m_stride;
      bool bottomUp = (stride < 0);
      if (bottomUp) stride = -stride;

      size_t bufferSize = (size_t)m_width * m_height * 4;
      if (m_frameUploadBuffer.size() != bufferSize) {
        m_frameUploadBuffer.resize(bufferSize);
      }

      for (int y = 0; y < m_height; ++y) {
        int srcY = bottomUp ? (m_height - 1 - y) : y;
        memcpy(m_frameUploadBuffer.data() + y * m_width * 4, data + srcY * stride, m_width * 4);
      }

      // Force alpha to 255 to ensure D2D/D3D blend doesn't make it transparent
      uint32_t* pixels = reinterpret_cast<uint32_t*>(m_frameUploadBuffer.data());
      for (size_t i = 0; i < (size_t)m_width * m_height; ++i) {
        pixels[i] |= 0xFF000000;
      }

      context->UpdateSubresource(m_texture.Get(), 0, nullptr, m_frameUploadBuffer.data(), m_width * 4, 0);

      if (FAILED(buffer->Unlock())) {
        return;
      }
    }
  }
}

void VideoPlayer::DecodeThreadFunc() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  while (!m_stopThread) {
    bool queueFull = false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      // Keep up to 5 frames in the queue
      queueFull = (m_frameQueue.size() >= 5);
    }

    if (queueFull) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    ComPtr<IMFSample> sample;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    
    HRESULT hr = m_reader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        nullptr,
        &flags,
        &timestamp,
        &sample
    );

    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
      m_isFinished = true;
      break;
    }

    if (sample) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_frameQueue.push({sample, timestamp});
    }
  }
  CoUninitialize();
}

} // namespace graphics

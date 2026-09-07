/**
 * @file ResourceManagerShaders.cpp
 * @brief 統合リソース管理クラスの実装
*/

#include "ResourceManager.h"
#include "ResourceManagerInternals.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"
#include "../graphics/GraphicsDevice.h"
#include <chrono>
#include <filesystem>

namespace resources {

ShaderHandle ResourceManager::LoadShader(const std::string &name,
                                         const std::wstring &vsPath,
                                         const std::wstring &psPath) {
  const auto startedAt = std::chrono::steady_clock::now();
  if (auto it = m_shaderCache.find(name); it != m_shaderCache.end()) {
    // LOG_DEBUG("Resource", "LoadShader cache hit: {} ({} ms)", name,
    //           ElapsedMs(startedAt));
    return it->second;
  }

  // 将来の拡張性を考慮しつつ現在は標準的な入力レイアウトを使用してコンパイル
  auto inputLayout = graphics::Shader::GetDefaultInputLayout();

  // コンパイル（存在しなければ Assets/ パスをフォールバック）
  graphics::Shader shader;
  auto tryCompile = [&](const std::wstring &vs, const std::wstring &ps) {
    return shader.LoadFromFile(m_device.GetDevice(), vs, "main", ps, "main",
                               inputLayout);
  };

  std::wstring vsUsed = vsPath;
  std::wstring psUsed = psPath;
  bool success = tryCompile(vsPath, psPath);

  if (!success) {
    std::filesystem::path vsAlt = std::filesystem::path(L"Assets") / vsPath;
    std::filesystem::path psAlt = std::filesystem::path(L"Assets") / psPath;
    if (std::filesystem::exists(vsAlt) && std::filesystem::exists(psAlt)) {
      vsUsed = vsAlt.wstring();
      psUsed = psAlt.wstring();
      success = tryCompile(vsUsed, psUsed);
    }
  }

  if (!success) {
    LOG_ERROR("Resource", "Failed to compile shader: {} (VS: {}, PS: {})", name,
              core::ToString(vsUsed), core::ToString(psUsed));
    return {};
  }

  auto handle = m_shaderPool.Add(std::move(shader));
  m_shaderCache[name] = handle;
  LOG_INFO("Resource", "Loaded Shader: {} ({} ms)", name, ElapsedMs(startedAt));
  return handle;
}

ShaderHandle ResourceManager::FindShader(const std::string &name) const {
  if (auto it = m_shaderCache.find(name); it != m_shaderCache.end()) {
    return it->second;
  }
  return {};
}

void ResourceManager::Clear() {
  m_meshPool.Clear();
  m_meshCache.clear();
  m_shaderPool.Clear();
  m_shaderCache.clear();
  m_audioPool.Clear();
  m_audioCache.clear();
  m_textureCache.clear();
}

void ResourceManager::DumpStatistics() const {
  LOG_INFO("ResourceStats", "=== Resource Statistics ===");
  LOG_INFO("ResourceStats", "Meshes: {} loaded", m_meshCache.size());
  for (const auto &[name, handle] : m_meshCache) {
    LOG_INFO("ResourceStats", "  - {} (ID:{})", name.c_str(), handle.index);
  }

  LOG_INFO("ResourceStats", "Shaders: {} loaded", m_shaderCache.size());
  for (const auto &[name, handle] : m_shaderCache) {
    LOG_INFO("ResourceStats", "  - {} (ID:{})", name.c_str(), handle.index);
  }

  LOG_INFO("ResourceStats", "Audio: {} loaded", m_audioCache.size());
  for (const auto &[name, handle] : m_audioCache) {
    LOG_INFO("ResourceStats", "  - {} (ID:{})", name.c_str(), handle.index);
  }
  LOG_INFO("ResourceStats", "===========================");
}

} // namespace resources

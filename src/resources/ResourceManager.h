#pragma once
/**
 * @file ResourceManager.h
 * @brief 統合リソース管理クラス
 */

#include "../audio/AudioClip.h"
#include "../graphics/Mesh.h"
#include "../graphics/Shader.h"
#include "ResourcePool.h"
#include <string>
#include <unordered_map>


namespace graphics {
class GraphicsDevice;
}

namespace resources {

using MeshHandle = core::ResourceHandle<graphics::Mesh>;
using ShaderHandle = core::ResourceHandle<graphics::Shader>;
using AudioHandle = core::ResourceHandle<audio::AudioClip>;

class ResourceManager {
public:
  ResourceManager(graphics::GraphicsDevice &device);
  ~ResourceManager() = default;

  /// @brief メッシュをロード（キャッシュ時は既存ハンドルを返す）
  /// @param path ファイルパス または "builtin/cube" などの特殊コマンド
  MeshHandle LoadMesh(const std::string &path);

  /// @brief メッシュを取得（レンダリングループ用）
  graphics::Mesh *GetMesh(MeshHandle handle);

  /// @brief シェーダーをロード
  ShaderHandle LoadShader(const std::string &name, const std::wstring &vsPath,
                          const std::wstring &psPath);

  /// @brief シェーダーを取得
  graphics::Shader *GetShader(ShaderHandle handle);

  /// @brief 音声をロード（WAVのみ対応）
  AudioHandle LoadAudio(const std::string &path);

  /// @brief 音声を取得
  audio::AudioClip *GetAudio(AudioHandle handle);

  /// @brief 全リソースを解放（シーン遷移用）
  void Clear();

  /// @brief リソース統計情報をログ出力
  void DumpStatistics() const;

private:
  graphics::GraphicsDevice &m_device;

  ResourcePool<graphics::Mesh> m_meshPool;
  std::unordered_map<std::string, MeshHandle> m_meshCache;

  ResourcePool<graphics::Shader> m_shaderPool;
  std::unordered_map<std::string, ShaderHandle> m_shaderCache;

  ResourcePool<audio::AudioClip> m_audioPool;
  std::unordered_map<std::string, AudioHandle> m_audioCache;
};

} // namespace resources

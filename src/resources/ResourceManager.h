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
#include <wrl/client.h>


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

  /// @brief 動的にメッシュを作成して登録
  MeshHandle CreateDynamicMesh(const std::string &name,
                               const std::vector<graphics::Vertex> &vertices,
                               const std::vector<uint32_t> &indices);

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

  /// @brief テクスチャをSRVとしてロード（キャッシュ付き）
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
  LoadTextureSRV(const std::string &path);

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

  std::unordered_map<std::string,
                     Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>
      m_textureCache;
};

} // namespace resources

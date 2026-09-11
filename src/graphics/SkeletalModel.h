#pragma once
/**
 * @file SkeletalModel.h
 * @brief ボーン・スキニング・アニメーションクリップ付きモデル(glTF/FBX等)の読み込みと
 *        CPUスキニングによるポーズ計算
*/

#include "Mesh.h"
#include <DirectXMath.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphics {

/** @brief スケルトンの1ノード（ボーンとは限らない。Transformノード全般） */
struct SkeletonNode {
  std::string name;
  int parentIndex = -1;
  std::vector<int> children;
  DirectX::XMFLOAT4X4 localBindTransform; ///< バインドポーズ時のローカル変換
  DirectX::XMFLOAT3 bindScale{1.0f, 1.0f, 1.0f};
  DirectX::XMFLOAT4 bindRotation{0.0f, 0.0f, 0.0f, 1.0f};
  DirectX::XMFLOAT3 bindTranslation{0.0f, 0.0f, 0.0f};
};

/** @brief スキニング用ボーン（スケルトンノードの一部を指す） */
struct Bone {
  std::string name;
  int nodeIndex = -1;
  DirectX::XMFLOAT4X4 offsetMatrix; ///< メッシュ空間 -> ボーン空間
};

/** @brief 頂点ごとのボーン影響（最大4本） */
struct VertexBoneWeights {
  uint32_t boneIndices[4] = {0, 0, 0, 0};
  float boneWeights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/** @brief Vector3のキーフレーム（秒単位の時刻） */
struct AnimKeyVec3 {
  float time = 0.0f;
  DirectX::XMFLOAT3 value{};
};

/** @brief 四元数のキーフレーム（秒単位の時刻） */
struct AnimKeyQuat {
  float time = 0.0f;
  DirectX::XMFLOAT4 value{};
};

/** @brief 1ノード分のアニメーションチャンネル */
struct AnimationChannel {
  int nodeIndex = -1;
  std::vector<AnimKeyVec3> positionKeys;
  std::vector<AnimKeyQuat> rotationKeys;
  std::vector<AnimKeyVec3> scaleKeys;
};

/** @brief 1本のアニメーションクリップ */
struct AnimationClip {
  std::string name;
  float duration = 0.0f; ///< 秒
  std::vector<AnimationChannel> channels;
};

/**
 * @brief スキンメッシュ+アニメーションクリップを保持し、CPUスキニングで
 *        任意時刻のポーズ頂点を計算するクラス。
 * @details 本エンジンの描画パイプラインはGPUスキニングに対応していないため、
 *          毎フレームCPU側でボーン行列を評価し、頂点を変形してから
 *          動的頂点バッファ(Mesh::UpdateVertices)へ書き込む方式を取る。
*/
class SkeletalModel {
public:
  /** @brief ファイルからスキンメッシュ+アニメーションを読み込む(Assimp使用) */
  bool LoadFromFile(const std::string &path);

  /** @brief バインドポーズの頂点（アニメーション未適用の初期形状） */
  const std::vector<Vertex> &GetBindPoseVertices() const {
    return m_bindVertices;
  }

  /** @brief インデックスバッファ */
  const std::vector<uint32_t> &GetIndices() const { return m_indices; }

  /** @brief 名前でアニメーションクリップを検索（見つからなければnullptr） */
  const AnimationClip *FindClip(const std::string &name) const;

  /** @brief 読み込まれているクリップ名の一覧 */
  std::vector<std::string> GetClipNames() const;

  /**
   * @brief 指定クリップ・時刻での姿勢を計算し、頂点配列へ書き出す
   * @param clip 対象クリップ（nullptrの場合はバインドポーズをそのまま出力）
   * @param timeSeconds クリップ内時刻（秒）。ループ再生時は呼び出し側で
   *        [0, duration) に丸めてから渡すこと。
   * @param outVertices 出力先（サイズはGetBindPoseVertices()と同じになる）
  */
  void ComputePose(const AnimationClip *clip, float timeSeconds,
                   std::vector<Vertex> &outVertices) const;

  /**
   * @brief 2つのクリップの姿勢をノード単位(TRS)で補間して頂点を計算する（クロスフェード用）
   * @param toWeight 0でfrom、1でtoの姿勢。clipがnullptrの側はバインドポーズ。
  */
  void ComputeBlendedPose(const AnimationClip *fromClip, float fromTime,
                          const AnimationClip *toClip, float toTime,
                          float toWeight,
                          std::vector<Vertex> &outVertices) const;

  /** @brief バインドポーズのバウンディングボックス高さ(Y方向)。スケール調整用 */
  float GetBindPoseHeight() const { return m_bindPoseHeight; }

  bool IsValid() const { return !m_bindVertices.empty() && !m_bones.empty(); }

  /**
   * @brief 埋め込みベースカラーテクスチャの生バイト列(png/jpg等の圧縮データそのまま)
   * @details 埋め込みテクスチャが無い場合は空。デコードはResourceManager側で行う。
  */
  const std::vector<uint8_t> &GetBaseColorTextureBytes() const {
    return m_baseColorTextureBytes;
  }

private:
  struct PoseLayer {
    const AnimationClip *clip = nullptr;
    float time = 0.0f;
  };

  /** @brief ノードのローカルTRSをクリップから取得（チャンネルが無ければバインドポーズ）*/
  void SampleLocalTRS(int nodeIndex, const PoseLayer &layer,
                      DirectX::XMVECTOR &outScale, DirectX::XMVECTOR &outRotation,
                      DirectX::XMVECTOR &outTranslation) const;

  void ComputeGlobalTransforms(int nodeIndex,
                               const DirectX::XMMATRIX &parentGlobal,
                               const PoseLayer &from, const PoseLayer &to,
                               float toWeight,
                               std::vector<DirectX::XMMATRIX> &outGlobal) const;

  std::vector<Vertex> m_bindVertices;
  std::vector<uint32_t> m_indices;
  std::vector<VertexBoneWeights> m_vertexWeights; // m_bindVerticesと1対1

  std::vector<SkeletonNode> m_nodes;
  std::unordered_map<std::string, int> m_nodeNameToIndex;

  std::vector<Bone> m_bones;
  std::unordered_map<std::string, int> m_boneNameToIndex;

  std::vector<AnimationClip> m_clips;
  std::unordered_map<std::string, int> m_clipNameToIndex;

  DirectX::XMFLOAT4X4 m_globalInverseTransform;
  float m_bindPoseHeight = 1.0f;
  std::vector<uint8_t> m_baseColorTextureBytes;
};

} // namespace graphics

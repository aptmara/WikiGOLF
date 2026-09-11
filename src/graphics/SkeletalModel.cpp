/**
 * @file SkeletalModel.cpp
 * @brief ボーン・スキニング・アニメーションクリップ付きモデルの読み込みと
 *        CPUスキニングによるポーズ計算の実装
*/

#include "SkeletalModel.h"
#include "../core/Logger.h"
#include "TangentGenerator.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cstdlib>
#include <execution>
#include <functional>
#include <numeric>

namespace graphics {

using namespace DirectX;

namespace {

/**
 * @brief Assimpの行列(列ベクトル規約・行優先メモリ)をDirectXMathの行ベクトル規約に変換する。
 * @details 転置して読み込むことで、DirectXMath流の "v' = v * M" 合成順序
 *          (子ローカル行列 * 親グローバル行列) がAssimp側の階層構造と一致するようになる。
*/
XMMATRIX ToXM(const aiMatrix4x4 &m) {
  return XMMatrixSet(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3,
                     m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

struct MeshInstance {
  const aiMesh *mesh;
  uint32_t baseIndex;
};

/**
 * @brief マテリアルのベースカラー(PBRのbaseColorFactor)を取得する。
 * @details 本モデルはパーツごとに単色のbaseColorFactorで塗り分けられており、
 *          画像テクスチャを持たないため、この色を頂点カラーとして焼き込む。
*/
XMFLOAT4 GetMaterialBaseColor(const aiScene *scene, unsigned int materialIndex) {
  if (materialIndex >= scene->mNumMaterials) {
    return {1.0f, 1.0f, 1.0f, 1.0f};
  }
  const aiMaterial *material = scene->mMaterials[materialIndex];

  aiColor4D color;
  if (material->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS) {
    return {color.r, color.g, color.b, color.a};
  }
  if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
    return {color.r, color.g, color.b, color.a};
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

void ExtractMeshVertices(const aiMesh *mesh, std::vector<Vertex> &outVertices,
                         std::vector<uint32_t> &outIndices,
                         uint32_t baseIndex, const XMFLOAT4 &materialColor) {
  for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
    Vertex vertex{};
    vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                       mesh->mVertices[i].z};

    if (mesh->HasNormals()) {
      vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y,
                       mesh->mNormals[i].z};
    } else {
      vertex.normal = {0.0f, 1.0f, 0.0f};
    }

    if (mesh->HasTextureCoords(0)) {
      vertex.texCoord = {mesh->mTextureCoords[0][i].x,
                         mesh->mTextureCoords[0][i].y};
    } else {
      vertex.texCoord = {0.0f, 0.0f};
    }

    if (mesh->HasVertexColors(0)) {
      // 頂点カラーとマテリアルのbaseColorFactorを乗算する（glTFの合成規則どおり）
      vertex.color = {mesh->mColors[0][i].r * materialColor.x,
                      mesh->mColors[0][i].g * materialColor.y,
                      mesh->mColors[0][i].b * materialColor.z,
                      mesh->mColors[0][i].a * materialColor.w};
    } else {
      vertex.color = materialColor;
    }

    outVertices.push_back(vertex);
  }

  for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
    const aiFace &face = mesh->mFaces[i];
    for (unsigned int j = 0; j < face.mNumIndices; j++) {
      outIndices.push_back(baseIndex + face.mIndices[j]);
    }
  }
}

XMVECTOR InterpolateVec3(const std::vector<AnimKeyVec3> &keys, float t,
                         XMVECTOR fallback) {
  if (keys.empty()) {
    return fallback;
  }
  if (keys.size() == 1 || t <= keys.front().time) {
    return XMLoadFloat3(&keys.front().value);
  }
  if (t >= keys.back().time) {
    return XMLoadFloat3(&keys.back().value);
  }
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (t >= keys[i].time && t <= keys[i + 1].time) {
      const float span = keys[i + 1].time - keys[i].time;
      const float f = span > 1e-6f ? (t - keys[i].time) / span : 0.0f;
      XMVECTOR a = XMLoadFloat3(&keys[i].value);
      XMVECTOR b = XMLoadFloat3(&keys[i + 1].value);
      return XMVectorLerp(a, b, f);
    }
  }
  return XMLoadFloat3(&keys.back().value);
}

XMVECTOR InterpolateQuat(const std::vector<AnimKeyQuat> &keys, float t,
                         XMVECTOR fallback) {
  if (keys.empty()) {
    return fallback;
  }
  if (keys.size() == 1 || t <= keys.front().time) {
    return XMLoadFloat4(&keys.front().value);
  }
  if (t >= keys.back().time) {
    return XMLoadFloat4(&keys.back().value);
  }
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    if (t >= keys[i].time && t <= keys[i + 1].time) {
      const float span = keys[i + 1].time - keys[i].time;
      const float f = span > 1e-6f ? (t - keys[i].time) / span : 0.0f;
      XMVECTOR a = XMLoadFloat4(&keys[i].value);
      XMVECTOR b = XMLoadFloat4(&keys[i + 1].value);
      return XMQuaternionSlerp(a, b, f);
    }
  }
  return XMLoadFloat4(&keys.back().value);
}

} // namespace

bool SkeletalModel::LoadFromFile(const std::string &path) {
  Assimp::Importer importer;
  unsigned int flags = aiProcess_Triangulate | aiProcess_GenNormals |
                       aiProcess_FlipUVs | aiProcess_LimitBoneWeights;

  const aiScene *scene = importer.ReadFile(path, flags);
  if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
      !scene->mRootNode) {
    LOG_ERROR("SkeletalModel", "Assimpエラー: {}", importer.GetErrorString());
    return false;
  }

  m_bindVertices.clear();
  m_indices.clear();
  m_vertexWeights.clear();
  m_nodes.clear();
  m_nodeNameToIndex.clear();
  m_bones.clear();
  m_boneNameToIndex.clear();
  m_clips.clear();
  m_clipNameToIndex.clear();

  std::vector<MeshInstance> meshInstances;

  std::function<int(const aiNode *, int)> buildNode =
      [&](const aiNode *node, int parentIndex) -> int {
    SkeletonNode skelNode;
    skelNode.name = node->mName.C_Str();
    skelNode.parentIndex = parentIndex;
    const XMMATRIX bindLocal = ToXM(node->mTransformation);
    XMStoreFloat4x4(&skelNode.localBindTransform, bindLocal);
    XMVECTOR bindS, bindR, bindT;
    if (XMMatrixDecompose(&bindS, &bindR, &bindT, bindLocal)) {
      XMStoreFloat3(&skelNode.bindScale, bindS);
      XMStoreFloat4(&skelNode.bindRotation, bindR);
      XMStoreFloat3(&skelNode.bindTranslation, bindT);
    }

    const int index = static_cast<int>(m_nodes.size());
    m_nodes.push_back(skelNode);
    if (!skelNode.name.empty()) {
      m_nodeNameToIndex[skelNode.name] = index;
    }

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
      const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
      const uint32_t baseIndex = static_cast<uint32_t>(m_bindVertices.size());
      const XMFLOAT4 materialColor =
          GetMaterialBaseColor(scene, mesh->mMaterialIndex);
      ExtractMeshVertices(mesh, m_bindVertices, m_indices, baseIndex,
                         materialColor);
      meshInstances.push_back({mesh, baseIndex});
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
      const int childIndex = buildNode(node->mChildren[i], index);
      m_nodes[index].children.push_back(childIndex);
    }
    return index;
  };

  buildNode(scene->mRootNode, -1);

  // 埋め込みベースカラーテクスチャの抽出（GLB内蔵の圧縮画像バイト列をそのまま保持）。
  // 複数マテリアルのうちテクスチャを持つものを探す
  // （例: パーツは単色baseColorFactorだが、装飾デカール用マテリアルだけ
  //   テクスチャを持つ、といった構成に対応するため）。
  for (unsigned int m = 0; m < scene->mNumMaterials && m_baseColorTextureBytes.empty();
       ++m) {
    const aiMaterial *material = scene->mMaterials[m];
    aiString texPath;
    const bool found =
        material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) ==
            AI_SUCCESS ||
        material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS;
    if (!found) {
      continue;
    }
    const std::string texPathStr = texPath.C_Str();
    if (texPathStr.empty() || texPathStr[0] != '*') {
      continue;
    }
    const int texIndex = std::atoi(texPathStr.c_str() + 1);
    if (texIndex < 0 || static_cast<unsigned int>(texIndex) >= scene->mNumTextures) {
      continue;
    }
    const aiTexture *tex = scene->mTextures[texIndex];
    if (tex->mHeight == 0 && tex->pcData) {
      // mHeight==0 は png/jpg 等の圧縮バイト列がそのまま入っていることを示す
      const uint8_t *bytes = reinterpret_cast<const uint8_t *>(tex->pcData);
      m_baseColorTextureBytes.assign(bytes, bytes + tex->mWidth);
    }
  }

  m_vertexWeights.assign(m_bindVertices.size(), VertexBoneWeights{});

  for (const auto &inst : meshInstances) {
    for (unsigned int b = 0; b < inst.mesh->mNumBones; ++b) {
      const aiBone *bone = inst.mesh->mBones[b];
      const std::string boneName = bone->mName.C_Str();

      int boneIndex;
      auto foundBone = m_boneNameToIndex.find(boneName);
      if (foundBone != m_boneNameToIndex.end()) {
        boneIndex = foundBone->second;
      } else {
        Bone newBone;
        newBone.name = boneName;
        auto nodeIt = m_nodeNameToIndex.find(boneName);
        newBone.nodeIndex =
            (nodeIt != m_nodeNameToIndex.end()) ? nodeIt->second : 0;
        XMStoreFloat4x4(&newBone.offsetMatrix, ToXM(bone->mOffsetMatrix));
        boneIndex = static_cast<int>(m_bones.size());
        m_bones.push_back(newBone);
        m_boneNameToIndex[boneName] = boneIndex;
      }

      for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
        const aiVertexWeight &vw = bone->mWeights[w];
        const uint32_t vertexIndex = inst.baseIndex + vw.mVertexId;
        if (vertexIndex >= m_vertexWeights.size()) {
          continue;
        }
        auto &weights = m_vertexWeights[vertexIndex];
        for (int slot = 0; slot < 4; ++slot) {
          if (weights.boneWeights[slot] <= 0.0f) {
            weights.boneIndices[slot] = static_cast<uint32_t>(boneIndex);
            weights.boneWeights[slot] = vw.mWeight;
            break;
          }
        }
      }
    }
  }

  for (auto &weights : m_vertexWeights) {
    const float sum = weights.boneWeights[0] + weights.boneWeights[1] +
                      weights.boneWeights[2] + weights.boneWeights[3];
    if (sum > 1e-5f) {
      for (float &wgt : weights.boneWeights) {
        wgt /= sum;
      }
    }
  }

  if (!m_nodes.empty()) {
    XMMATRIX rootGlobal = XMLoadFloat4x4(&m_nodes[0].localBindTransform);
    XMStoreFloat4x4(&m_globalInverseTransform,
                    XMMatrixInverse(nullptr, rootGlobal));
  } else {
    XMStoreFloat4x4(&m_globalInverseTransform, XMMatrixIdentity());
  }

  for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
    const aiAnimation *anim = scene->mAnimations[a];
    AnimationClip clip;
    clip.name = anim->mName.C_Str();
    const double ticksPerSecond =
        anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 1.0;
    clip.duration = static_cast<float>(anim->mDuration / ticksPerSecond);

    for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
      const aiNodeAnim *channel = anim->mChannels[c];
      auto nodeIt = m_nodeNameToIndex.find(channel->mNodeName.C_Str());
      if (nodeIt == m_nodeNameToIndex.end()) {
        continue;
      }

      AnimationChannel outChannel;
      outChannel.nodeIndex = nodeIt->second;

      outChannel.positionKeys.reserve(channel->mNumPositionKeys);
      for (unsigned int k = 0; k < channel->mNumPositionKeys; ++k) {
        const auto &key = channel->mPositionKeys[k];
        outChannel.positionKeys.push_back(
            {static_cast<float>(key.mTime / ticksPerSecond),
             XMFLOAT3{key.mValue.x, key.mValue.y, key.mValue.z}});
      }

      outChannel.rotationKeys.reserve(channel->mNumRotationKeys);
      for (unsigned int k = 0; k < channel->mNumRotationKeys; ++k) {
        const auto &key = channel->mRotationKeys[k];
        outChannel.rotationKeys.push_back(
            {static_cast<float>(key.mTime / ticksPerSecond),
             XMFLOAT4{key.mValue.x, key.mValue.y, key.mValue.z,
                      key.mValue.w}});
      }

      outChannel.scaleKeys.reserve(channel->mNumScalingKeys);
      for (unsigned int k = 0; k < channel->mNumScalingKeys; ++k) {
        const auto &key = channel->mScalingKeys[k];
        outChannel.scaleKeys.push_back(
            {static_cast<float>(key.mTime / ticksPerSecond),
             XMFLOAT3{key.mValue.x, key.mValue.y, key.mValue.z}});
      }

      clip.channels.push_back(std::move(outChannel));
    }

    m_clipNameToIndex[clip.name] = static_cast<int>(m_clips.size());
    m_clips.push_back(std::move(clip));
  }

  ComputeTangents(m_bindVertices, m_indices);

  float minY = m_bindVertices.empty() ? 0.0f : m_bindVertices.front().position.y;
  float maxY = minY;
  for (const auto &v : m_bindVertices) {
    minY = (std::min)(minY, v.position.y);
    maxY = (std::max)(maxY, v.position.y);
  }
  m_bindPoseHeight = (std::max)(maxY - minY, 0.001f);

  LOG_INFO("SkeletalModel",
          "ロード成功: {} (頂点:{}, ボーン:{}, クリップ:{})", path,
          m_bindVertices.size(), m_bones.size(), m_clips.size());

  return !m_bindVertices.empty();
}

const AnimationClip *SkeletalModel::FindClip(const std::string &name) const {
  auto it = m_clipNameToIndex.find(name);
  if (it == m_clipNameToIndex.end()) {
    return nullptr;
  }
  return &m_clips[it->second];
}

std::vector<std::string> SkeletalModel::GetClipNames() const {
  std::vector<std::string> names;
  names.reserve(m_clips.size());
  for (const auto &clip : m_clips) {
    names.push_back(clip.name);
  }
  return names;
}

void SkeletalModel::SampleLocalTRS(int nodeIndex, const PoseLayer &layer,
                                   XMVECTOR &outScale, XMVECTOR &outRotation,
                                   XMVECTOR &outTranslation) const {
  const SkeletonNode &node = m_nodes[nodeIndex];
  const XMVECTOR bindS = XMLoadFloat3(&node.bindScale);
  const XMVECTOR bindR = XMLoadFloat4(&node.bindRotation);
  const XMVECTOR bindT = XMLoadFloat3(&node.bindTranslation);

  if (layer.clip) {
    for (const auto &channel : layer.clip->channels) {
      if (channel.nodeIndex == nodeIndex) {
        outScale = InterpolateVec3(channel.scaleKeys, layer.time, bindS);
        outRotation = InterpolateQuat(channel.rotationKeys, layer.time, bindR);
        outTranslation =
            InterpolateVec3(channel.positionKeys, layer.time, bindT);
        return;
      }
    }
  }
  outScale = bindS;
  outRotation = bindR;
  outTranslation = bindT;
}

void SkeletalModel::ComputeGlobalTransforms(
    int nodeIndex, const XMMATRIX &parentGlobal, const PoseLayer &from,
    const PoseLayer &to, float toWeight,
    std::vector<XMMATRIX> &outGlobal) const {
  XMVECTOR s, r, t;
  if (toWeight >= 1.0f) {
    SampleLocalTRS(nodeIndex, to, s, r, t);
  } else if (toWeight <= 0.0f) {
    SampleLocalTRS(nodeIndex, from, s, r, t);
  } else {
    XMVECTOR s0, r0, t0, s1, r1, t1;
    SampleLocalTRS(nodeIndex, from, s0, r0, t0);
    SampleLocalTRS(nodeIndex, to, s1, r1, t1);
    s = XMVectorLerp(s0, s1, toWeight);
    r = XMQuaternionNormalize(XMQuaternionSlerp(r0, r1, toWeight));
    t = XMVectorLerp(t0, t1, toWeight);
  }

  const XMMATRIX local = XMMatrixScalingFromVector(s) *
                         XMMatrixRotationQuaternion(r) *
                         XMMatrixTranslationFromVector(t);
  const XMMATRIX global = XMMatrixMultiply(local, parentGlobal);
  outGlobal[nodeIndex] = global;

  for (int child : m_nodes[nodeIndex].children) {
    ComputeGlobalTransforms(child, global, from, to, toWeight, outGlobal);
  }
}

void SkeletalModel::ComputePose(const AnimationClip *clip, float timeSeconds,
                                std::vector<Vertex> &outVertices) const {
  ComputeBlendedPose(nullptr, 0.0f, clip, timeSeconds, 1.0f, outVertices);
}

void SkeletalModel::ComputeBlendedPose(const AnimationClip *fromClip,
                                       float fromTime,
                                       const AnimationClip *toClip,
                                       float toTime, float toWeight,
                                       std::vector<Vertex> &outVertices) const {
  // UV・頂点カラーは変化しないため、サイズが同じなら初回以降のコピーを省く
  if (outVertices.size() != m_bindVertices.size()) {
    outVertices = m_bindVertices;
  }
  if (m_bones.empty() || m_nodes.empty()) {
    return;
  }

  std::vector<XMMATRIX> globalTransforms(m_nodes.size(), XMMatrixIdentity());
  ComputeGlobalTransforms(0, XMMatrixIdentity(), PoseLayer{fromClip, fromTime},
                          PoseLayer{toClip, toTime}, toWeight,
                          globalTransforms);

  const XMMATRIX globalInverse = XMLoadFloat4x4(&m_globalInverseTransform);

  std::vector<XMMATRIX> finalBoneMatrices(m_bones.size());
  for (size_t i = 0; i < m_bones.size(); ++i) {
    const XMMATRIX offset = XMLoadFloat4x4(&m_bones[i].offsetMatrix);
    const XMMATRIX nodeGlobal = globalTransforms[m_bones[i].nodeIndex];
    finalBoneMatrices[i] =
        XMMatrixMultiply(XMMatrixMultiply(offset, nodeGlobal), globalInverse);
  }

  // 頂点ごとに独立しているのでチャンク単位で並列に変形する。
  // 接線/従法線はノーマルマップにしか使われず、スキンメッシュでは
  // ノーマルマップを使わないためバインドポーズの値のまま変形しない。
  const size_t vertexCount = m_bindVertices.size();
  constexpr size_t kChunkSize = 8192;
  std::vector<size_t> chunks((vertexCount + kChunkSize - 1) / kChunkSize);
  std::iota(chunks.begin(), chunks.end(), size_t{0});
  std::for_each(std::execution::par, chunks.begin(), chunks.end(),
                [&](size_t chunk) {
    const size_t end = (std::min)(vertexCount, (chunk + 1) * kChunkSize);
    for (size_t v = chunk * kChunkSize; v < end; ++v) {
      const VertexBoneWeights &w = m_vertexWeights[v];
      const XMVECTOR bindPos = XMLoadFloat3(&m_bindVertices[v].position);
      const XMVECTOR bindNormal = XMLoadFloat3(&m_bindVertices[v].normal);

      XMVECTOR pos;
      XMVECTOR normal;
      if (w.boneWeights[0] >= 0.999f) {
        // 剛体ウェイト(1ボーン100%)の高速パス。本モデルはほぼ全頂点が該当する
        const XMMATRIX &m = finalBoneMatrices[w.boneIndices[0]];
        pos = XMVector3Transform(bindPos, m);
        normal = XMVector3TransformNormal(bindNormal, m);
      } else {
        const float totalWeight = w.boneWeights[0] + w.boneWeights[1] +
                                  w.boneWeights[2] + w.boneWeights[3];
        if (totalWeight < 1e-5f) {
          continue; // 影響ボーンなし: バインドポーズのまま
        }
        pos = XMVectorZero();
        normal = XMVectorZero();
        for (int k = 0; k < 4; ++k) {
          const float weight = w.boneWeights[k];
          if (weight <= 0.0f) {
            continue;
          }
          const XMMATRIX &m = finalBoneMatrices[w.boneIndices[k]];
          pos = XMVectorAdd(pos,
                            XMVectorScale(XMVector3Transform(bindPos, m), weight));
          normal = XMVectorAdd(
              normal, XMVectorScale(XMVector3TransformNormal(bindNormal, m), weight));
        }
        pos = XMVectorScale(pos, 1.0f / totalWeight);
      }

      XMStoreFloat3(&outVertices[v].position, pos);
      XMStoreFloat3(&outVertices[v].normal, XMVector3Normalize(normal));
    }
  });
}

} // namespace graphics

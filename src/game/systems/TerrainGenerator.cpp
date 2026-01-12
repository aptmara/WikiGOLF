#include "TerrainGenerator.h"
#include "../../core/Logger.h"
#include <random>
#include <algorithm>
#include <cmath>

namespace game::systems {

using namespace DirectX;

// ヘルパー：線形補間
static float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// ヘルパー：スムースステップ
static float SmoothStep(float edge0, float edge1, float x) {
    x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

TerrainData TerrainGenerator::GenerateTerrain(
    const std::string& articleText,
    const std::vector<std::pair<std::string, std::wstring>>& links,
    const TerrainConfig& config
) {
    TerrainData data;
    data.config = config;
    
    // ハイトマップ初期化
    int totalVerts = config.resolutionX * config.resolutionZ;
    data.heightMap.resize(totalVerts, 0.0f);
    
    // 1. 基本形状生成 (ノイズ + プラットフォーム)
    GenerateBaseHeightMap(data, articleText);
    
    // 2. リンク位置に基づくプラットフォーム生成
    CreatePlatforms(data, links);
    
    // 3. スムージング処理
    ApplySmoothing(data, 3); // 3回スムージング
    
    // 4. メッシュ生成
    CalculateNormals(data);
    GenerateMesh(data);
    
    return data;
}

void TerrainGenerator::GenerateBaseHeightMap(TerrainData& data, const std::string& text) {
    // 擬似乱数生成器 (記事テキストをシードにする)
    std::seed_seq seed(text.begin(), text.end());
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    int resX = data.config.resolutionX;
    int resZ = data.config.resolutionZ;
    
    // 簡易パーリンノイズ風 (周波数を変えて重ね合わせ)
    for (int z = 0; z < resZ; ++z) {
        for (int x = 0; x < resX; ++x) {
            float nx = (float)x / resX;
            float nz = (float)z / resZ;
            
            // 低周波 (大きな起伏)
            float h1 = std::sin(nx * 3.14f * 2.0f) * std::cos(nz * 3.14f * 2.0f);
            
            // 高周波 (細かな凹凸)
            float h2 = std::sin(nx * 10.0f + nz * 5.0f) * 0.2f;
            
            // 外周を高くする (壁)
            float wallFactor = 0.0f;
            float dx = nx - 0.5f;
            float dz = nz - 0.5f;
            float distFromCenter = std::sqrt(dx*dx + dz*dz) * 2.0f; // 0.0 center -> 1.0 edge
            
            if (distFromCenter > 0.8f) {
                wallFactor = SmoothStep(0.8f, 1.0f, distFromCenter) * 3.0f;
            }
            
            float h = (h1 * 0.5f + h2 * 0.1f) * data.config.heightScale + wallFactor;
            
            // ベース高さ調整
            SetHeight(data, x, z, h + data.config.baseHeight);
        }
    }
}

void TerrainGenerator::CreatePlatforms(TerrainData& data, const std::vector<std::pair<std::string, std::wstring>>& links) {
    int resX = data.config.resolutionX;
    int resZ = data.config.resolutionZ;
    
    // リンク数に応じてプラットフォームを作る
    // ここでは簡易的に、リンクのハッシュ値から位置を決定
    
    for (const auto& link : links) {
        // ハッシュから位置を決定
        std::hash<std::string> hasher;
        size_t h = hasher(link.first);
        
        // 0.2 ~ 0.8 の範囲に配置 (端っこすぎないように)
        float px = 0.2f + (float)(h % 100) / 100.0f * 0.6f;
        float pz = 0.2f + (float)((h / 100) % 100) / 100.0f * 0.6f;
        
        int cx = (int)(px * resX);
        int cz = (int)(pz * resZ);
        
        // プラットフォーム半径
        int radius = resX / 10; // 幅の1/10くらい
        
        // プラットフォームの高さ (現在のその地点の高さの平均にするか、固定するか)
        // ここでは「周囲より少し低くして平らにする（窪地）」アプローチ
        float targetHeight = GetHeight(data, cx, cz) - 0.5f; 
        
        for (int z = cz - radius * 2; z <= cz + radius * 2; ++z) {
            for (int x = cx - radius * 2; x <= cx + radius * 2; ++x) {
                if (x < 0 || x >= resX || z < 0 || z >= resZ) continue;
                
                float dx = (float)(x - cx);
                float dz = (float)(z - cz);
                float dist = std::sqrt(dx*dx + dz*dz);
                
                if (dist < radius) {
                    // 完全な平地
                    // 徐々に平らにする (Lerp)
                    float currentH = GetHeight(data, x, z);
                    SetHeight(data, x, z, Lerp(currentH, targetHeight, 0.8f));
                } else if (dist < radius * 2.0f) {
                    // スムースな接続部
                    float t = SmoothStep(radius, radius * 2.0f, dist);
                    float currentH = GetHeight(data, x, z);
                    // 外側ほど元の高さ、内側ほどターゲット高さ
                    SetHeight(data, x, z, Lerp(targetHeight, currentH, t));
                }
            }
        }
    }
}

void TerrainGenerator::ApplySmoothing(TerrainData& data, int iterations) {
    int resX = data.config.resolutionX;
    int resZ = data.config.resolutionZ;
    std::vector<float> tempMap = data.heightMap;
    
    for (int iter = 0; iter < iterations; ++iter) {
        for (int z = 1; z < resZ - 1; ++z) {
            for (int x = 1; x < resX - 1; ++x) {
                // 3x3 平均
                float sum = 0.0f;
                sum += GetHeight(data, x-1, z-1);
                sum += GetHeight(data, x,   z-1);
                sum += GetHeight(data, x+1, z-1);
                
                sum += GetHeight(data, x-1, z);
                sum += GetHeight(data, x,   z);
                sum += GetHeight(data, x+1, z);
                
                sum += GetHeight(data, x-1, z+1);
                sum += GetHeight(data, x,   z+1);
                sum += GetHeight(data, x+1, z+1);
                
                tempMap[z * resX + x] = sum / 9.0f;
            }
        }
        data.heightMap = tempMap;
    }
}

void TerrainGenerator::CalculateNormals(TerrainData& data) {
    int resX = data.config.resolutionX;
    int resZ = data.config.resolutionZ;
    float cellW = data.config.worldWidth / (resX - 1);
    float cellD = data.config.worldDepth / (resZ - 1);
    
    data.normals.resize(data.heightMap.size());
    
    for (int z = 0; z < resZ; ++z) {
        for (int x = 0; x < resX; ++x) {
            // 隣接点を使って勾配を計算
            // L R
            // T B (Top/Bottom is Z axis)
            
            float hL = (x > 0) ? GetHeight(data, x - 1, z) : GetHeight(data, x, z);
            float hR = (x < resX - 1) ? GetHeight(data, x + 1, z) : GetHeight(data, x, z);
            float hD = (z > 0) ? GetHeight(data, x, z - 1) : GetHeight(data, x, z); // Down (-Z)
            float hU = (z < resZ - 1) ? GetHeight(data, x, z + 1) : GetHeight(data, x, z); // Up (+Z)
            
            // 接線ベクトル
            XMVECTOR tangentX = XMVectorSet(2.0f * cellW, hR - hL, 0.0f, 0.0f);
            XMVECTOR tangentZ = XMVectorSet(0.0f, hU - hD, 2.0f * cellD, 0.0f);
            
            // 法線 = Cross(Z, X)  (左手座標系 Y-up)
            // DirectXMathは左手系が基本だが、Crossの順序に注意
            // T_z cross T_x -> Normal
            XMVECTOR normal = XMVector3Cross(tangentZ, tangentX);
            normal = XMVector3Normalize(normal);
            
            XMStoreFloat3(&data.normals[z * resX + x], normal);
        }
    }
}

void TerrainGenerator::GenerateMesh(TerrainData& data) {
    int resX = data.config.resolutionX;
    int resZ = data.config.resolutionZ;
    float width = data.config.worldWidth;
    float depth = data.config.worldDepth;
    
    std::vector<graphics::Vertex> vertices;
    std::vector<uint32_t> indices;
    
    vertices.reserve(resX * resZ);
    indices.reserve((resX - 1) * (resZ - 1) * 6);
    
    // 頂点生成
    for (int z = 0; z < resZ; ++z) {
        for (int x = 0; x < resX; ++x) {
            float u = (float)x / (resX - 1);
            float v = (float)z / (resZ - 1); // 1.0 - ... にするかはUV座標系による
            
            float px = (u - 0.5f) * width;
            float pz = (0.5f - v) * depth; // Z軸反転注意。ここでは手前が-ZとするならこれでOK
            float py = GetHeight(data, x, z);
            
            graphics::Vertex vert;
            vert.position = { px, py, pz };
            vert.normal = data.normals[z * resX + x];
            vert.texCoord = { u, v };
            vert.color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 白
            
            vertices.push_back(vert);
        }
    }
    
    // インデックス生成 (Triangle List)
    for (int z = 0; z < resZ - 1; ++z) {
        for (int x = 0; x < resX - 1; ++x) {
            // 0 --- 1
            // |  /  |
            // 2 --- 3
            //
            // Tri 1: 0-1-2
            // Tri 2: 2-1-3
            
            uint32_t i0 = z * resX + x;
            uint32_t i1 = z * resX + (x + 1);
            uint32_t i2 = (z + 1) * resX + x;
            uint32_t i3 = (z + 1) * resX + (x + 1);
            
            // 時計回りか反時計回りかはカリング設定による
            // 通常DirectXは時計回りが表面だが、CullNoneならどちらでも見える
            // ここでは標準的な時計回りで定義
            
            // Tri 1
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
            
            // Tri 2
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
        }
    }
    
    // データ格納
    data.vertices = std::move(vertices);
    data.indices = std::move(indices);
}

float TerrainGenerator::GetHeight(const TerrainData& data, int x, int z) {
    if (x < 0 || x >= data.config.resolutionX || z < 0 || z >= data.config.resolutionZ) return 0.0f;
    return data.heightMap[z * data.config.resolutionX + x];
}

void TerrainGenerator::SetHeight(TerrainData& data, int x, int z, float h) {
    if (x < 0 || x >= data.config.resolutionX || z < 0 || z >= data.config.resolutionZ) return;
    data.heightMap[z * data.config.resolutionX + x] = h;
}

} // namespace game::systems

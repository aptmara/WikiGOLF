/**
 * @file WikiPageLoaderAssets.cpp
 * @brief サムネイルと看板のECS表示を実装します。
*/

#include "../../graphics/GraphicsDevice.h"
#include "WikiPageLoader.h"
#include "HoleVisualRules.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../controllers/MinimapController.h"
#include "../systems/WikiClient.h"
#include <algorithm>
#include <cmath>
#include <future>

#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

/**
 * @brief デコード済みBGRAピクセル列からD3D11 SRVを作成する
*/
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> WikiPageLoader::CreateSRVFromPixels(
    core::GameContext& ctx, const std::vector<uint8_t>& pixelsBGRA,
    uint32_t width, uint32_t height)
{
    if (pixelsBGRA.empty() || width == 0 || height == 0 ||
        pixelsBGRA.size() < static_cast<size_t>(width) * height * 4) {
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixelsBGRA.data();
    initData.SysMemPitch = width * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = ctx.graphics.GetDevice()->CreateTexture2D(&texDesc, &initData, &tex);
    if (FAILED(hr)) {
        LOG_ERROR("WikiPageLoader", "Failed to create thumbnail texture (HRESULT: {:08X})",
                  static_cast<uint32_t>(hr));
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    hr = ctx.graphics.GetDevice()->CreateShaderResourceView(tex.Get(), &srvDesc, &srv);
    if (FAILED(hr)) {
        LOG_ERROR("WikiPageLoader", "Failed to create thumbnail SRV (HRESULT: {:08X})",
                  static_cast<uint32_t>(hr));
        return nullptr;
    }
    return srv;
}

/**
 * @brief 目的記事の代表サムネイルをGPUテクスチャ化して保持する
*/
void WikiPageLoader::SetTargetThumbnail(core::GameContext& ctx,
                                        const std::vector<uint8_t>& pixelsBGRA,
                                        uint32_t width, uint32_t height)
{
    m_hasTargetThumbnail = false;
    m_targetThumbnailSRV = CreateSRVFromPixels(ctx, pixelsBGRA, width, height);
    if (!m_targetThumbnailSRV) {
        return;
    }

    m_targetThumbnailAspect = static_cast<float>(width) / static_cast<float>(height);
    m_hasTargetThumbnail = true;
    LOG_INFO("WikiPageLoader", "Target thumbnail texture created: {}x{}", width, height);
}

/**
 * @brief 記事サムネイル看板（ビルボード）を1枚生成する
*/
ecs::Entity WikiPageLoader::CreateHoleSignboardEntity(
    core::GameContext& ctx, float x, float z, float terrainH,
    bool isTargetHole, int hopsToTarget, ID3D11ShaderResourceView* srv,
    float aspect)
{
    constexpr float kSignboardWidth = 3.5f;
    constexpr float kSignboardClearance = 0.4f; // 旗ポール先端からの浮かせ量
    const float signboardHeight = kSignboardWidth / std::max(0.1f, aspect);

    // CreateProceduralFlag（ProceduralFlag.cpp）と同じ計算式で旗ポールの高さを求め、
    // その真上に看板を置く。
    float poleSize = 0.90f;
    if (isTargetHole) {
        poleSize = 1.05f;
    }
    const float poleHeight = 2.65f * poleSize;
    const float poleTopY = terrainH + 0.05f + poleHeight;

    auto signE = m_pageEntityOwner.Create(ctx.world);
    auto& signT = ctx.world.Add<Transform>(signE);
    signT.position = {x, poleTopY + kSignboardClearance + signboardHeight * 0.5f, z};
    signT.scale = {kSignboardWidth, signboardHeight, 1.0f};

    // 額縁の色は旗・光柱と同じホップ数カラー（GetHoleColor）に揃える。
    // エフェクトの強さも目的地に近いほど強くする（0=無演出の静止色 〜 1=最大）。
    const XMFLOAT4 frameColor =
        HoleVisualRules::GetColor(isTargetHole, hopsToTarget);
    float effectIntensity = 0.0f;
    if (isTargetHole) {
        effectIntensity = 1.0f;
    } else if (hopsToTarget == 1) {
        effectIntensity = 0.75f;
    } else if (hopsToTarget == 2) {
        effectIntensity = 0.5f;
    } else if (hopsToTarget >= 3 && hopsToTarget <= 5) {
        effectIntensity = 0.25f;
    }

    auto& signMr = ctx.world.Add<MeshRenderer>(signE);
    signMr.mesh = ctx.resource.LoadMesh("builtin/quad");
    signMr.shader = ctx.resource.LoadShader(
        "Textured", L"Assets/shaders/TexturedVS.hlsl",
        L"Assets/shaders/TexturedPS.hlsl");
    signMr.textureSRV = srv;
    signMr.hasTexture = true;
    signMr.color = frameColor;
    signMr.customFlags.x = 1.0f; // フェード係数の初期値（1=完全表示）
    signMr.customFlags.y = effectIntensity;

    ctx.world.Add<Billboard>(signE);
    return signE;
}

/**
 * @brief ボール付近のホールに対し、記事サムネイル看板を遅延ロードする
*/
void WikiPageLoader::UpdateNearbyHoleSignboards(core::GameContext& ctx,
                                                const DirectX::XMFLOAT3& ballPos,
                                                ecs::Entity cameraEntity)
{
    // 距離判定・フェッチ開始・キャッシュ走査は毎フレーム行うと全ホールを60回/秒
    // スキャンすることになり無駄が大きいため、一定間隔に間引く。
    // ビルボード回転（カメラ追従）だけは見た目の滑らかさのため毎フレーム行う。
    m_signboardScanTimer += ctx.dt;
    if (m_signboardScanTimer >= kSignboardScanInterval) {
        m_signboardScanTimer = 0.0f;

        // 1. 範囲内にあり、まだ取得を試みていないホールのフェッチを開始する
        //    （Wikimedia APIの同時接続数目安に合わせ、同時実行数を制限）
        ctx.world.Query<GolfHole, Transform>().Each(
            [&](ecs::Entity /*e*/, GolfHole& hole, Transform& t) {
                if (hole.signboardEntity != 0 || hole.linkTarget.empty()) return;
                if (m_holeThumbnailCache.find(hole.linkTarget) != m_holeThumbnailCache.end()) return;
                if (m_activeThumbnailFetches >= kMaxConcurrentThumbnailFetches) return;

                const float dx = t.position.x - ballPos.x;
                const float dz = t.position.z - ballPos.z;
                if (dx * dx + dz * dz > kThumbnailLoadRadius * kThumbnailLoadRadius) return;

                HoleThumbnailState newState;
                newState.status = HoleThumbnailState::Status::Fetching;
                const std::string title = hole.linkTarget;
                newState.future = std::async(std::launch::async, [title]() {
                    PendingHoleThumbnail result;
                    game::systems::WikiClient client;
                    std::string url = client.FetchPageThumbnail(title, 200);
                    if (url.empty()) return result;
                    std::string bytes = client.DownloadBinary(url);
                    if (bytes.empty()) return result;
                    result.found = graphics::DecodeWikiImageFromMemory(
                        bytes, result.pixelsBGRA, result.width, result.height);
                    return result;
                });
                m_holeThumbnailCache.emplace(hole.linkTarget, std::move(newState));
                ++m_activeThumbnailFetches;
            });

        // 2. 完了したフェッチをGPUテクスチャ化する（メインスレッド）
        for (auto& [title, thumbState] : m_holeThumbnailCache) {
            if (thumbState.status != HoleThumbnailState::Status::Fetching) continue;
            if (!thumbState.future.valid() ||
                thumbState.future.wait_for(std::chrono::milliseconds(0)) !=
                    std::future_status::ready) {
                continue;
            }

            PendingHoleThumbnail result = thumbState.future.get();
            --m_activeThumbnailFetches;

            if (!result.found) {
                thumbState.status = HoleThumbnailState::Status::Failed;
                LOG_DEBUG("WikiPageLoader", "No thumbnail available for '{}'", title);
                continue;
            }

            thumbState.srv = CreateSRVFromPixels(ctx, result.pixelsBGRA, result.width, result.height);
            if (!thumbState.srv) {
                thumbState.status = HoleThumbnailState::Status::Failed;
                continue;
            }
            thumbState.aspect = static_cast<float>(result.width) / static_cast<float>(result.height);
            thumbState.status = HoleThumbnailState::Status::Ready;
            LOG_DEBUG("WikiPageLoader", "Nearby hole thumbnail ready: '{}' ({}x{})",
                     title, result.width, result.height);
        }

        // 3. 取得済みサムネイルを、対応する未表示ホールへ反映する
        ctx.world.Query<GolfHole, Transform>().Each(
            [&](ecs::Entity /*e*/, GolfHole& hole, Transform& t) {
                if (hole.signboardEntity != 0 || hole.linkTarget.empty()) return;

                auto it = m_holeThumbnailCache.find(hole.linkTarget);
                if (it == m_holeThumbnailCache.end() ||
                    it->second.status != HoleThumbnailState::Status::Ready) {
                    return;
                }

                float terrainH = 0.0f;
                if (m_terrainSystem) {
                    terrainH =
                        m_terrainSystem->GetHeight(t.position.x, t.position.z);
                }
                hole.signboardEntity = static_cast<uint32_t>(CreateHoleSignboardEntity(
                    ctx, t.position.x, t.position.z, terrainH, hole.isTarget,
                    hole.hopsToTarget, it->second.srv.Get(), it->second.aspect));
            });
    }

    // 4. 既存の全看板をカメラの方向へビルボード回転させ、近づきすぎたら
    //    ドットフェードで消す（毎フレーム）。
    //    通常時はヨーのみ（見た目が傾かず読みやすい）、マップ俯瞰時は
    //    真上から見下ろしてもヨーだけでは正面が見えなくなるため
    //    3軸フルビルボード（Look-at）に切り替える。
    if (auto* camT = ctx.world.Get<Transform>(cameraEntity)) {
        constexpr float kFadeEndDistance = 3.0f;   // これ以下の距離で完全に消える
        constexpr float kFadeStartDistance = 7.0f; // これより遠ければ完全表示

        const auto* state = ctx.world.GetGlobal<GolfGameState>();
        const bool useLookAt = state && state->isMapView;

        const XMVECTOR camPos = XMLoadFloat3(&camT->position);

        ctx.world.Query<Transform, Billboard>().Each(
            [&](ecs::Entity e, Transform& t, Billboard&) {
                const XMVECTOR objPos = XMLoadFloat3(&t.position);
                const XMVECTOR toObj = XMVectorSubtract(objPos, camPos);
                const float distSq = XMVectorGetX(XMVector3LengthSq(toObj));

                if (distSq > 1e-8f) {
                    if (useLookAt) {
                        // 3軸フルビルボード（Look-at回転）。
                        // クアッドの正面(ローカル-Z)が常にカメラを向くよう、
                        // ローカル+Zをカメラと反対方向へ合わせる基底を組む。
                        const XMVECTOR zAxis = XMVector3Normalize(toObj);
                        XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                        // カメラがほぼ真上/真下にある場合はupが縮退するため別軸で代用
                        if (std::fabs(XMVectorGetX(XMVector3Dot(zAxis, worldUp))) > 0.999f) {
                            worldUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                        }
                        const XMVECTOR xAxis = XMVector3Normalize(XMVector3Cross(worldUp, zAxis));
                        const XMVECTOR yAxis = XMVector3Cross(zAxis, xAxis);

                        const XMMATRIX rotMat(xAxis, yAxis, zAxis, XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));
                        const XMVECTOR rot = XMQuaternionRotationMatrix(rotMat);
                        XMStoreFloat4(&t.rotation, rot);
                    } else {
                        // ヨーのみ（水平回転）。地面に立つ看板として傾かず読みやすい。
                        const float dx = XMVectorGetX(toObj);
                        const float dz = XMVectorGetZ(toObj);
                        if (std::fabs(dx) > 1e-4f || std::fabs(dz) > 1e-4f) {
                            const float yaw = std::atan2(dx, dz);
                            const XMVECTOR rot = XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);
                            XMStoreFloat4(&t.rotation, rot);
                        }
                    }
                }

                if (auto* mr = ctx.world.Get<MeshRenderer>(e)) {
                    if (useLookAt) {
                        // マップ俯瞰時は看板が真下の記事本文テクスチャを覆い隠して
                        // しまうため、常に非表示にする。
                        mr->customFlags.x = 0.0f;
                    } else {
                        const float dist = std::sqrt(distSq);
                        const float fade = std::clamp(
                            (dist - kFadeEndDistance) / (kFadeStartDistance - kFadeEndDistance),
                            0.0f, 1.0f);
                        mr->customFlags.x = fade;
                    }
                }
            });
    }
}


} // namespace game::scenes


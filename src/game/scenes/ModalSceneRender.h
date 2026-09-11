#pragma once
/**
 * @file ModalSceneRender.h
 * @brief PushScene方式のモーダルシーン（RankingScene/AchievementScene等）が
 *        共通で使う描画処理
*/

#include <DirectXMath.h>

namespace core {
struct GameContext;
class Scene;
}

namespace game::scenes {

/**
 * @brief モーダルシーン所有のUIText/UIButtonを、背景暗転＋レイヤー順で
 *        直接描画します。
 * @details UIRenderSystem等の通常描画パスは、シーンを問わずUIText/UIImage/
 *          UIButtonを型ごとに別パスで描画するため、片方のシーンのUIImage
 *          （タイトル画像など）がもう片方のシーンのUIText（モーダル本体）
 *          より後に描かれて手前に来てしまうことがある。
 *          この関数はSceneManager::Render経由（全パス最後）で呼び出す想定で、
 *          呼び出したシーンが所有するUIText/UIButtonだけをここで直接描画し、
 *          常に他の全要素より手前に表示させる。
 * @param scene 描画対象のUIText/UIButtonを所有しているシーン（OwnsEntityで判定）
 * @param dimColor 背景暗転オーバーレイの色（レターボックス含め画面全体を塗る）
*/
void RenderModalScene(core::GameContext &ctx, const core::Scene &scene,
                      const DirectX::XMFLOAT4 &dimColor);

} // namespace game::scenes

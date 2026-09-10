/**
 * @file TutorialOverlayControllerUI.cpp
 * @brief Wikipedia風チュートリアルUIを更新します。
 */

#include "TutorialOverlayController.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../utils/UIConstants.h"
#include "../../ecs/World.h"
#include <algorithm>

namespace game::controllers {

void TutorialOverlayController::UpdateUI(core::GameContext& ctx) {
    const bool visible = m_visible && m_step != TutorialStep::Done;
    if (auto* bg = ctx.world.Get<components::UIText>(m_overlayBgEntity))
        bg->visible = visible;
    if (auto* txt = ctx.world.Get<components::UIText>(m_overlayTextEntity))
        txt->visible = visible;
    if (auto* action = ctx.world.Get<components::UIText>(m_actionTextEntity))
        action->visible = visible;
    if (auto* skip = ctx.world.Get<components::UIText>(m_skipTextEntity))
        skip->visible = visible;
    if (!visible) return;

    std::wstring text;
    std::wstring action;
    std::wstring hint;
    switch (m_step) {
    case TutorialStep::Intro:
        text = L"WIKIGOLF ガイド  1 / 9\n記事から記事へ、カップインでリンクを渡り歩くゴルフです。";
        action = L"[ ENTER ] はじめる";
        break;
    case TutorialStep::Camera:
        text = L"カメラ  2 / 9\n左または右ドラッグで回転。ホイールでズーム。Shift中は精密操作。";
        action = L"[ ドラッグ ] 回転  ＋  [ ホイール ] ズーム";
        hint = L"[ ENTER ] スキップ";
        break;
    case TutorialStep::Aim:
        text = L"狙う  3 / 9\n中クリックで狙いを置くと、向きと距離に合うクラブが自動で選ばれます。";
        action = L"[ 中クリック ] コース上に照準ピンを置く";
        hint = L"[ ENTER ] スキップ";
        break;
    case TutorialStep::Club:
        text = L"クラブ  4 / 9\nQ / Eでクラブを変更。飛距離と弾道が変わり、パターはグリーン向きです。";
        action = L"[ Q / E ] クラブを切り替える";
        hint = L"[ ENTER ] スキップ";
        break;
    case TutorialStep::Power:
        text = L"ショット  5 / 9 — パワー\n左クリックでゲージ開始、もう一度左クリックで飛距離を決定。右クリックで取消。";
        action = L"[ 左クリック ] 開始  →  [ 左クリック ] パワー決定";
        hint = L"[ ENTER ] ショット説明をスキップ";
        break;
    case TutorialStep::Impact:
        text = L"ショット  5 / 9 — インパクト\n戻るマーカーを左クリック。決めたパワー位置がPerfect、周囲ほど精度が落ちます。";
        action = L"[ 左クリック ] インパクトを決めて打つ";
        hint = L"[ 右クリック ] やり直す  ｜  [ ENTER ] スキップ";
        break;
    case TutorialStep::TerrainInfo: {
        const auto& targets = GetActiveEventCameraTargets();
        if (m_terrainCardIndex < targets.size()) {
            const auto& target = targets[m_terrainCardIndex];
            text = L"地形  6 / 9  —  " + target.name + L"\n" + target.desc;
        } else {
            text = L"地形  6 / 9\n地形ごとに転がりやすさが変わります。";
        }
        action = L"[ ENTER ] 次の地形を見る";
        break;
    }
    case TutorialStep::FlagInfo: {
        const auto& targets = GetActiveEventCameraTargets();
        if (m_terrainCardIndex < targets.size()) {
            const auto& target = targets[m_terrainCardIndex];
            text = L"リンクの旗  7 / 9  —  " + target.name + L"\n" + target.desc;
        } else {
            text = L"リンクの旗  7 / 9\n旗色はゴール記事までのリンク距離を示します。";
        }
        action = L"[ ENTER ] 次の旗を見る";
        break;
    }
    case TutorialStep::MapOpen:
        text = L"マップ  8 / 9 — 開く\nコース全体と旗の位置を確認できます。";
        action = L"[ M ] マップを開く";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapPan:
        text = L"マップ  8 / 9 — 移動\n見たい場所へ地図を動かします。";
        action = L"[ 左ドラッグ ] マップをパンする";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapZoom:
        text = L"マップ  8 / 9 — 拡大縮小\n旗が密集した場所も細かく確認できます。";
        action = L"[ ホイール ] マップをズームする";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapAim:
        text = L"マップ  8 / 9 — 狙いを置く\n中クリックした地点へ照準ピンを置き、距離に合うクラブを自動選択します。";
        action = L"[ 中クリック ] マップ上に照準ピンを置く";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapHelpOpen:
        text = L"マップ  8 / 9 — ヘルプ\nマップ専用の操作一覧をその場で確認できます。";
        action = L"[ ? ] ヘルプを開く";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapHelpClose:
        text = L"マップ  8 / 9 — ヘルプを閉じる\n同じキーで表示を切り替えます。";
        action = L"[ ? ] ヘルプを閉じる";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapClose:
        text = L"マップ  8 / 9 — 戻る\n俯瞰を終えてショット画面へ戻ります。";
        action = L"[ Esc ] マップを閉じる";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::LinkCup:
        text = L"リンクを渡る  9 / 9\nスタート地点のすぐ前（約4m）、「フェアウェイ」の照準ピンへ入れましょう。";
        action = L"[ 照準ピン ] フェアウェイへカップイン";
        hint = L"[ ENTER ] 記事移動をスキップ";
        break;
    case TutorialStep::GoalCup:
        if (const auto* state = ctx.world.GetGlobal<components::GolfGameState>();
            state && state->gameCleared) {
            text = L"TUTORIAL COMPLETE\nリンクを読み、狙い、打ってゴールへ到達しました。";
            action = L"ゴール到達！";
            hint = L"タイトルへ戻ります…";
        } else {
            text = L"最後のチャレンジ\n赤い「ゴール」の旗へ。好きなクラブと狙い方でカップインしてください。";
            action = L"[ 赤い旗 ] ゴールへカップイン";
            hint = L"ゴール以外はティーへ復帰  ｜  [ ENTER ] 終了";
        }
        break;
    case TutorialStep::Done:
        break;
    }

    if (auto* txt = ctx.world.Get<components::UIText>(m_overlayTextEntity))
        txt->text = text;
    if (auto* actionText = ctx.world.Get<components::UIText>(m_actionTextEntity))
        actionText->text = action;
    if (auto* skip = ctx.world.Get<components::UIText>(m_skipTextEntity))
        skip->text = hint;
}

const std::vector<TutorialOverlayController::EventCameraTarget>&
TutorialOverlayController::GetActiveEventCameraTargets() const {
    static const std::vector<EventCameraTarget> empty;
    if (m_step == TutorialStep::TerrainInfo) return m_eventCamTargets;
    if (m_step == TutorialStep::FlagInfo) return m_flagEventCamTargets;
    return empty;
}

void TutorialOverlayController::TriggerStepClear(core::GameContext& ctx) {
    if (m_stepClearPending) return;
    if (ctx.world.IsAlive(m_checkMarkEntity)) ctx.world.DestroyEntity(m_checkMarkEntity);
    m_checkMarkEntity = m_entityOwner.Create(ctx.world);
    m_checkMarkTimer = 0.0f;
    m_stepClearPending = true;
    auto& img = ctx.world.Add<components::UIImage>(m_checkMarkEntity);
    img.texturePath = "Assets/textures/mark_check.png";
    img.x = 640.0f;
    img.y = 74.0f;
    img.width = 0.0f;
    img.height = 0.0f;
    img.alpha = 1.0f;
    img.visible = true;
    img.layer = game::ui::kLayerOverlay + 10;
}

void TutorialOverlayController::UpdateStepClearAnim(core::GameContext& ctx) {
    auto* img = ctx.world.Get<components::UIImage>(m_checkMarkEntity);
    if (!img) return;
    constexpr float maxSize = 84.0f;
    constexpr float finalSize = 62.0f;
    float size = finalSize;
    if (m_checkMarkTimer < 0.22f) {
        const float t = m_checkMarkTimer / 0.22f;
        size = maxSize * (1.0f - (1.0f - t) * (1.0f - t));
    } else if (m_checkMarkTimer < 0.38f) {
        const float t = (m_checkMarkTimer - 0.22f) / 0.16f;
        size = maxSize - (maxSize - finalSize) * t;
    }
    img->width = size;
    img->height = size;
    img->x = 640.0f - size * 0.5f;
    img->y = 92.0f - size * 0.5f;
}

} // namespace game::controllers

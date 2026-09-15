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
    const bool visible = m_visible;
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
        text = L"WIKIGOLF ガイド  1 / 9\n記事内のリンクがカップ（旗）になります。カップインして次の記事へ進みましょう。";
        action = L"[ ENTER ] はじめる";
        break;
    case TutorialStep::Camera:
        text = L"カメラ操作  2 / 9\nマウスドラッグで視点を回転、ホイールでズームできます。（Shiftで微調整）";
        action = L"[ マウスドラッグ ] 回転  ＋  [ ホイール ] ズーム";
        hint = L"[ ENTER ] スキップ";
        break;
    case TutorialStep::Aim:
        text = L"狙いを定める  3 / 9\nミニマップやコース上を中クリックすると、照準ピンと推奨クラブ・強さの目安が出ます。";
        action = L"[ 中クリック ] ミニマップに照準ピンを置く";
        hint = L"[ ENTER ] スキップ";
        break;
    case TutorialStep::Club:
        text = L"クラブ選択  4 / 9\nQ / Eキーでクラブを変更できます。状況に合わせて弾道や番手を選び直せます。";
        action = L"[ Q / E ] クラブを切り替える";
        hint = L"[ ENTER ] スキップ";
        break;
    case TutorialStep::Power:
        text = L"ショット（強さ決定）  5 / 9\n左クリックでスイングを開始し、もう一度左クリックで打つ強さを決定します。";
        action = L"[ 左クリック ] 開始  →  [ 左クリック ] 強さを決定";
        hint = L"[ 右クリック ] 取消  ｜  [ ENTER ] スキップ";
        break;
    case TutorialStep::Impact:
        text = L"ショット（インパクト）  5 / 9\n折り返して戻るバーを左クリック！ 決めた強さの位置に近いほど正確に飛びます。";
        action = L"[ 左クリック ] タイミングを合わせてショット";
        hint = L"[ 右クリック ] やり直す  ｜  [ ENTER ] スキップ";
        break;
    case TutorialStep::TerrainInfo: {
        const auto& targets = GetActiveEventCameraTargets();
        if (m_terrainCardIndex < targets.size()) {
            const auto& target = targets[m_terrainCardIndex];
            text = L"コースの地形  6 / 9  —  " + target.name + L"\n" + target.desc;
        } else {
            text = L"コースの地形  6 / 9\n地形によってボールの転がりやすさやショットのしやすさが変化します。";
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
            text = L"リンクの旗  7 / 9\n旗の色は、最終目標（ゴール記事）までのリンク距離を表しています。";
        }
        action = L"[ ENTER ] 次の旗を見る";
        break;
    }
    case TutorialStep::MapOpen:
        text = L"全体マップ  8 / 9\nMキーで全体マップを開きます。コース全体の地形や旗の位置を俯瞰してみましょう。";
        action = L"[ M ] マップを開く";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapPan:
        text = L"マップ移動  8 / 9\nマウスをドラッグ（左 / 右）して、見たいエリアへマップを移動できます。";
        action = L"[ 左 / 右ドラッグ ] マップをスクロール";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapZoom:
        text = L"マップ拡大縮小  8 / 9\nホイールを回転させてズームできます。旗が密集しているエリアの確認に役立ちます。";
        action = L"[ ホイール ] ズームイン / ズームアウト";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapAim:
        text = L"マップ照準  8 / 9\n全体マップ上でも中クリックで照準ピンを置けます。（設置後にショット画面へ戻ります）";
        action = L"[ 中クリック ] マップ上に照準ピンを置く";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapHelpOpen:
        text = L"操作一覧  8 / 9\nSpaceキーでボール位置へ復帰、Fキーでコース全景、?キーで操作一覧を確認できます。";
        action = L"[ ? ] 操作ヘルプを開く";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapHelpClose:
        text = L"操作一覧  8 / 9\nもう一度 ? キーを押すと、操作一覧ヘルプを閉じることができます。";
        action = L"[ ? ] 操作ヘルプを閉じる";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::MapClose:
        text = L"ショットへ戻る  8 / 9\n全体マップの確認を終えたら、マップを閉じてショット画面へ戻りましょう。";
        action = L"[ M / Esc ] マップを閉じる";
        hint = L"[ ENTER ] マップ説明をスキップ";
        break;
    case TutorialStep::LinkCup:
        text = L"リンクを渡る  9 / 9\n照準ピンを目安に、すぐ目の前（約4m先）にある「フェアウェイ」のカップへ打ちましょう。";
        action = L"「フェアウェイ」のカップに入れる";
        hint = L"[ ENTER ] スキップして次へ";
        break;
    case TutorialStep::GoalCup:
        if (const auto* state = ctx.world.GetGlobal<components::GolfGameState>();
            state && state->gameCleared) {
            text = L"チュートリアル完了！\nお見事です！基本操作をマスターしてゴールへ到達しました。";
            action = L"ゴール達成！";
            hint = L"タイトル画面へ戻ります…";
        } else {
            text = L"最後のチャレンジ\n正面奥に見える赤い旗「ゴール」を目指しましょう！ 自由にカップインしてください。";
            action = L"赤いカップ（ゴール）に入れる";
            hint = L"ゴール以外に入ると打ち直し  ｜  [ ENTER ] 終了";
        }
        break;
    case TutorialStep::Done:
        text = L"チュートリアル完了！\nお見事です！基本操作をマスターしてゴールへ到達しました。";
        action = L"Wikipediaへの接続を確認中...";
        hint = L"接続確認後、ゲームを開始します…";
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

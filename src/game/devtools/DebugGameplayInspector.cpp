#include "DebugGameplayInspector.h"

#include "DebugGameplaySnapshot.h"
#include "DebugCupInStatus.h"
#include "DebugSlopeStatus.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "imgui.h"

namespace game::debug {
namespace {

const char *BoolText(bool value) { return value ? "true" : "false"; }

void DrawVector3(const char *label, const DirectX::XMFLOAT3 &value) {
  ImGui::Text("%s: %.3f, %.3f, %.3f", label, value.x, value.y, value.z);
}

} // namespace

void DrawGameplayInspector(core::GameContext &ctx) {
  const DebugGameplaySnapshot data = CaptureGameplaySnapshot(ctx.world);
  const DebugSlopeStatus slope = CaptureSlopeStatus(ctx.world);
  const DebugCupInStatus cupIn = CaptureCupInStatus(ctx.world);
  ImGui::SeparatorText("ゴルフゲーム状態");
  if (!data.golf.available) {
    ImGui::TextDisabled("GolfGameStateはありません。");
  } else {
    ImGui::Text("現在の記事: %s", data.golf.currentPage.c_str());
    ImGui::Text("目的の記事: %s", data.golf.targetPage.c_str());
    ImGui::Text("打数: %d / PAR: %d / 移動: %d", data.golf.shotCount,
                data.golf.par, data.golf.moveCount);
    ImGui::Text("地形: %s / 速度: %.3f", data.golf.material.c_str(),
                data.golf.ballSpeed);
    ImGui::Text("接地: %s / OB: %s / ショット可能: %s",
                BoolText(data.golf.grounded), BoolText(data.golf.outOfBounds),
                BoolText(data.golf.canShoot));
    ImGui::Text("マップ表示: %s / クリア: %s", BoolText(data.golf.mapView),
                BoolText(data.golf.gameCleared));
    ImGui::Text("風: %.3f, %.3f / %.3f m/s", data.golf.windDirection.x,
                data.golf.windDirection.y, data.golf.windSpeed);
    DrawVector3("最後のショット位置", data.golf.lastShotPosition);
    if (slope.available) {
      ImGui::Text("坂道上: %s / 傾斜角: %.2f度 / 法線Y: %.4f",
                  BoolText(slope.evaluation.isOnSlope),
                  slope.evaluation.angleDegrees, slope.evaluation.normalY);
      ImGui::TextDisabled("坂道閾値: 法線Y < %.2f（約11.5度）",
                          kSlopeFlatNormalYThreshold);
    } else {
      ImGui::TextDisabled("坂道判定: 地形サンプルなし");
    }
  }

  ImGui::SeparatorText("ショット状態");
  if (!data.shot.available) {
    ImGui::TextDisabled("ShotStateはありません。");
  } else {
    ImGui::Text("フェーズ: %s / 判定: %s", data.shot.phase.c_str(),
                data.shot.judgement.c_str());
    ImGui::Text("パワー: %.3f / 方向: %.1f / 確定: %.3f",
                data.shot.powerGauge, data.shot.powerDirection,
                data.shot.confirmedPower);
    ImGui::Text("インパクト: %.3f / 中心: %.3f / 確定: %.3f",
                data.shot.impactGauge, data.shot.impactPerfectCenter,
                data.shot.confirmedImpact);
    ImGui::Text("最大パワー: %.3f / 結果表示: %.3f秒", data.shot.maxPower,
                data.shot.resultDisplayTime);
  }

  ImGui::SeparatorText("ボール物理");
  if (!data.ball.available) {
    ImGui::TextDisabled("有効なボールTransform/RigidBodyはありません。");
  } else {
    ImGui::Text("Entity: %u / 速度の大きさ: %.3f", data.ball.entity,
                data.ball.speed);
    DrawVector3("位置", data.ball.position);
    DrawVector3("速度", data.ball.velocity);
    DrawVector3("加速度", data.ball.acceleration);
    DrawVector3("角速度", data.ball.angularVelocity);
    ImGui::Text("質量: %.3f / Drag: %.3f", data.ball.mass, data.ball.drag);
    ImGui::Text("転がり摩擦: %.3f / 反発: %.3f / Spin減衰: %.3f",
                data.ball.rollingFriction, data.ball.restitution,
                data.ball.spinDecay);
  }

  ImGui::SeparatorText("ホールイン判定");
  if (!cupIn.available) {
    ImGui::TextDisabled("判定可能なボールまたはホールがありません。");
  } else {
    ImGui::Text("カップイン: %s / ホールインワン: %s / ターゲット: %s",
                BoolText(cupIn.readyForCupIn), BoolText(cupIn.holeInOne),
                BoolText(cupIn.targetHole));
    ImGui::Text("最寄りホール: #%u %s", cupIn.holeEntity,
                cupIn.linkTarget.c_str());
    ImGui::Text("水平範囲: %s  距離 %.3f / 判定半径 %.3f",
                BoolText(cupIn.withinHorizontalRange),
                cupIn.horizontalDistance, cupIn.captureRadius);
    ImGui::Text("高さ範囲: %s  相対Y %.3f (-1.0 < Y < 0.0)",
                BoolText(cupIn.withinVerticalRange), cupIn.verticalOffset);
    ImGui::Text("低速条件: %s  速度 %.3f (< 0.1)",
                BoolText(cupIn.slowEnough), cupIn.speed);
  }
}

} // namespace game::debug

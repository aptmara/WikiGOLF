/**
 * @file WikiGolfSceneResultEffects.cpp
 * @brief 着地とショット判定の結果表示を実装します。
 */

#include "WikiGolfScene.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <cmath>

namespace game::scenes {

using namespace game::components;

void WikiGolfScene::UpdateResultVisuals(core::GameContext &ctx, float dt) {
  // 着地地形の結果画像(Fairway/Rough/Bunker/Green/OB)の登場・中間・退場
  // アニメーション。スイング判定の結果画像とは完全に別のエンティティ・
  // 別の演出カーブ(このすぐ下のブロック)で、意図的に共通化しない。
  if (m_terrainDisplayTimer > 0.0f) {
      m_terrainDisplayTimer -= dt;
      if (m_terrainImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_terrainImageEntity);
          if (ui) {
              if (m_terrainDisplayTimer <= 0.0f) {
                  ui->visible = false;
              } else {
                  const float total   = std::max(m_terrainDisplayTotal, 0.05f);
                  const float elapsed = total - m_terrainDisplayTimer;
                  const float targetW = m_terrainDisplayTargetW;
                  const float targetH = m_terrainDisplayTargetH;

                  float scale = 1.0f;
                  float alpha = 1.0f;
                  float rotDeg = 0.0f;
                  float offsetX = 0.0f;
                  float offsetY = 0.0f;

                  switch (m_terrainDisplayTier) {
                  case TerrainResultTier::Perfect: {
                      // グリーンにきれいに乗った、上品でゆったりした演出。
                      // 判定側の Perfect(激しく弾む)とは違い、着地の柔らかさを
                      // 表すため沈み込んでから静かに定位置へ戻る。
                      constexpr float kEnter = 0.4f;
                      constexpr float kExit  = 0.4f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          float settle = 1.0f - (1.0f - t) * (1.0f - t); // イーズアウト
                          scale = 1.08f - 0.08f * settle; // わずかに大きめから収束
                          alpha = std::min(1.0f, t * 1.6f);
                      } else if (m_terrainDisplayTimer < kExit) {
                          float t = m_terrainDisplayTimer / kExit;
                          alpha = t;
                          offsetY = -(1.0f - t) * 10.0f;
                      } else {
                          offsetY = std::sin(elapsed * 1.2f) * 3.0f; // 静かな浮遊
                      }
                      break;
                  }
                  case TerrainResultTier::Good: {
                      // フェアウェイ/氷: 過度な演出を付けない実直なポップ。
                      constexpr float kEnter = 0.2f;
                      constexpr float kExit  = 0.3f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = t;
                          alpha = t;
                      } else if (m_terrainDisplayTimer < kExit) {
                          alpha = m_terrainDisplayTimer / kExit;
                      }
                      break;
                  }
                  case TerrainResultTier::Rough:
                  default: {
                      // ラフ/バンカー/OB: ボテッと落ちて土煙が舞うような、
                      // 弾みの悪い着地感。表示中もじわっと沈む。
                      constexpr float kEnter = 0.18f;
                      constexpr float kExit  = 0.25f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = 1.25f - 0.25f * t; // 大きめから縮んで収まる(潰れる感じ)
                          alpha = t;
                          offsetY = (1.0f - t) * 10.0f; // 落下してめり込む
                      } else if (m_terrainDisplayTimer < kExit) {
                          alpha = m_terrainDisplayTimer / kExit;
                      }
                      offsetX += std::sin(elapsed * 9.0f) * 2.0f; // 土煙のようなゆらぎ(低頻度)
                      break;
                  }
                  }

                  ui->alpha = std::clamp(alpha, 0.0f, 1.0f);
                  ui->width  = targetW * scale;
                  ui->height = targetH * scale;
                  ui->rotation = rotDeg; // D2D1::Matrix3x2F::Rotation は度数指定
                  ui->x = (1280.0f - ui->width) * 0.5f + offsetX;
                  ui->y = (720.0f - ui->height) * 0.5f + offsetY;
              }
          }
      }
  }

  // スイング判定の結果画像(Perfect/Great/Nice/Miss)の登場・中間・退場
  // アニメーション。着地地形の演出とは別エンティティ・別カーブ。
  if (m_judgeDisplayTimer > 0.0f) {
      m_judgeDisplayTimer -= dt;
      if (m_judgeImageEntity != UINT32_MAX) {
          auto* ui = ctx.world.Get<game::components::UIImage>(m_judgeImageEntity);
          if (ui) {
              if (m_judgeDisplayTimer <= 0.0f) {
                  ui->visible = false;
              } else {
                  const float total   = std::max(m_judgeDisplayTotal, 0.05f);
                  const float elapsed = total - m_judgeDisplayTimer;
                  const float targetW = m_judgeDisplayTargetW;
                  const float targetH = m_judgeDisplayTargetH;

                  float scale = 1.0f;
                  float alpha = 1.0f;
                  float rotDeg = 0.0f;
                  float offsetX = 0.0f;
                  float offsetY = 0.0f;

                  using Judgement = game::components::ShotJudgement;
                  switch (m_judgeDisplayJudgement) {
                  case Judgement::Special: {
                      // インパクトの気持ちよさを弾けるスタンプで表現。
                      // 大きくオーバーシュートして登場し、余韻中も脈動し続け、
                      // 最後は膨らみながら浮き上がって消える。
                      constexpr float kEnter = 0.28f;
                      constexpr float kExit  = 0.4f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          float tm1 = t - 1.0f;
                          constexpr float kOvershoot = 2.0f;
                          scale = std::max(0.0f, 1.0f + (kOvershoot + 1.0f) * tm1 * tm1 * tm1 +
                                                  kOvershoot * tm1 * tm1);
                          alpha = std::min(1.0f, t * 2.4f);
                      } else if (m_judgeDisplayTimer < kExit) {
                          float t = m_judgeDisplayTimer / kExit;
                          scale = 1.0f + (1.0f - t) * 0.15f;
                          alpha = t;
                          offsetY = -(1.0f - t) * 16.0f;
                      } else {
                          scale = 1.0f + std::sin(elapsed * 4.0f) * 0.04f;
                          rotDeg = std::sin(elapsed * 2.0f) * 2.2f;
                      }
                      break;
                  }
                  case Judgement::Great: {
                      // 確かな手応え。小さめのバウンドでキビキビ決まる。
                      constexpr float kEnter = 0.18f;
                      constexpr float kExit  = 0.3f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = t * (1.0f + (1.0f - t) * 0.22f);
                          alpha = t;
                      } else if (m_judgeDisplayTimer < kExit) {
                          alpha = m_judgeDisplayTimer / kExit;
                      }
                      break;
                  }
                  case Judgement::Nice: {
                      // まずまずの手応え。オーバーシュートなしで淡々と現れる。
                      constexpr float kEnter = 0.22f;
                      constexpr float kExit  = 0.25f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = t;
                          alpha = t;
                      } else if (m_judgeDisplayTimer < kExit) {
                          alpha = m_judgeDisplayTimer / kExit;
                      }
                      break;
                  }
                  case Judgement::Miss:
                  default: {
                      // タイミングを外した気まずさをぐらつきで表現。
                      // 斜めに傾いだまま現れ、表示中ずっと細かく震え、
                      // 余韻なくすぐ消える。
                      constexpr float kEnter = 0.12f;
                      constexpr float kExit  = 0.18f;
                      if (elapsed < kEnter) {
                          float t = elapsed / kEnter;
                          scale = 0.6f + 0.4f * t;
                          alpha = t;
                          rotDeg = (1.0f - t) * -10.0f;
                      } else if (m_judgeDisplayTimer < kExit) {
                          alpha = m_judgeDisplayTimer / kExit;
                      }
                      offsetX += (std::sin(elapsed * 34.0f) + std::sin(elapsed * 51.0f)) * 1.8f;
                      rotDeg  += std::sin(elapsed * 26.0f) * 3.0f;
                      break;
                  }
                  }

                  ui->alpha = std::clamp(alpha, 0.0f, 1.0f);
                  ui->width  = targetW * scale;
                  ui->height = targetH * scale;
                  ui->rotation = rotDeg; // D2D1::Matrix3x2F::Rotation は度数指定
                  ui->x = (1280.0f - ui->width) * 0.5f + offsetX;
                  ui->y = game::ui::kJudgeImageCenterY - ui->height * 0.5f + offsetY;
              }
          }
      }
  }
}

} // namespace game::scenes


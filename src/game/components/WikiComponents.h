#pragma once
/**
 * @file WikiComponents.h
 * @brief WikiPinball固有のコンポーネント定義
 */

#include "../systems/TerrainGenerator.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <vector>

namespace game::components {

// ... (existing structs)

/**
 * @brief 地形コライダー
 * ハイトマップデータを保持し、詳細な衝突判定に使用する。
 */
struct TerrainCollider {
  std::shared_ptr<game::systems::TerrainData> data;
};

/**
 * @brief Wikiの見出し（障害物）コンポーネント
 * リンク先記事への遷移に使用
 */
struct Heading {
  int maxHealth = 3;       ///< 最大耐久値
  int currentHealth = 3;   ///< 現在の耐久値
  std::string textSnippet; ///< 表示するテキスト内容
  std::string fullText;    ///< 完全なテキスト
  std::string linkTarget;  ///< リンク先記事タイトル（空の場合はリンクではない）
  uint32_t textEntityId = 0; ///< 対応するテキストUIのEntity ID
  bool isDestroyed = false;  ///< 破壊済みフラグ
};

/**
 * @brief ゲーム状態をシステム間で共有するためのグローバルデータ
 */
struct WikiGameState {
  uint32_t scoreEntity = 0;  ///< スコア表示UIエンティティ
  uint32_t infoEntity = 0;   ///< 情報表示UIエンティティ
  uint32_t headerEntity = 0; ///< ヘッダー表示UIエンティティ
  uint32_t introEntity = 0;  ///< 記事概要表示UIエンティティ
  int score = 0;             ///< 現在のスコア
  int headingsUnlocked = 0;  ///< アンロック済み見出し数
  std::string currentPage;   ///< 現在の記事タイトル
  std::string currentIntro;  ///< 現在の記事概要
  std::string targetPage;    ///< 目的の記事タイトル
  std::string pendingLink;   ///< 遷移待ちリンク（↑キーで遷移）
  int moveCount = 0;         ///< 遷移回数
  int lives = 3;             ///< 残機
  bool gameCleared = false;  ///< クリアフラグ
};

/**
 * @brief WikiGolf初期化用グローバルデータ
 * LoadingSceneで非同期ロードしたデータをWikiGolfSceneへ渡すために使用
 */
struct WikiGlobalData {
  std::unique_ptr<game::systems::WikiShortestPath> pathSystem;
  std::string startPage;
  std::string targetPage;
  int targetPageId = -1;
  int initialPar = -1;

  // 初回ロード済みデータ (LoadingSceneで取得済みの場合に使用)
  bool hasCachedData = false;
  std::vector<game::systems::WikiLink> cachedLinks;
  std::string cachedExtract;
};

/**
 * @brief ゴールホールコンポーネント
 */
struct GoalHole {
  bool isOpen = false;    ///< 開放状態
  std::string targetPage; ///< 遷移先のページ名
};

/**
 * @brief ピンボールのボール識別用タグ
 */
struct PinballBall {
  bool active = true; ///< アクティブ状態
};

/**
 * @brief フリッパーコンポーネント
 */
struct Flipper {
  enum Side { Left, Right };
  Side side;                 ///< 左右の識別
  float maxAngle = 45.0f;    ///< 最大回転角 (度)
  float currentParam = 0.0f; ///< 現在の動作パラメータ (0.0 - 1.0)
  float turnSpeed = 10.0f;   ///< 回転速度
};

/**
 * @brief ゴルフゲーム状態
 */
struct GolfGameState {
  // UI エンティティ
  uint32_t headerEntity = 0;      ///< ヘッダーUI
  uint32_t shotCountEntity = 0;   ///< 打数表示UI
  uint32_t infoEntity = 0;        ///< 情報表示UI
  uint32_t windEntity = 0;        ///< 風表示UI
  uint32_t windArrowEntity = 0;   ///< 風矢印UI（画像）
  uint32_t gaugeBarEntity = 0;    ///< パワーゲージバーUI（画像）
  uint32_t gaugeFillEntity = 0;   ///< パワーゲージ中身UI（画像）
  uint32_t gaugeMarkerEntity = 0; ///< ゲージマーカーUI
  uint32_t pathEntity = 0;        ///< 経路表示UI
  uint32_t judgeEntity = 0;       ///< 判定表示UI
  uint32_t ballEntity = 0;        ///< ボールエンティティ

  std::string currentPage; ///< 現在の記事
  std::string targetPage;  ///< 目的記事
  int targetPageId = -1;   ///< 目的記事ID

  // 経路履歴
  std::vector<std::string> pathHistory; ///< 訪問した記事の履歴

  float fieldWidth = 20.0f; ///< フィールド幅（最小20）
  float fieldDepth = 30.0f; ///< フィールド奥行（最小30）

  // 風システム
  DirectX::XMFLOAT2 windDirection = {1.0f, 0.0f}; ///< 風向き（正規化）
  float windSpeed = 0.0f;                         ///< 風速（m/s）

  int shotCount = 0;        ///< 打数
  int par = 5;              ///< パー（リンク数÷2+2）
  int moveCount = 0;        ///< 遷移回数
  bool gameCleared = false; ///< クリアフラグ
  bool canShoot = true;     ///< ショット可能か

  // 結果画面UI
  uint32_t resultBgEntity = 0;    ///< 結果画面背景
  uint32_t resultTextEntity = 0;  ///< 結果テキスト
  uint32_t retryButtonEntity = 0; ///< おあいこボタン
};

/**
 * @brief ショット判定結果
 */
enum class ShotJudgement {
  None,  ///< 未判定
  Great, ///< 完璧（中央±5%）
  Nice,  ///< 良好（中央±15%）
  Miss   ///< ミス（それ以外）
};

/**
 * @brief ショット状態（みんなのゴルフ風パワーゲージ）
 */
struct ShotState {
  /// @brief ショットフェーズ
  enum class Phase {
    Idle,          ///< 待機中（クリックでパワー開始）
    PowerCharging, ///< パワーゲージ往復中
    ImpactTiming,  ///< インパクト待ち
    Executing,     ///< ショット実行中
    ShowResult     ///< 判定結果表示中
  };

  Phase phase = Phase::Idle;

  // パワーゲージ（0.0〜1.0を往復）
  float powerGaugePos = 0.0f;   ///< ゲージ現在位置
  float powerGaugeDir = 1.0f;   ///< ゲージ移動方向（1.0 or -1.0）
  float powerGaugeSpeed = 1.5f; ///< ゲージ速度（1秒で1.5往復）

  // インパクトゲージ（0.0〜1.0、0.5が中央）
  float impactGaugePos = 0.0f;
  float impactGaugeDir = 1.0f;
  float impactGaugeSpeed = 2.0f; ///< インパクトは速い

  // 確定値
  float confirmedPower = 0.0f;  ///< 確定パワー（0.0〜1.0）
  float confirmedImpact = 0.5f; ///< 確定インパクト（0.5が完璧）

  // 判定結果
  ShotJudgement judgement = ShotJudgement::None;
  float resultDisplayTime = 0.0f; ///< 結果表示残り時間

  // ショットパラメータ
  float maxPower = 35.0f; ///< 最大パワー（速度）

  // スピン入力（将来用）
  DirectX::XMFLOAT2 spinInput = {0.0f, 0.0f};
};

/**
 * @brief ゴルフホール（リンク用）
 */
struct GolfHole {
  std::string linkTarget; ///< リンク先記事
  float radius = 0.5f;    ///< 判定半径
  bool isTarget = false;  ///< 目的記事へのリンクか
  float gravity = 5.0f;   ///< 吸引力
};

} // namespace game::components

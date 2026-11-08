# GPU の計測・高速化とベンチ録画のナレッジ

2026年11月に AMD Radeon 610M（内蔵 GPU）で 60〜90 FPS を目指して行った計測と高速化から、
手順・判断基準・ハマりどころをまとめる。個々の詳細は各コミットにある。

## 1. 計測の道具

### 1.1 ベンチマークスクリプト `tools/bench/Run-Benchmark.ps1`

計測用ビルド（`-DWIKIGOLF_DEBUG_TOOLS=ON -DWIKIGOLF_PROFILING=ON`、既定の出力先 `build-perf\Release`）を
自動で動かし、プレイ中の GPU 時間と内訳を出す。

```powershell
pwsh -File tools/bench/Run-Benchmark.ps1 -Course 日本 -Seconds 75 -Frames 600 -AutoShot 8 -Preset LOW
```

| 引数 | 働き |
|---|---|
| `-Course 日本` | タイトルのコース選択でページを入れ、「確認」「スタート！」まで自動で進む（ゴールは既定で東京） |
| `-AutoShot 8` | 構えてから 8 秒待って自動で打つ（パワー 7 割・インパクトはパーフェクト付近）。カメラが動く場面も測れる |
| `-Seconds` / `-Frames` | 起動から N 秒で自動終了し、コース中（WikiGolfScene）の最後の M フレームを集計する |
| `-Preset LOW` など | 計測の間だけ `save\settings.ini` の画質を書き換え、終わったら戻す |
| `-GrassFull` | 芝の自動間引きを止めて全密度で描く |
| `-Record` | 画面を録画する（3章） |

出力の表は GPU 区間を階層で並べ、`Tri(k)` `VS(k)` `PS(k)` はその区間でラスタライズした三角形・頂点シェーダー・
画素シェーダーの実行数（千単位、1 フレームあたり）。

- 親の区間（`GPU.Meshes.Terrain` など）を直接計っていなければ、内訳の合計を出す。
- 区間の追加は `graphics::ScopedGpuTimer(ctx.graphics, "GPU.親.子")`。同じ名前はフレーム内で合算される。
- 実行数は D3D11 のパイプライン統計で、8 フレームに 1 回だけ取る（`kPipelineStatisticsInterval`）。
  生データは `profiling\session_*\performance_scopes.csv` の末尾の列。

### 1.2 数字の読み方

- **CPU の時間が GPU とほぼ同じなら GPU 待ち。** `RenderSystem.SubmitDraws` が CPU で 10ms 以上に見えても、
  中身はドライバーが GPU の終わりを待っている時間。
- **画素あたりの時間（ms ÷ PS 実行数）で比べる。** 打球のたびにカメラの位置が変わり、地形の時間は回によって
  2ms ほど揺れる。シェーダーの改善は ns/画素にすると揺れの影響を受けにくい。
- **PS 実行数 ≒ 画面の画素数なら、重ね塗りは起きていない。** 地形は深度パスのおかげで画面 1 回分しか塗っておらず、
  重いのは 1 画素あたりの計算そのもの（約 5.6ns/画素、DXBC で約 800 命令）だった。

### 1.3 比べ方（揺れへの対処）

- **同じ回の中で交互に測る（A/B を 2〜3 周）。** 本体の温度で全体が 2ms ほど上下するので、日や時刻の違う数字は比べない。
- **外れ値はやり直す。** 地形が 0ms・フレームが 3ms 台の回は、UI 画面のまま終わっている。他の作業で CPU が使われた回も捨てる。
- **部品を外して測る（アブレーション）。** シェーダーの一部を正規表現で一時的に無効化して測り、終わったら戻す。
  効果は足し算にならない（部品どうしが並行して動く）ので、順位を見る材料にする。
- **命令数は fxc で見る。** `fxc /T ps_5_0 /E main /O3 /Fc out.asm TerrainPS.hlsl` の
  `// Approximately N instruction slots used`。

## 2. 610M で分かったこと

### 2.1 効いた変更

| 変更 | 前 → 後 | 要点 |
|---|---|---|
| ミニマップの穴アイコン（D2D） | UI 71ms → 0.9ms | 毎回 ColorMatrix 効果を掛けていた。色ごとに一度だけ描いた画像をキャッシュ |
| 地形の深度パス | 24.1 → 19.8ms（MEDIUM） | `discard` があると AMD では早期深度テストが効かない。先に深度だけ書き（カップに掛かるタイルだけくり抜く）、本描画は深度一致の画素だけ塗る |
| 地表の記事を地形の描画で重ねる | 28.8 → 24.1ms（MEDIUM） | 同じ形のメッシュをもう一度描く分をなくす |
| 空を不透明物のあとに描く | 0.79 → 0.13ms（MEDIUM） | 深度テスト付き・深度 1.0 で描き、何もない画素だけ塗る |
| 消えきった芝の繊維の計算を飛ばす | 地形の画素あたり −8% | 最も粗い繊維まで消える距離で、繊維の計算をまとめて飛ばす（絵は同じ） |
| 記事の濃淡ノイズを 4 段 → 2 段 | 地形の画素あたり −9%（LOW） | 省いた段の平均を足して明るさを揃える |
| LOW: ブルームと拡大時のシャープ化を省く | 後処理 1.0 → 0.45ms、拡大 1.15 → 0.5ms | プリセットで LOW を選んだときだけ（`QualitySettings::lightPostProcess`） |

結果（「日本」・自動ショット）: LOW 約 11ms（約 90 FPS）、MEDIUM 約 20ms（約 50 FPS）。

### 2.2 効かなかった・悪化した変更

| 試したこと | 結果 | 理由の見立て |
|---|---|---|
| 既存の `[branch]` を `[flatten]` に | +6ms | 両方の側を常に計算する |
| 画素ごとの細かい分岐でアルベドを飛ばす（SampleGrad） | 悪化 | 分岐が画素ごとにばらつく |
| テクスチャのミップを粗くする（SampleBias） | 変わらず | 帯域ではなく計算と待ち時間が重い |
| 法線マップ・影を外す | 0 前後（MEDIUM 時点） | ボトルネックではなかった |

分岐で処理を飛ばすなら、**画面上でまとまって同じ側に入る条件**（距離・描画単位）にする。
画素ごとにばらつく条件は内蔵 GPU では逆効果になりやすい。

### 2.3 残っている重さ（LOW、1 フレームあたり）

- 地形 5〜7ms：繊維模様・記事の重ね（異方性 8x で読む）・地表テクスチャが大きい
- 影 約 1ms：600〜1,100 回の描画。3 分の 2 は地形を影のマップへ描く分
- 半透明 約 1ms：大半はコース境界の壁（画面の 40〜100% を覆う）
- MEDIUM の芝：GPU 負荷が 14ms を超えると自動間引きで最小まで減る（芝自体は全密度でも約 1.4ms）

## 3. ベンチの録画

```powershell
pwsh -File tools/bench/Run-Benchmark.ps1 -Course 日本 -AutoShot 8 -Preset LOW -Record
```

`build-perf\Release\recordings\bench_LOW_<日時>.mp4`（1280x720・30fps・約 1 分で 4MB 前後）ができる。
録画の負荷で FPS は下がるので、**数字は録画なしの回で取る**。中身は `tools/bench/BenchRecording.ps1`。
ffmpeg が要る（`winget install Gyan.FFmpeg.Essentials`。PATH になくても winget の場所から探す）。

### 3.1 ハマりどころ

| 症状 | 原因 | 対処 |
|---|---|---|
| gdigrab が真っ白・真っ黒、ddagrab がフレームを返さず止まる、gfxcapture が 1 コマで終わる | 離席中で画面がスリープし、デスクトップが合成されていない | マウスを 1 画素動かして起こし、`SetThreadExecutionState(CONTINUOUS\|SYSTEM\|DISPLAY_REQUIRED)` で録画中は消さない |
| ゲームの位置を録っても別のウィンドウが映る | スクリプトから起動したゲームは前面に出ない | 録画の間だけ `SetWindowPos(HWND_TOPMOST)`。ゲームの終了で消える |
| 最前面にしたら gdigrab が黒い | 最前面のゲームは DWM の合成を通らず直接表示される | Desktop Duplication（ddagrab）でモニターから描画範囲を切り出す |
| 起動直後に録画が切れる | ウィンドウの作り直し・大きさの変更 | ウィンドウが出てから 6 秒待って位置を取り直す |
| 複数モニターで場所がずれる | ddagrab はモニター単位 | ウィンドウのあるモニター基準の座標にする。主モニター以外なら `-RecordOutputIndex` |

## 4. その他のハマりどころ

- **ラベルに空白を入れない。** `Start-Process` に引数を渡すと空白で割れ、`-Label "LOW 録画中"` の後半が
  ゴールのページ名として渡り、コースに入れなかった。
- **`tool_shader_precompile Assets\shaders` を手で走らせない。** ソースのフォルダに `.cso` とハッシュを書く。
  ゲームはハッシュが合うときしか使わないので計測は狂わないが、消すには
  `git clean -f -- "Assets/shaders/*.cso" "Assets/shaders/precompiled_shaders.hash"`。
- **テストは ctest で回す。** テスト用の exe を直接まとめて起動すると、引数が要るもの（`test_particle_rendering`、
  `tool_shader_precompile`）が失敗に見える。逆に ctest では PATH に `build-perf\Release` がなく、
  `resource_manager_rules` が DLL 不足のエラー表示で止まる（直接 PATH を通せば通る）。
- **別のワークツリーのゲームを止めない。** 同じ名前の WikiGolf.exe が他のワークツリーから動いていることがある。
  録画・計測では `Path` が自分のビルドのものだけを扱う。

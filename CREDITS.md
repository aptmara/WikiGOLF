# WikiGOLF クレジット / Credits

各ライセンスの全文は `licenses/` フォルダにあります。

## 第三者素材
| 素材 | 作者・配布元 | ライセンス |
|---|---|---|
| Wikipedia 記事データ（同梱のリンクグラフ・実行時取得） | Wikipedia 執筆者 | CC BY-SA 4.0 / GFDL（`licenses/WIKIPEDIA-LICENSE`） |
| タイトル画面の写真 `Golfer_swing.jpg` | Lilrizz（Wikimedia Commons） | CC0 1.0（`licenses/GOLFER-SWING-PHOTO-LICENSE`） |
| 効果音・BGM `Assets/sounds/*` | OtoLogic（https://otologic.jp） | CC BY 4.0（`licenses/SOUNDS-LICENSE`） |
| 効果音 `Assets/sounds/*` | 効果音ラボ（https://soundeffect-lab.info/） | 同サイトの利用規約（`licenses/SOUNDS-LICENSE`） |
| 入力アイコン `Assets/ui/*` | Kenney（https://kenney.nl） | CC0 1.0（`licenses/Kenney-LICENSE`） |
| フォント Kiwi Maru | The Kiwi Maru Project Authors | SIL OFL 1.1（`licenses/KiwiMaru-OFL.txt`） |
| フォント Barlow Condensed | The Barlow Project Authors | SIL OFL 1.1（`licenses/BarlowCondensed-OFL.txt`） |
| フォント Share Tech Mono | Carrois Type Design, Ralph du Carrois | SIL OFL 1.1（`licenses/ShareTechMono-OFL.txt`） |

## ライブラリ
| ライブラリ | ライセンス |
|---|---|
| Assimp 6.0.2 | BSD 3-Clause（`licenses/assimp-LICENSE`） |
| Zstandard (zstd) 1.5.7 | BSD 3-Clause（`licenses/zstd-LICENSE`） |
| litehtml / gumbo | BSD 3-Clause / Apache 2.0（ビルド時に `licenses/` へ同梱） |
| stb_image_write | パブリックドメイン / MIT |
| Dear ImGui（デバッグビルドのみ） | MIT |

## 生成AIの使用
| 対象 | ツール | 内容 |
|---|---|---|
| 地形テクスチャ `terrain_materials/*`、草テクスチャ `GRASS_BASE.png` | ChatGPT（OpenAI 画像生成） | 画像生成 |
| 地形UIアイコン `all_terrain_ui_assets/*` | ChatGPT（OpenAI 画像生成） | 画像生成 |
| タイトル画面のスカイボックス `skybox_default_px_*.png` | Gemini（Google 画像生成） | 画像生成 |
| 判定文字・HUD画像 `ui_judge_*`、`ui_terrain_*`、`ui_wind_arrow`、`golf_ball_icon_transparent` | ChatGPT（OpenAI 画像生成）※要確認 | 画像生成後に背景透過などの加工 |
| ソースコード | Claude（Anthropic）など | 実装・修正の補助。設計と最終判断は開発者本人 |

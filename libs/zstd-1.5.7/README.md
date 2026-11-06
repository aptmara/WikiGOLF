# zstd（展開部分だけ）

リンクグラフ `Assets/data/jawiki.linkgraph` の展開に使います（[WikiLinkGraph.cpp](../../src/game/systems/WikiLinkGraph.cpp)）。

- 元: https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
  （SHA-256 `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`）
- ライセンス: BSD（[LICENSE](LICENSE)）。配布物には `licenses/zstd-LICENSE` として同梱し、CREDITS.md にも載せている
- 取り込んだもの: `lib/zstd.h`, `lib/zstd_errors.h`, `lib/common/*.h`,
  `lib/common/` の debug / entropy_common / error_private / fse_decompress / xxhash / zstd_common の `.c`,
  `lib/decompress/` の `.c` と `.h`（アセンブリ版 `.S` は除く）。中身は変えていない
- 圧縮はツール側（Python 3.14 標準の `compression.zstd`）で行うので、圧縮部分は入れていない

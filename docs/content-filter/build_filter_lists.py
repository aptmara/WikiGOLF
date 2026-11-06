#!/usr/bin/env python3
"""記事フィルター Assets/data/content_filter.tsv を生成します。

日本語版Wikipediaのカテゴリをたどって除外候補を集め、同梱のリンクグラフ
（Assets/data/jawiki.linkgraph）からリダイレクトも展開します。ネットワーク接続と
リンクグラフの実体（Git LFS）が必要です。

    python docs/content-filter/build_filter_lists.py

出力形式（UTF-8 / タブ区切り）: level<TAB>page_id<TAB>title
    level 1 = 常時除外（明確に性的な記事のみ）
    level 2 = セーフモードで追加除外

除外の範囲を変えるときは、下の LEVEL1 / LEVEL2 / EXCLUDED を編集します。
誤判定はカテゴリ単位で EXCLUDED に追加し、個別の記事は ALLOW_TITLES または
SAFE_MODE_ONLY_TITLES に追加します。変更後は、被リンク数の多い記事に無関係なものが
混ざっていないかを必ず確認してください。
"""

import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

API = "https://ja.wikipedia.org/w/api.php?"
HEADERS = {
    "User-Agent": "WikiGOLF-content-filter-build/1.0 "
    "(https://github.com/aptmara/WikiGOLF)"
}
ROOT = Path(__file__).resolve().parents[2]
GRAPH_PATH = ROOT / "Assets" / "data" / "jawiki.linkgraph"
sys.path.insert(0, str(ROOT / "tools" / "linkgraph"))
OUTPUT_PATH = ROOT / "Assets" / "data" / "content_filter.tsv"

# (カテゴリ名, 下位カテゴリをたどる深さ)
# 上位カテゴリを深くたどると、ピンク映画に出演した俳優や暗殺に関わった歴史上の人物など
# 無関係な記事が大量に混ざる。内容が揃った具体的なカテゴリだけを指定すること。

# 常時除外: 性行為そのものの記事と、ポルノ産業の記事だけに絞る（最小限）
LEVEL1 = [
    ("日本のAV女優", 1),
    ("日本のAV男優", 0),
    ("アメリカ合衆国のポルノ女優", 0),
    ("日本のアダルトビデオメーカー", 0),
    ("日本のAV事務所", 0),
    ("アダルトビデオの賞", 0),
    ("ポルノ映画の賞", 0),
    ("ポルノグラフィ専門テレビチャンネル", 0),
    ("アダルトサイト", 0),
    ("日本のアダルトサイト", 0),
    ("アメリカ合衆国のアダルトサイト", 0),
    ("ポルノグラフィ", 0),
    ("ポルノグラフィ用語", 0),
    ("ジャンル別のポルノグラフィ", 0),
    ("ヘンタイ", 0),
    ("性交体位", 0),
    ("射精", 0),
    ("肛門性愛", 0),
    ("オーラル・エロティシズム", 0),
    ("フェラチオ", 0),
    ("クンニリングス", 0),
    ("マスターベーション", 0),
    ("非挿入性交", 0),
    ("大人のおもちゃ", 1),
    ("児童ポルノ", 0),
    ("児童買春", 0),
]

# セーフモードで追加除外: 成人向け作品、性に関する話題、殺人・自殺・テロなどの事件
# 戦争や虐殺などの歴史的な出来事は学習にも関わるため、対象にしない
LEVEL2 = [
    ("アダルトゲーム", 2),
    ("成人向け漫画", 2),
    ("アダルト雑誌", 1),
    ("日本のポルノ映画", 0),
    ("アメリカ合衆国のポルノ映画", 0),
    ("日活ロマンポルノ", 0),
    ("東映ポルノ", 0),
    ("日本のAV監督", 0),
    ("性行為", 0),
    ("BDSM", 1),
    ("パラフィリア", 0),
    ("フェティシズム", 1),
    ("ペドフィリア", 0),
    ("売買春", 0),
    ("娼婦", 0),
    ("性犯罪", 0),
    ("強姦", 0),
    ("性犯罪事件", 1),
    ("児童性的虐待事件", 0),
    ("殺人事件", 0),
    ("一家殺傷事件", 0),
    ("バラバラ殺人", 1),
    ("保険金殺人", 0),
    ("未解決の殺人事件", 1),
    ("無差別殺人事件", 0),
    ("日本の連続殺人事件", 0),
    ("銃乱射事件", 1),
    ("拷問", 0),
    ("拷問方法", 1),
    ("自殺", 0),
    ("自殺の方法", 0),
    ("自殺事件", 0),
    ("いじめ自殺事件", 0),
    ("集団自殺", 0),
    ("テロ事件", 0),
    ("自爆テロ", 0),
    ("航空機テロ", 0),
    ("鉄道テロ", 0),
    ("処刑方法", 0),
]

# ここに挙げたカテゴリには入らない（人物一覧や作品、無関係な話題が混ざるため）
EXCLUDED = {
    "アダルトビデオを題材とした作品",
    "アダルトゲーム声優",
    "アダルトゲームの主題歌",
    "アダルトゲームのアルバム",
    "アダルトゲームライター",
    "アダルトゲームの登場人物",
    "アダルトゲーム関連の団体",
    "コンピュータソフトウェア倫理機構",
    "テレビアニメ化されたアダルトゲーム",
    "成人向け漫画家",
    "フェミニズムとポルノグラフィ",
    "反ポルノグラフィ・フェミニスト",
    "ポルノ法",
    "BDSM作品",
    "性犯罪を題材とした作品",
    "強姦を題材とした作品",
    "売春を題材とした作品",
    "ペドフィリアを題材とした作品",
    "少年愛",
    "萌え属性",
    "テロリズムを題材とした作品",
    "シリアルキラーを題材とした作品",
    "各国の性犯罪者",
    "日本の性犯罪者",
    "アメリカ合衆国の性犯罪者",
    "ベルリンの戦い",
    "処刑器具",
    "拷問器具",
    "去勢",
}

# どちらのモードでも除外しない記事（カテゴリの誤判定や、一般的な話題）
ALLOW_TITLES = {
    "いじめ",
    "毒",
    "鞭",
    "十字架",
    "桜田門外の変",
    "遊女",
    "花魁",
    "纏足",
    "脚線美",
    "くすぐり",
    "コンドーム",
    "ジャニー喜多川",
    "魔族_(お笑いコンビ)",
    "宦官",
    "プリアーポス",
    "Rakuten_TV",
    "笠倉出版社",
    "大陸書房",
    "三和出版",
    "東京スポーツ映画大賞",
    "チャイナ_(プロレスラー)",
    "ラーズ・サリバン",
    "松本竜助",
    "梅垣義明",
    "江川達也",
    "岩井志麻子",
    "中島知子",
    "中島丈博",
    "宝島_(雑誌)",
    "麻雀放浪記",
    "屋根裏の散歩者",
    "2パック",
    "殉死",
    "シアン化カリウム",
    "一酸化炭素中毒",
    "沖縄戦における集団自決",
    "尼港事件",
    "泣きゲー",
    "セーファーセックス",
}

# 常時除外のカテゴリに入るが、明確に性的とは言えないためセーフモードだけで除外する
SAFE_MODE_ONLY_TITLES = {
    "児童買春、児童ポルノに係る行為等の規制及び処罰並びに児童の保護等に関する法律",
    "男性間性行為",
    "精通",
    "夢精",
    "射精後不応期",
    "膣内射精障害",
    "貞操帯",
    "DLsite",
    "飯島愛",
}

MAX_REQUESTS = 4000
REQUEST_INTERVAL_SECONDS = 0.12


class CategoryClient:
    def __init__(self):
        self.request_count = 0
        self.cache = {}

    @staticmethod
    def fetch(request):
        for attempt in range(5):
            try:
                with urllib.request.urlopen(request, timeout=30) as response:
                    return json.load(response)
            except urllib.error.HTTPError as error:
                if error.code != 429 or attempt == 4:
                    raise
                time.sleep(int(error.headers.get("Retry-After") or 30))

    def members(self, category):
        if category in self.cache:
            return self.cache[category]
        pages, subcategories = [], []
        continuation = {}
        while True:
            if self.request_count >= MAX_REQUESTS:
                raise RuntimeError("APIリクエスト数の上限に達しました")
            query = {
                "action": "query",
                "list": "categorymembers",
                "cmtitle": "Category:" + category,
                "cmlimit": "500",
                "cmtype": "page|subcat",
                "cmprop": "ids|title|type",
                "format": "json",
                "formatversion": "2",
                **continuation,
            }
            request = urllib.request.Request(
                API + urllib.parse.urlencode(query), headers=HEADERS
            )
            data = self.fetch(request)
            self.request_count += 1
            time.sleep(REQUEST_INTERVAL_SECONDS)
            for member in data["query"]["categorymembers"]:
                if member["type"] == "subcat":
                    subcategories.append(member["title"].split(":", 1)[1])
                elif member["ns"] == 0:
                    pages.append((member["pageid"], member["title"]))
            if "continue" not in data:
                break
            continuation = data["continue"]
        self.cache[category] = (pages, subcategories)
        return self.cache[category]


def normalize(title):
    return title.replace(" ", "_")


def collect(client, seeds):
    """カテゴリをたどり、{page_id: title} を返します。"""
    pages = {}
    empty = []
    best_depth = {}
    frontier = list(seeds)
    while frontier:
        category, depth = frontier.pop()
        if category in EXCLUDED or best_depth.get(category, -1) >= depth:
            continue
        best_depth[category] = depth
        members, subcategories = client.members(category)
        if not members and not subcategories:
            empty.append(category)
        for page_id, title in members:
            pages[page_id] = normalize(title)
        if depth > 0:
            frontier.extend((sub, depth - 1) for sub in subcategories)
    return pages, empty, len(best_depth)


def load_graph():
    """リンクグラフからタイトル辞書と人気記事を読みます。"""
    import numpy as np
    from verify_linkgraph import LinkGraph, decode_titles

    graph = LinkGraph(GRAPH_PATH)
    meta = json.loads(graph.raw("meta"))
    titles, redirect = decode_titles(graph.raw("titles"))
    planes = np.frombuffer(graph.raw("title_ids"), np.uint8).reshape(3, -1).astype(np.uint32)
    title_ids = planes[0] | planes[1] << 8 | planes[2] << 16
    wiki_ids = np.frombuffer(graph.raw("wiki_ids"), np.uint32)
    popular_raw = graph.raw("popular")
    count = int.from_bytes(popular_raw[:4], "little")
    pairs = np.frombuffer(popular_raw, np.uint32, offset=4).reshape(count, 2)
    popular = [int(wiki_ids[a]) for a, indeg in pairs if indeg >= 10000][:2000]
    redirects = [(t.decode("utf-8", "surrogateescape"), int(wiki_ids[title_ids[i]]))
                 for i, t in enumerate(titles) if redirect[i]]
    return {
        "articles": meta["articles"],
        "redirects": redirects,
        "redirect_titles": {t for t, _ in redirects},
        "popular": popular,
    }


def main():
    client = CategoryClient()
    level1, empty1, categories1 = collect(client, LEVEL1)
    level2, empty2, categories2 = collect(client, LEVEL2)
    for page_id in level1:
        level2.pop(page_id, None)

    allow = {normalize(title) for title in ALLOW_TITLES}
    safe_mode_only = {normalize(title) for title in SAFE_MODE_ONLY_TITLES}
    level2.update({i: t for i, t in level1.items() if t in safe_mode_only})
    level1 = {i: t for i, t in level1.items() if t not in allow and t not in safe_mode_only}
    level2 = {i: t for i, t in level2.items() if t not in allow}

    graph = load_graph()
    # 転送ページ自体がカテゴリに入っていることがある（別会社の旧サービス名など）。
    # 転送先の記事が対象のときだけ、下の展開で除外する。
    level1 = {i: t for i, t in level1.items() if t not in graph["redirect_titles"]}
    level2 = {i: t for i, t in level2.items() if t not in graph["redirect_titles"]}

    level_by_id = {i: 1 for i in level1}
    level_by_id.update({i: 2 for i in level2})

    rows = {}
    for page_id, title in level1.items():
        rows[page_id] = (1, title)
    for page_id, title in level2.items():
        rows[page_id] = (2, title)

    # カップのリンクや手入力はリダイレクト名で来ることがあるため展開する。
    # リンクグラフは転送ページの page_id を持たないので、転送先の page_id で書く
    # （ゲームは page_id では転送先を、タイトルでは転送ページ名を判定する）。
    redirect_rows = []
    for title, target_page_id in graph["redirects"]:
        level = level_by_id.get(target_page_id)
        if level is not None:
            redirect_rows.append((level, target_page_id, title))

    popular = set(graph["popular"])
    with OUTPUT_PATH.open("w", encoding="utf-8", newline="\n") as output:
        for page_id, (level, title) in sorted(rows.items(), key=lambda r: (r[1][0], r[0])):
            output.write(f"{level}\t{page_id}\t{title}\n")

    real_pages = graph["articles"]
    hits = {
        level: [rows[i][1] for i in popular if i in rows and rows[i][0] == level]
        for level in (1, 2)
    }
    summary = {
        "requests": client.request_count,
        "level1_articles": len(level1),
        "level2_articles": len(level2),
        "redirects": len(redirect_rows),
        "rows": len(rows) + len(redirect_rows),
        "db_articles": real_pages,
        "level1_percent": round(len(level1) / real_pages * 100, 3),
        "level1_and_2_percent": round((len(level1) + len(level2)) / real_pages * 100, 3),
        "categories": [categories1, categories2],
        "missing_or_empty_categories": empty1 + empty2,
        "popular_goal_candidates": len(popular),
        "popular_hits_level1": hits[1],
        "popular_hits_level2": hits[2],
        "output_bytes": OUTPUT_PATH.stat().st_size,
    }
    sys.stdout.reconfigure(encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()

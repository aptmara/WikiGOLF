#!/usr/bin/env python3
"""
日本語版 Wikipedia のダンプから、WikiGOLF 用のリンクグラフファイルを作ります。

    python tools/linkgraph/build_linkgraph.py                 # 最新の完了済みダンプで作る
    python tools/linkgraph/build_linkgraph.py --date 20261101

手順（全コアを使います）:
  1. ダンプ4本（page / redirect / linktarget / pagelinks）を取得し、SHA-1 を確かめる
  2. 4本を同時に展開し、ブロックごとに全プロセスで解析する
  3. 転送を解決して「記事 -> 記事」のリンクにし、常時除外（content_filter.tsv の
     レベル1）の記事を取り除く
  4. linkgraph_order（C++）で重複除去・ID の振り直し（再帰的グラフ二分割）・
     入リンク一覧の符号化を行う
  5. タイトル辞書などと合わせ、LZMS で圧縮して1ファイルに書き出す

必要なもの: Windows、Python 3.10 以降、numpy、CMake と C++ コンパイラ。

圧縮は Windows 標準の Compression API（cabinet.dll）の LZMS を使う。ゲーム側も
同じ API で展開するので、外部ライブラリが要らない（zstd -19 より 5% ほど大きい）。

出力ファイルの形式（リトルエンディアン）:
  ヘッダ 32 バイト: b'WGLG', u32 版=2, u32 セクション数, u32 予約, u64 ファイル長, u64 予約
  セクション表（48 バイト x セクション数）:
      char 名前[16], u64 位置, u64 格納サイズ, u64 展開後サイズ, u32 圧縮(0=なし,2=LZMS),
      u32 フレームの展開後サイズ（0 ならセクション全体で1フレーム）
  圧縮したセクションの中身: u32 フレーム数, u32 予約, u64 各フレームの圧縮後サイズ[フレーム数],
      続いて各フレーム（LZMS のバッファ形式）。各フレームは独立していて並列に展開できる。
  セクション:
    meta        JSON（元ダンプの日付・件数・作成日時など）
    in_lists    記事ごとの入リンク一覧を ID 順に連結したもの。各一覧は昇順の ID を
                「前の ID との差 - 1」（先頭は -1 からの差）の LEB128 で並べる。
                展開後 16MiB ごとのフレーム
    in_len      u32[記事数] 各記事の入リンク一覧のバイト数
    titles      タイトル辞書。UTF-8 のバイト順に並べ、64 件ごとのブロックで前方圧縮:
                u8 見出し（下位7ビット: 前の見出しと共通の先頭バイト数。ブロック先頭は 0。
                最上位ビット: 1 なら転送ページのタイトル）、残りのバイト、0x00
                記事のタイトル（各記事ちょうど1件）と、記事への転送ページのタイトルの両方を含む。
                展開後 4MiB ごとのフレーム（64件のブロックの途中で切れることがある）
    title_ids   titles と同じ順の記事 ID。下位・中位・上位バイトの3面に分けて格納
    wiki_ids    u32[記事数] 記事 ID -> Wikipedia の page_id
    popular     u32 件数, 続いて (u32 記事 ID, u32 被リンク数) を被リンク数の多い順に
                （被リンク 5000 件以上の記事）
  記事 ID は 0 から記事数-1。タイトルの空白はアンダースコア。
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import multiprocessing as mp
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import warnings
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

from lzms import compress_frames

ROOT = Path(__file__).resolve().parents[2]
TOOL_DIR = Path(__file__).resolve().parent
DUMPS = 'https://dumps.wikimedia.org/jawiki/'
USER_AGENT = 'WikiGOLF-linkgraph-build/1.0 (https://github.com/aptmara/WikiGOLF)'
TABLES = ('page', 'redirect', 'linktarget', 'pagelinks')
JOBS = {'page': 'pagetable', 'redirect': 'redirecttable',
        'linktarget': 'linktargettable', 'pagelinks': 'pagelinkstable'}
BLOCK_BYTES = 32 << 20
FRAME_BYTES = 16 << 20
TITLE_FRAME_BYTES = 4 << 20
POPULAR_MIN_INCOMING = 5000
T0 = time.time()
warnings.filterwarnings('ignore', category=DeprecationWarning)


def log(*args) -> None:
    print(f'[{time.time() - T0:7.1f}s]', *args, flush=True)


# ---------------------------------------------------------------------------
# 1. ダンプの取得
# ---------------------------------------------------------------------------

def http_get(url: str, attempts: int = 8) -> bytes:
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504) or attempt + 1 == attempts:
                raise
        except urllib.error.URLError:
            if attempt + 1 == attempts:
                raise
        time.sleep(min(60, 5 * 2 ** attempt))
    raise RuntimeError(url)


def latest_complete_date() -> str:
    index = http_get(DUMPS).decode()
    for date in sorted(set(re.findall(r'href="(\d{8})/"', index)), reverse=True):
        try:
            status = json.loads(http_get(f'{DUMPS}{date}/dumpstatus.json'))
        except Exception:
            continue
        if all(status['jobs'].get(JOBS[t], {}).get('status') == 'done' for t in TABLES):
            return date
    raise RuntimeError('完了済みのダンプが見つかりません')


def download(date: str, dump_dir: Path) -> None:
    dump_dir.mkdir(parents=True, exist_ok=True)
    sums = http_get(f'{DUMPS}{date}/jawiki-{date}-sha1sums.txt').decode()
    expected = {name: digest for digest, name in
                (line.split() for line in sums.splitlines() if line.strip())}

    def fetch(table: str) -> None:
        name = f'jawiki-{date}-{table}.sql.gz'
        path = dump_dir / name
        if path.exists() and sha1(path) == expected[name]:
            log('dump ok (cached)', name)
            return
        for attempt in range(8):
            tmp = path.with_suffix('.part')
            try:
                request = urllib.request.Request(f'{DUMPS}{date}/{name}',
                                                 headers={'User-Agent': USER_AGENT})
                with urllib.request.urlopen(request, timeout=120) as response, \
                        open(tmp, 'wb') as out:
                    shutil.copyfileobj(response, out, 8 << 20)
                if sha1(tmp) != expected[name]:
                    raise RuntimeError(f'{name}: SHA-1 が一致しません')
                tmp.replace(path)
                log('dump downloaded', name, f'{path.stat().st_size / 1e6:.0f}MB')
                return
            except urllib.error.HTTPError as error:
                if error.code != 429 or attempt == 7:
                    raise
                time.sleep(min(120, 10 * 2 ** attempt))

    # dumps.wikimedia.org は同時接続数を絞っているので2本ずつ
    with ThreadPoolExecutor(2) as pool:
        list(pool.map(fetch, sorted(TABLES, key=lambda t: t != 'pagelinks')))


def sha1(path: Path) -> str:
    digest = hashlib.sha1()
    with open(path, 'rb') as f:
        while chunk := f.read(16 << 20):
            digest.update(chunk)
    return digest.hexdigest()


# ---------------------------------------------------------------------------
# 2. ダンプの解析（ワーカープロセスで実行する関数）
# ---------------------------------------------------------------------------

STR = rb"'((?:[^'\\]|\\.)*)'"
RX = {
    'page': re.compile(rb'^\((\d+),0,' + STR + rb',(\d),', re.M),
    'redirect': re.compile(rb'^\((\d+),0,' + STR + rb",(NULL|'(?:[^'\\]|\\.)*')", re.M),
    'linktarget': re.compile(rb'^\((\d+),0,' + STR + rb'\)', re.M),
}
NON_DATA = re.compile(rb'^[^(\n].*$', re.M)
UNESCAPE = re.compile(rb'\\(.)')
UNESCAPE_MAP = {b'0': b'\0', b'n': b'\n', b'r': b'\r', b't': b'\t', b'Z': b'\x1a'}


def unescape(raw: bytes) -> str:
    if b'\\' in raw:
        raw = UNESCAPE.sub(lambda m: UNESCAPE_MAP.get(m.group(1), m.group(1)), raw)
    return raw.decode('utf-8', 'surrogateescape')


POWERS = 10.0 ** np.arange(20)


def parse_numbers(block: bytes, columns: int) -> np.ndarray:
    """数字だけのタプル行 "(a,b,c)," を (行数, columns) の整数配列にする。"""
    data = np.frombuffer(NON_DATA.sub(b'', block), np.uint8)
    digits = np.flatnonzero((data >= 48) & (data <= 57)).astype(np.int32)
    if len(digits) == 0:
        return np.empty((0, columns), np.int64)
    first = np.empty(len(digits), bool)                  # 数の先頭の桁
    first[0] = True
    np.not_equal(np.diff(digits), 1, out=first[1:])
    starts = np.flatnonzero(first).astype(np.int32)
    lengths = np.diff(np.append(starts, np.int32(len(digits))))
    number = np.repeat(np.arange(len(starts), dtype=np.int32), lengths)
    place = np.repeat(starts + lengths - 1, lengths) - np.arange(len(digits), dtype=np.int32)
    values = np.bincount(number, weights=(data[digits] - 48) * POWERS[place])
    return values.astype(np.int64).reshape(-1, columns)


def parse_block(args):
    table, block = args
    rows = RX[table].findall(block)
    if table == 'page':
        return table, [(int(i), unescape(t), r == b'1') for i, t, r in rows]
    if table == 'redirect':
        return table, [(int(i), unescape(t)) for i, t, iw in rows if iw in (b'NULL', b"''")]
    return table, [(int(i), unescape(t)) for i, t in rows]


def parse_pagelinks_range(args):
    """展開済みの pagelinks.sql の [start, end) を読み、記事名前空間からのリンクを保存する。"""
    path, start, end, out = args
    with open(path, 'rb') as f:
        f.seek(max(0, start - 1))
        data = f.read(end - start + 1 + (1 << 20))
    begin = 0 if start == 0 else data.index(b'\n') + 1   # 前の範囲が受け持つ途中の行を飛ばす
    stop = data.find(b'\n', end - max(0, start - 1) - 1)
    stop = len(data) if stop < 0 else stop + 1
    values = parse_numbers(data[begin:stop], 3)
    values = values[values[:, 1] == 0]
    np.save(out, np.stack([values[:, 0], values[:, 2]], axis=1).astype(np.uint32))
    return out


def decompress(src: Path, dst: Path) -> None:
    with gzip.open(src, 'rb') as f, open(dst, 'wb') as out:
        shutil.copyfileobj(f, out, 32 << 20)


def iter_blocks(table: str, path: Path, gate: threading.Semaphore):
    with gzip.open(path, 'rb') as f:
        rest = b''
        while True:
            chunk = f.read(BLOCK_BYTES)
            if not chunk:
                break
            chunk = rest + chunk
            cut = chunk.rfind(b'\n') + 1
            rest = chunk[cut:]
            gate.acquire()
            yield table, chunk[:cut]
        if rest:
            gate.acquire()
            yield table, rest


def parse_dumps(date: str, dump_dir: Path, work: Path, processes: int) -> dict:
    results = {t: [] for t in TABLES}
    gate = threading.Semaphore(processes * 2)   # 展開が解析より先に行き過ぎないように
    with mp.Pool(processes) as pool:
        def run(table: str) -> None:
            path = dump_dir / f'jawiki-{date}-{table}.sql.gz'
            for name, value in pool.imap_unordered(parse_block, iter_blocks(table, path, gate)):
                results[name].append(value)
                gate.release()
            log('parsed', table)

        def run_pagelinks() -> None:
            # 最大のダンプ。いったん展開して書き出し、各プロセスが担当範囲を直接読む
            # （大きなブロックをプロセス間で送らない）
            raw = work / 'pagelinks.sql'
            decompress(dump_dir / f'jawiki-{date}-pagelinks.sql.gz', raw)
            log('decompressed pagelinks', f'{raw.stat().st_size / 1e9:.2f}GB')
            parts = work / 'pagelinks-parts'
            shutil.rmtree(parts, ignore_errors=True)
            parts.mkdir()
            size = raw.stat().st_size
            step = 4 << 20   # 1プロセスあたりの作業メモリを小さく保つ
            tasks = [(str(raw), s, min(size, s + step), str(parts / f'{i:05d}.npy'))
                     for i, s in enumerate(range(0, size, step))]
            for out in pool.imap_unordered(parse_pagelinks_range, tasks):
                results['pagelinks'].append(np.load(out))
            shutil.rmtree(parts, ignore_errors=True)
            raw.unlink()
            log('parsed pagelinks')

        # gzip の展開はスレッドで並行させる（zlib は GIL を手放す）
        with ThreadPoolExecutor(len(TABLES)) as threads:
            jobs = [threads.submit(run, t) for t in TABLES if t != 'pagelinks']
            jobs.append(threads.submit(run_pagelinks))
            for job in jobs:
                job.result()
    return results


# ---------------------------------------------------------------------------
# 3. 記事 -> 記事のリンクにする
# ---------------------------------------------------------------------------

def build_graph(parsed: dict, filter_path: Path | None):
    title_to_id: dict[str, int] = {}
    is_redirect: dict[int, bool] = {}
    for rows in parsed['page']:
        for pid, title, red in rows:
            title_to_id[title] = pid
            is_redirect[pid] = red
    redirect_title: dict[int, str] = {}
    for rows in parsed['redirect']:
        for pid, title in rows:
            if is_redirect.get(pid):
                redirect_title[pid] = title

    def resolve(title: str) -> int:
        for _ in range(6):   # 転送は5段までたどる
            pid = title_to_id.get(title)
            if pid is None:
                return -1
            if not is_redirect[pid]:
                return pid
            title = redirect_title.get(pid)
            if title is None:
                return -1
        return -1

    article_wiki = np.array(sorted(p for p, r in is_redirect.items() if not r), dtype=np.int64)
    blocked = set()
    if filter_path and filter_path.exists():
        for line in filter_path.read_text(encoding='utf-8').splitlines():
            parts = line.split('\t')
            if len(parts) >= 2 and parts[0] == '1' and parts[1].isdigit():
                blocked.add(int(parts[1]))
    keep = ~np.isin(article_wiki, np.array(sorted(blocked), dtype=np.int64))
    article_wiki = article_wiki[keep]
    dense = np.full(int(max(is_redirect)) + 1, -1, dtype=np.int64)
    dense[article_wiki] = np.arange(len(article_wiki))
    log('articles', len(article_wiki), 'blocked (level 1)', int((~keep).sum()))

    id_to_title = {pid: t for t, pid in title_to_id.items()}
    article_titles = [id_to_title[int(p)] for p in article_wiki]
    redirect_pairs = []           # (転送元タイトル, 記事の dense ID)
    for title, pid in title_to_id.items():
        if is_redirect[pid]:
            target = resolve(title)
            if target >= 0 and dense[target] >= 0:
                redirect_pairs.append((title, int(dense[target])))

    lt_ids, lt_dense = [], []
    for rows in parsed['linktarget']:
        for lt, title in rows:
            target = resolve(title)
            if target >= 0 and dense[target] >= 0:
                lt_ids.append(lt)
                lt_dense.append(dense[target])
    lt_ids = np.array(lt_ids, dtype=np.int64)
    lt_map = np.full(int(lt_ids.max()) + 1, -1, dtype=np.int64)
    lt_map[lt_ids] = np.array(lt_dense, dtype=np.int64)
    log('link targets resolved', len(lt_ids), 'redirects', len(redirect_pairs))

    links = np.concatenate(parsed['pagelinks'])
    parsed['pagelinks'].clear()
    src = links[:, 0].astype(np.int64)
    lt = links[:, 1].astype(np.int64)
    del links
    ok = src < len(dense)
    src, lt = src[ok], lt[ok]
    src = dense[src]
    dst = np.where(lt < len(lt_map), lt_map[np.minimum(lt, len(lt_map) - 1)], -1)
    ok = (src >= 0) & (dst >= 0) & (src != dst)
    log('raw links', len(ok), 'article links (with duplicates)', int(ok.sum()))
    return article_wiki, article_titles, redirect_pairs, src[ok], dst[ok]


# ---------------------------------------------------------------------------
# 4. C++ ツール
# ---------------------------------------------------------------------------

def order_tool(work: Path) -> Path:
    build = work / 'tool-build'
    exe = build / 'Release' / 'linkgraph_order.exe'
    if not exe.exists():
        exe = build / 'linkgraph_order'
    source = TOOL_DIR / 'linkgraph_order.cpp'
    if not exe.exists() or exe.stat().st_mtime < source.stat().st_mtime:
        log('building linkgraph_order')
        subprocess.run(['cmake', '-S', str(TOOL_DIR), '-B', str(build)], check=True,
                       stdout=subprocess.DEVNULL)
        subprocess.run(['cmake', '--build', str(build), '--config', 'Release',
                        '--parallel'], check=True, stdout=subprocess.DEVNULL)
        exe = build / 'Release' / 'linkgraph_order.exe'
        if not exe.exists():
            exe = build / 'linkgraph_order'
    return exe


# ---------------------------------------------------------------------------
# 5. 書き出し
# ---------------------------------------------------------------------------

def front_code(titles: list[bytes], is_redirect: list[bool]) -> bytes:
    out = bytearray()
    prev = b''
    for i, title in enumerate(titles):
        shared = 0
        if i % 64:
            limit = min(len(prev), len(title), 127)
            while shared < limit and prev[shared] == title[shared]:
                shared += 1
        out.append(shared | (0x80 if is_redirect[i] else 0))
        out += title[shared:]
        out.append(0)
        prev = title
    return bytes(out)


def write_file(out_path: Path, date: str, work: Path, article_wiki, article_titles,
               redirect_pairs, threads: int) -> None:
    n = len(article_wiki)
    rank = np.fromfile(work / 'rank.bin', dtype=np.uint32).astype(np.int64)
    indeg = np.fromfile(work / 'indeg.bin', dtype=np.uint32)
    in_len = np.fromfile(work / 'in_len.bin', dtype=np.uint32)
    in_lists = (work / 'in_lists.bin').read_bytes()
    assert len(rank) == n and int(in_len.sum()) == len(in_lists)

    wiki_by_new = np.empty(n, dtype=np.uint32)
    wiki_by_new[rank] = article_wiki
    pairs = [(t.encode('utf-8', 'surrogateescape'), int(rank[i]), False)
             for i, t in enumerate(article_titles)]
    pairs += [(t.encode('utf-8', 'surrogateescape'), int(rank[d]), True) for t, d in redirect_pairs]
    pairs.sort()
    titles = front_code([p[0] for p in pairs], [p[2] for p in pairs])
    ids = np.array([p[1] for p in pairs], dtype=np.uint32)
    title_ids = b''.join(((ids >> (8 * k)) & 0xFF).astype(np.uint8).tobytes() for k in range(3))
    popular_old = np.flatnonzero(indeg >= POPULAR_MIN_INCOMING)
    popular_old = popular_old[np.lexsort((rank[popular_old], -indeg[popular_old].astype(np.int64)))]
    popular = struct.pack('<I', len(popular_old)) + np.stack(
        [rank[popular_old], indeg[popular_old]], axis=1).astype(np.uint32).tobytes()
    meta = json.dumps({
        'format': 'wikigolf-linkgraph', 'version': 2,
        'source': f'jawiki-{date}', 'built_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
        'articles': n, 'links': int(indeg.sum()), 'titles': len(pairs),
        'popular_min_incoming': POPULAR_MIN_INCOMING,
    }, ensure_ascii=False).encode()

    raw = {
        'in_lists': (in_lists, FRAME_BYTES),
        'in_len': (in_len.tobytes(), 0),
        'titles': (titles, TITLE_FRAME_BYTES),
        'title_ids': (title_ids, 0),
        'wiki_ids': (wiki_by_new.tobytes(), 0),
    }
    with ThreadPoolExecutor(threads) as pool:
        compressed = compress_frames({k: v for k, v in raw.items()}, pool)

    sections = [('meta', meta, len(meta), 0, 0)]
    for name in ('in_lists', 'in_len', 'titles', 'title_ids', 'wiki_ids'):
        data, frame = raw[name]
        sections.append((name, compressed[name], len(data), 2, frame))
    sections.append(('popular', popular, len(popular), 0, 0))
    header_size = 32 + 48 * len(sections)
    offset = header_size
    table = b''
    for name, data, raw_size, codec, frame in sections:
        table += struct.pack('<16sQQQII', name.encode(), offset, len(data), raw_size, codec, frame)
        offset += len(data)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = out_path.with_suffix(out_path.suffix + '.tmp')
    with open(tmp, 'wb') as f:
        f.write(struct.pack('<4sIIIQQ', b'WGLG', 2, len(sections), 0, offset, 0))
        f.write(table)
        for _, data, *_ in sections:
            f.write(data)
    tmp.replace(out_path)
    log('written', out_path, f'{offset / 1e6:.1f}MB')
    for name, data, raw_size, *_ in sections:
        log(f'  {name:10s} {len(data) / 1e6:8.2f}MB (raw {raw_size / 1e6:.2f}MB)')


# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    parser.add_argument('--date', default='latest', help='ダンプの日付 YYYYMMDD（既定: 最新の完了済み）')
    parser.add_argument('--work-dir', type=Path,
                        default=Path(os.environ.get('TEMP', '/tmp')) / 'wikigolf-linkgraph',
                        help='ダンプと中間ファイルの置き場所')
    parser.add_argument('--filter', type=Path, default=ROOT / 'Assets/data/content_filter.tsv',
                        help='常時除外（レベル1）の記事を取り除くための除外リスト')
    parser.add_argument('--out', type=Path, help='出力ファイル（既定: 作業フォルダ内）')
    parser.add_argument('--iterations', type=int, default=20, help='二分割の反復回数')
    parser.add_argument('--threads', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--offline', action='store_true', help='ダウンロードせず手元のダンプを使う')
    args = parser.parse_args()

    work: Path = args.work_dir
    work.mkdir(parents=True, exist_ok=True)
    date = args.date
    if date == 'latest':
        if args.offline:
            found = sorted(p.name.split('-')[1] for p in (work / 'dumps').glob('jawiki-*-pagelinks.sql.gz'))
            if not found:
                sys.exit('手元にダンプがありません')
            date = found[-1]
        else:
            date = latest_complete_date()
    log('dump date', date, 'threads', args.threads)
    if not args.offline:
        download(date, work / 'dumps')

    parsed = parse_dumps(date, work / 'dumps', work, args.threads)
    article_wiki, article_titles, redirect_pairs, src, dst = build_graph(parsed, args.filter)
    del parsed

    edges = work / 'edges.bin'
    with open(edges, 'wb') as f:
        f.write(struct.pack('<IQ', len(article_wiki), len(src)))
        f.write(src.astype(np.uint32).tobytes())
        f.write(dst.astype(np.uint32).tobytes())
    del src, dst
    log('edges written')

    exe = order_tool(work)
    subprocess.run([str(exe), str(edges), str(work), str(args.iterations), str(args.threads)],
                   check=True)

    out = args.out or work / f'jawiki-{date}.linkgraph'
    write_file(out, date, work, article_wiki, article_titles, redirect_pairs, args.threads)
    log('done')


if __name__ == '__main__':
    main()

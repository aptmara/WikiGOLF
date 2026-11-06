#!/usr/bin/env python3
"""
build_linkgraph.py が書き出したリンクグラフファイルを検証します。

    python tools/linkgraph/verify_linkgraph.py FILE.linkgraph [--old-db OLD.sqlite] [--pairs 40]

確かめること:
  - ヘッダ・セクション表・各セクションの展開後サイズ
  - 入リンク一覧がすべて昇順・範囲内で、件数が meta・popular と合う
  - タイトル辞書が昇順で、引いた記事 ID が正しい
  - 逆向き BFS で各ゴールから何段で何割の記事に届くか（と所要時間）
  - --old-db を渡すと、旧 SQLite（SDOW 形式）での最短手数と食い違わないか
"""

from __future__ import annotations

import argparse
import json
import random
import sqlite3
import struct
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

from frames import decompress_section


def fail(msg: str) -> None:
    sys.exit(f'NG: {msg}')


class LinkGraph:
    def __init__(self, path: Path):
        data = path.read_bytes()
        magic, version, count, _, length, _ = struct.unpack_from('<4sIIIQQ', data, 0)
        if magic != b'WGLG' or version != 2:
            fail('ヘッダが違う')
        if length != len(data):
            fail(f'ファイル長 {len(data)} がヘッダの {length} と違う')
        self.sections = {}
        for i in range(count):
            name, off, size, raw, codec, frame = struct.unpack_from('<16sQQQII', data, 32 + 48 * i)
            self.sections[name.rstrip(b'\0').decode()] = (off, size, raw, codec, frame)
        self.data = data

    def raw(self, name: str, pool=None) -> bytes:
        off, size, raw, codec, frame = self.sections[name]
        blob = self.data[off:off + size]
        try:
            out = decompress_section(blob, raw, frame, codec, pool)
        except ValueError as e:
            fail(f'{name}: {e}')
        if len(out) != raw:
            fail(f'{name} の展開後サイズが {len(out)}（表では {raw}）')
        return out

    def in_lists(self) -> bytes:
        with ThreadPoolExecutor() as pool:
            return self.raw('in_lists', pool)


def decode_lists(blob: bytes, in_len: np.ndarray):
    """LEB128 の差分列を CSR（indptr, indices）に戻す。"""
    b = np.frombuffer(blob, np.uint8)
    ends = np.flatnonzero(b < 0x80)
    starts = np.r_[0, ends[:-1] + 1]
    length = ends - starts + 1
    val = np.zeros(len(ends), np.int64)
    for k in range(int(length.max())):
        m = length > k
        val[m] |= (b[starts[m] + k].astype(np.int64) & 0x7F) << (7 * k)
    row_end_byte = np.cumsum(in_len.astype(np.int64))
    indptr = np.r_[0, np.searchsorted(ends, row_end_byte - 1, side='right')]
    if indptr[-1] != len(val):
        fail('一覧の区切りが in_len と合わない')
    c = np.cumsum(val + 1)
    row_base = np.r_[0, c][indptr[:-1]]
    counts = np.diff(indptr)
    indices = c - np.repeat(row_base, counts) - 1
    return indptr, indices


def decode_titles(blob: bytes) -> tuple[list[bytes], list[bool]]:
    titles, redirect = [], []
    prev = b''
    i = 0
    n = len(blob)
    while i < n:
        head = blob[i]
        j = blob.index(0, i + 1)
        cur = prev[:head & 0x7F] + blob[i + 1:j]
        titles.append(cur)
        redirect.append(bool(head & 0x80))
        prev = cur
        i = j + 1
    return titles, redirect


def bfs_to(target: int, indptr, indices, n: int) -> np.ndarray:
    """target へ何手で届くか（届かなければ -1）。"""
    dist = np.full(n, -1, np.int16)
    dist[target] = 0
    frontier = np.array([target], np.int64)
    level = 0
    while len(frontier):
        level += 1
        s, e = indptr[frontier], indptr[frontier + 1]
        cnt = e - s
        idx = np.repeat(s - np.r_[0, np.cumsum(cnt)[:-1]], cnt) + np.arange(int(cnt.sum()))
        cand = indices[idx]
        cand = cand[dist[cand] < 0]
        cand = np.unique(cand)
        dist[cand] = level
        frontier = cand
    return dist


def old_hops(db: sqlite3.Connection, src: int, dst: int, limit: int = 12) -> int:
    """旧 SQLite で双方向 BFS をして最短手数を返す（届かなければ -1）。"""
    def neighbors(ids, column):
        out = set()
        ids = list(ids)
        for i in range(0, len(ids), 900):
            chunk = ids[i:i + 900]
            q = f'SELECT {column} FROM links WHERE id IN ({",".join("?" * len(chunk))})'
            for (text,) in db.execute(q, chunk):
                if text:
                    out.update(int(x) for x in text.split('|'))
        return out
    if src == dst:
        return 0
    fwd, bwd = {src}, {dst}
    seen_f, seen_b = {src}, {dst}
    df = db_ = 0
    while fwd and bwd and df + db_ < limit:
        if len(fwd) <= len(bwd):
            nxt = neighbors(fwd, 'outgoing_links') - seen_f
            df += 1
            if nxt & seen_b:
                return df + db_
            seen_f |= nxt
            fwd = nxt
        else:
            nxt = neighbors(bwd, 'incoming_links') - seen_b
            db_ += 1
            if nxt & seen_f:
                return df + db_
            seen_b |= nxt
            bwd = nxt
    return -1


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('file', type=Path)
    p.add_argument('--old-db', type=Path)
    p.add_argument('--pairs', type=int, default=40)
    p.add_argument('--seed', type=int, default=1)
    args = p.parse_args()

    t = time.time()
    g = LinkGraph(args.file)
    meta = json.loads(g.raw('meta'))
    n = meta['articles']
    print('meta', meta)
    in_len = np.frombuffer(g.raw('in_len'), np.uint32)
    wiki_ids = np.frombuffer(g.raw('wiki_ids'), np.uint32)
    if len(in_len) != n or len(wiki_ids) != n:
        fail('記事数が合わない')
    if len(np.unique(wiki_ids)) != n:
        fail('wiki_ids に重複がある')
    t_load = time.time()
    blob = g.in_lists()
    t_unz = time.time()
    indptr, indices = decode_lists(blob, in_len)
    t_dec = time.time()
    print(f'読み込み {t_load - t:.2f}s / in_lists 展開 {t_unz - t_load:.2f}s / 復号 {t_dec - t_unz:.2f}s')

    # 一覧の中身
    if len(indices) != meta['links']:
        fail(f'リンク数 {len(indices)} が meta の {meta["links"]} と違う')
    if indices.min() < 0 or indices.max() >= n:
        fail('範囲外の ID がある')
    row = np.repeat(np.arange(n), np.diff(indptr))
    same_row = row[1:] == row[:-1]
    if (np.diff(indices)[same_row] <= 0).any():
        fail('昇順でない（または重複した）一覧がある')
    if (indices == row).any():
        print('  注意: 自分へのリンクが', int((indices == row).sum()), '件')
    indeg = np.diff(indptr)

    # popular
    pop = g.raw('popular')
    (cnt,) = struct.unpack_from('<I', pop, 0)
    pairs = np.frombuffer(pop, np.uint32, offset=4).reshape(cnt, 2)
    if not (indeg[pairs[:, 0]] == pairs[:, 1]).all():
        fail('popular の被リンク数が一覧と合わない')
    if (np.diff(pairs[:, 1].astype(np.int64)) > 0).any():
        fail('popular が被リンク数の多い順でない')
    if cnt != int((indeg >= meta['popular_min_incoming']).sum()):
        fail('popular の件数が違う')

    # タイトル
    titles, redirect = decode_titles(g.raw('titles'))
    tid_raw = np.frombuffer(g.raw('title_ids'), np.uint8).reshape(3, -1).astype(np.uint32)
    title_ids = tid_raw[0] | tid_raw[1] << 8 | tid_raw[2] << 16
    if len(titles) != meta['titles'] or len(title_ids) != len(titles):
        fail('タイトル数が合わない')
    if any(titles[i] >= titles[i + 1] for i in range(len(titles) - 1)):
        fail('タイトルが昇順でない（または重複している）')
    if title_ids.max() >= n:
        fail('タイトルの記事 ID が範囲外')
    own = title_ids[~np.array(redirect)]
    if len(own) != n or len(np.unique(own)) != n:
        fail(f'記事自身のタイトルが記事ごとに1件ずつになっていない（{len(own)} 件）')
    by_title = {int(title_ids[i]): titles[i] for i in range(len(titles)) if not redirect[i]}
    lookup = {}
    for name in ['日本', '東京都', '1989年', '1月1日', '2024', 'アメリカ合衆国', '日本国']:
        key = name.encode()
        lo, hi = 0, len(titles)
        while lo < hi:
            mid = (lo + hi) // 2
            if titles[mid] < key:
                lo = mid + 1
            else:
                hi = mid
        lookup[name] = int(title_ids[lo]) if lo < len(titles) and titles[lo] == key else None
    print('タイトル', len(titles), '件  例:', {k: (v, int(indeg[v]) if v is not None else None)
                                             for k, v in lookup.items()})
    print(f'検証（構造）OK  {time.time() - t:.1f}s')

    # BFS
    rng = random.Random(args.seed)
    targets = [lookup['日本'], lookup['東京都']] + [int(x) for x in pairs[rng.sample(range(cnt), 3), 0]]
    targets += rng.sample(range(n), 3)
    dists = {}
    for target in targets:
        t0 = time.time()
        d = bfs_to(target, indptr, indices, n)
        el = time.time() - t0
        dists[target] = d
        hist = np.bincount(d[d >= 0])
        reach = (d >= 0).mean() * 100
        print(f'  {by_title[target].decode():20s} 届く {reach:5.1f}%  最大 {len(hist) - 1} 手'
              f'  分布 {hist.tolist()}  ({el:.2f}s numpy)')

    if not args.old_db:
        return
    db = sqlite3.connect(f'file:{args.old_db}?mode=ro', uri=True)
    sources = rng.sample(range(n), args.pairs)
    agree = differ = 0
    for k, s in enumerate(sources):
        target = targets[k % len(targets)]
        ws, wt = int(wiki_ids[s]), int(wiki_ids[target])
        new = int(dists[target][s])
        if db.execute('SELECT 1 FROM links WHERE id=?', (ws,)).fetchone() is None:
            print(f'  {by_title[s].decode()} は旧DBにない（新しい記事）')
            continue
        old = old_hops(db, ws, wt)
        mark = 'OK' if old == new else '差'
        agree += old == new
        differ += old != new
        print(f'  {mark} {by_title[s].decode()} -> {by_title[target].decode()}  新 {new} / 旧 {old}')
    print(f'旧DBとの比較: 一致 {agree} / 不一致 {differ}')


if __name__ == '__main__':
    main()

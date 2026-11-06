"""
リンクグラフのセクションをフレームに分けて圧縮・展開します。

圧縮方式（セクション表の「圧縮」の値）:
    2 = LZMS（Windows 標準の Compression API, cabinet.dll）。以前の版の形式で、読むだけ
    3 = zstd（Python 3.14 標準の compression.zstd）。今はこちらで書く

圧縮したセクションの中身:
    u32 フレーム数, u32 予約, u64 各フレームの圧縮後サイズ[フレーム数], 各フレーム
フレームは独立していて、並列に展開できます。どちらの方式も呼び出し中は GIL が外れるので、
スレッドプールで並列に動きます。ゲーム側の展開は src/game/systems/WikiLinkGraph.cpp。
"""

from __future__ import annotations

import ctypes
import struct
import sys

CODEC_NONE = 0
CODEC_LZMS = 2
CODEC_ZSTD = 3

# zstd の最高圧縮。展開の速さは -19 と変わらない
ZSTD_LEVEL = 22

_COMPRESS_ALGORITHM_LZMS = 5
_cabinet = None


def _lzms():
    global _cabinet
    if _cabinet is not None:
        return _cabinet
    if sys.platform != 'win32':
        raise OSError('LZMS には Windows の Compression API が必要です')
    from ctypes import wintypes
    lib = ctypes.WinDLL('cabinet', use_last_error=True)
    handle = ctypes.c_void_p
    for name in ('CreateCompressor', 'CreateDecompressor'):
        fn = getattr(lib, name)
        fn.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(handle)]
        fn.restype = wintypes.BOOL
    for name in ('Compress', 'Decompress'):
        fn = getattr(lib, name)
        fn.argtypes = [handle, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        fn.restype = wintypes.BOOL
    lib.CloseCompressor.argtypes = [handle]
    lib.CloseDecompressor.argtypes = [handle]
    _cabinet = lib
    return lib


def _lzms_compress(data: bytes) -> bytes:
    lib = _lzms()
    handle = ctypes.c_void_p()
    if not lib.CreateCompressor(_COMPRESS_ALGORITHM_LZMS, None, ctypes.byref(handle)):
        raise OSError(ctypes.get_last_error(), 'CreateCompressor')
    try:
        out = ctypes.create_string_buffer(len(data) + len(data) // 8 + (1 << 16))
        size = ctypes.c_size_t()
        if not lib.Compress(handle, data, len(data), out, len(out), ctypes.byref(size)):
            raise OSError(ctypes.get_last_error(), 'Compress')
        return out.raw[:size.value]
    finally:
        lib.CloseCompressor(handle)


def _lzms_decompress(data: bytes, raw_size: int) -> bytes:
    lib = _lzms()
    handle = ctypes.c_void_p()
    if not lib.CreateDecompressor(_COMPRESS_ALGORITHM_LZMS, None, ctypes.byref(handle)):
        raise OSError(ctypes.get_last_error(), 'CreateDecompressor')
    try:
        out = ctypes.create_string_buffer(max(raw_size, 1))
        size = ctypes.c_size_t()
        if not lib.Decompress(handle, data, len(data), out, raw_size, ctypes.byref(size)):
            raise OSError(ctypes.get_last_error(), 'Decompress')
        if size.value != raw_size:
            raise ValueError(f'展開後のサイズが {size.value}（期待は {raw_size}）')
        return out.raw[:raw_size]
    finally:
        lib.CloseDecompressor(handle)


def _zstd_compress(data: bytes) -> bytes:
    from compression import zstd
    return zstd.compress(data, options={zstd.CompressionParameter.compression_level: ZSTD_LEVEL})


def _zstd_decompress(data: bytes, raw_size: int) -> bytes:
    from compression import zstd
    out = zstd.decompress(data)
    if len(out) != raw_size:
        raise ValueError(f'展開後のサイズが {len(out)}（期待は {raw_size}）')
    return out


_COMPRESS = {CODEC_LZMS: _lzms_compress, CODEC_ZSTD: _zstd_compress}
_DECOMPRESS = {CODEC_LZMS: _lzms_decompress, CODEC_ZSTD: _zstd_decompress}


def split(data: bytes, frame: int) -> list[bytes]:
    if frame <= 0 or len(data) <= frame:
        return [data]
    return [data[i:i + frame] for i in range(0, len(data), frame)]


def compress_frames(sections: dict[str, tuple[bytes, int]], pool,
                    codec: int = CODEC_ZSTD) -> dict[str, bytes]:
    """{名前: (データ, フレームの大きさ)} を圧縮し、{名前: セクションの中身} を返す。"""
    compress = _COMPRESS[codec]
    jobs = {name: [pool.submit(compress, f) for f in split(data, frame)]
            for name, (data, frame) in sections.items()}
    out = {}
    for name, futures in jobs.items():
        frames = [f.result() for f in futures]
        head = struct.pack('<II', len(frames), 0) + struct.pack(f'<{len(frames)}Q',
                                                                *map(len, frames))
        out[name] = head + b''.join(frames)
    return out


def decompress_section(blob: bytes, raw_size: int, frame: int, codec: int,
                       pool=None) -> bytes:
    if codec == CODEC_NONE:
        return blob
    if codec not in _DECOMPRESS:
        raise ValueError(f'圧縮方式 {codec} は扱えません')
    decompress = _DECOMPRESS[codec]
    count, _ = struct.unpack_from('<II', blob, 0)
    sizes = struct.unpack_from(f'<{count}Q', blob, 8)
    pos = 8 + 8 * count
    parts = []
    for i, size in enumerate(sizes):
        expect = raw_size if count == 1 else min(frame, raw_size - i * frame)
        parts.append((blob[pos:pos + size], expect))
        pos += size
    if pos != len(blob):
        raise ValueError('フレームの合計がセクションの大きさと合いません')
    if pool is None:
        return b''.join(decompress(d, n) for d, n in parts)
    return b''.join(pool.map(lambda a: decompress(*a), parts))

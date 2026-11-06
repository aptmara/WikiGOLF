"""
Windows 標準の Compression API（cabinet.dll）の LZMS で、リンクグラフのセクションを
圧縮・展開します。ゲーム側（src/game/systems/WikiLinkGraph.cpp）も同じ API で展開します。

圧縮したセクションの中身:
    u32 フレーム数, u32 予約, u64 各フレームの圧縮後サイズ[フレーム数], 各フレーム
フレームは独立していて、並列に展開できます。ctypes の呼び出し中は GIL が外れるので、
スレッドプールで並列に動きます。
"""

from __future__ import annotations

import ctypes
import struct
import sys
from ctypes import wintypes

COMPRESS_ALGORITHM_LZMS = 5

if sys.platform != 'win32':
    raise ImportError('LZMS の圧縮には Windows の Compression API が必要です')

_cabinet = ctypes.WinDLL('cabinet', use_last_error=True)
_handle = ctypes.c_void_p
_cabinet.CreateCompressor.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(_handle)]
_cabinet.CreateCompressor.restype = wintypes.BOOL
_cabinet.Compress.argtypes = [_handle, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                              ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
_cabinet.Compress.restype = wintypes.BOOL
_cabinet.CloseCompressor.argtypes = [_handle]
_cabinet.CreateDecompressor.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(_handle)]
_cabinet.CreateDecompressor.restype = wintypes.BOOL
_cabinet.Decompress.argtypes = [_handle, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
_cabinet.Decompress.restype = wintypes.BOOL
_cabinet.CloseDecompressor.argtypes = [_handle]


def compress(data: bytes) -> bytes:
    handle = _handle()
    if not _cabinet.CreateCompressor(COMPRESS_ALGORITHM_LZMS, None, ctypes.byref(handle)):
        raise OSError(ctypes.get_last_error(), 'CreateCompressor')
    try:
        out = ctypes.create_string_buffer(len(data) + len(data) // 8 + (1 << 16))
        size = ctypes.c_size_t()
        if not _cabinet.Compress(handle, data, len(data), out, len(out), ctypes.byref(size)):
            raise OSError(ctypes.get_last_error(), 'Compress')
        return out.raw[:size.value]
    finally:
        _cabinet.CloseCompressor(handle)


def decompress(data: bytes, raw_size: int) -> bytes:
    handle = _handle()
    if not _cabinet.CreateDecompressor(COMPRESS_ALGORITHM_LZMS, None, ctypes.byref(handle)):
        raise OSError(ctypes.get_last_error(), 'CreateDecompressor')
    try:
        out = ctypes.create_string_buffer(max(raw_size, 1))
        size = ctypes.c_size_t()
        if not _cabinet.Decompress(handle, data, len(data), out, raw_size, ctypes.byref(size)):
            raise OSError(ctypes.get_last_error(), 'Decompress')
        if size.value != raw_size:
            raise ValueError(f'展開後のサイズが {size.value}（期待は {raw_size}）')
        return out.raw[:raw_size]
    finally:
        _cabinet.CloseDecompressor(handle)


def split(data: bytes, frame: int) -> list[bytes]:
    if frame <= 0 or len(data) <= frame:
        return [data]
    return [data[i:i + frame] for i in range(0, len(data), frame)]


def compress_frames(sections: dict[str, tuple[bytes, int]], pool) -> dict[str, bytes]:
    """{名前: (データ, フレームの大きさ)} を圧縮し、{名前: セクションの中身} を返す。"""
    jobs = {name: [pool.submit(compress, f) for f in split(data, frame)]
            for name, (data, frame) in sections.items()}
    out = {}
    for name, futures in jobs.items():
        frames = [f.result() for f in futures]
        head = struct.pack('<II', len(frames), 0) + struct.pack(f'<{len(frames)}Q',
                                                                *map(len, frames))
        out[name] = head + b''.join(frames)
    return out


def decompress_section(blob: bytes, raw_size: int, frame: int, pool=None) -> bytes:
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

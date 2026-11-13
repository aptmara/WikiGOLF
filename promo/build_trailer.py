# -*- coding: utf-8 -*-
"""WikiGOLF 紹介動画の組み立て。録画の音は使わず、Assets/sounds から貼り直す。"""
import subprocess, sys, os, json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

REPO = Path(__file__).resolve().parent.parent
SRC = Path(r"C:\Users\aptma\Videos\画面録画")
WORK = Path(__file__).parent / "work"
OUT = REPO / "promo"
WORK.mkdir(exist_ok=True); OUT.mkdir(exist_ok=True)
SND = REPO / "Assets" / "sounds"
FONT = str(REPO / "Assets" / "Fonts" / "Kiwi_Maru" / "KiwiMaru-Medium.ttf")
W, H, FPS = 1920, 1080, 30

FILES = {k: SRC / f"画面録画 2026-11-13 {v}.mp4" for k, v in
         dict(A="173455", B="110344", C="172604", D="174030", E="172221", F="173145").items()}
# ウィンドウ枠とタイトルバーを外す切り出し(元の解像度ごと)
CROP = {"B": "crop=2552:1436:6:48"}
DEFAULT_CROP = "crop=2456:1382:7:48"

def run(cmd):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print(r.stderr[-3000:]); sys.exit(1)
    return r

# (名前, 素材, 開始秒, 長さ, 種類)
SEGS = [
    ("s1",   "A", 30.6,  2.7, ""),
    ("s2",   "A", 33.3,  3.3, ""),
    ("s4a",  "B", 54.5,  2.6, ""),
    ("s3",   "B", 58.6,  3.3, ""),
    ("s4b",  "B", 62.85, 4.7, ""),
    ("s5",   "B", 68.5,  2.0, ""),
    ("s6",   "C", 15.4,  5.0, ""),
    ("s7",   "D", 3.6,   6.4, ""),
    ("s9a",  "A", 53.0,  1.0, "left96"),
    ("s9b",  "D", 41.6,  1.0, ""),
    ("s9c",  "D", 193.0, 1.0, ""),
    ("s9d",  "E", 158.0, 1.0, ""),
    ("s10a", "C", 43.2,  2.2, ""),
    ("s10b", "A", 100.8, 2.4, ""),
    ("s10c", "F", 118.0, 2.4, ""),
    ("s11",  "C", 50.0,  4.3, ""),
    ("s12",  "A", 76.6,  5.6, ""),
    ("s13",  "C", 9.85,  3.0, "still"),
    ("s14",  "D", 232.85, 3.6, ""),
    ("s15",  "B", 242.2, 4.2, ""),
    ("end",  "B", 269.2, 6.2, "end"),
]
ENC = ["-c:v", "libx264", "-preset", "fast", "-crf", "12", "-pix_fmt", "yuv420p", "-an"]

def make_mask():
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle((480, 30, 1440, 288), 30, fill=255)
    d.rectangle((598, 270, 1312, 345), fill=255)
    d.rounded_rectangle((655, 330, 1265, 850), 30, fill=255)
    m = m.filter(ImageFilter.GaussianBlur(12))
    p = WORK / "end_mask.png"; m.save(p); return p

def font(size): return ImageFont.truetype(FONT, size)

def make_caption(text, cy, name, size=60, cx=W // 2):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    f = font(size)
    d = ImageDraw.Draw(im)
    l, t, r, b = d.textbbox((0, 0), text, font=f)
    tw, th = r - l, b - t
    padx, pady = 46, 24
    x0, x1 = cx - tw // 2 - padx, cx + tw // 2 + padx
    y0, y1 = cy - th // 2 - pady, cy + th // 2 + pady
    # 影
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((x0, y0 + 8, x1, y1 + 8), 30, fill=(0, 0, 0, 150))
    im = Image.alpha_composite(im, sh.filter(ImageFilter.GaussianBlur(12)))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((x0, y0, x1, y1), 30, fill=(13, 22, 48, 225), outline=(236, 205, 110, 255), width=3)
    d.text((cx - tw // 2 - l, cy - th // 2 - t), text, font=f, fill=(255, 255, 255, 255))
    p = WORK / f"{name}.png"; im.save(p); return p

def make_endtext():
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # 下側を暗くする帯
    g = Image.new("L", (1, H), 0)
    for y in range(H):
        a = 0 if y < 800 else min(235, int((y - 800) / 110 * 235))
        g.putpixel((0, y), a)
    band = Image.new("RGBA", (W, H), (8, 12, 28, 255)); band.putalpha(g.resize((W, H)))
    im = Image.alpha_composite(im, band)
    d = ImageDraw.Draw(im)
    def center(text, y, size, fill, stroke=0):
        f = font(size); l, t, r, b = d.textbbox((0, 0), text, font=f)
        d.text(((W - (r - l)) // 2 - l, y), text, font=f, fill=fill, stroke_width=stroke, stroke_fill=(8, 12, 28, 255))
    center("ぜひ、プレイしてみてください。", 880, 58, (255, 232, 150, 255), 3)
    center("記事本文: Wikipedia(CC BY-SA 4.0)。レイアウト変更や一部省略をしています。", 972, 24, (225, 228, 238, 255))
    center("WikiGOLFは個人が制作した非公式のゲームで、Wikimedia財団とは関係ありません。", 1008, 24, (225, 228, 238, 255))
    center("効果音・BGM: OtoLogic(CC BY 4.0)、効果音ラボ", 1044, 22, (190, 196, 212, 255))
    p = WORK / "endtext.png"; im.save(p); return p

def render_segments():
    mask = make_mask(); endtext = make_endtext()
    outs = []
    for name, key, ss, dur, kind in SEGS:
        out = WORK / f"{name}.mp4"; outs.append(out)
        crop = CROP.get(key, DEFAULT_CROP)
        base = f"{crop},scale={W}:{H}:flags=lanczos,setsar=1"
        if kind == "still":
            png = WORK / f"{name}.png"
            run(["ffmpeg", "-y", "-v", "error", "-ss", ss, "-i", FILES[key], "-frames:v", "1", "-vf", base, png])
            n = int(dur * FPS)
            vf = (f"scale={W*2}:{H*2}:flags=lanczos,zoompan=z='1+0.05*on/{n}':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)'"
                  f":d={n}:s={W}x{H}:fps={FPS},format=yuv420p")
            run(["ffmpeg", "-y", "-v", "error", "-i", png, "-vf", vf, "-frames:v", n, *ENC, out])
        elif kind == "end":
            fc = (f"[0:v]{base},fps={FPS}[c];"
                  f"[2:v]format=rgba,fade=t=in:st=0.5:d=0.5:alpha=1[t];"
                  f"[c][t]overlay,fade=t=in:st=0:d=0.35,fade=t=out:st={dur-0.6}:d=0.6,format=yuv420p[v]")
            run(["ffmpeg", "-y", "-v", "error", "-ss", ss, "-t", dur, "-i", FILES[key],
                 "-loop", "1", "-t", dur, "-i", mask, "-loop", "1", "-framerate", FPS, "-t", dur, "-i", endtext,
                 "-filter_complex", fc, "-map", "[v]", "-t", dur, *ENC, out])
        else:
            vf = base
            if kind == "left96":
                vf = f"{crop},crop=iw*0.955:ih*0.955:0:ih*0.02,scale={W}:{H}:flags=lanczos,setsar=1"
            run(["ffmpeg", "-y", "-v", "error", "-ss", ss, "-t", dur, "-i", FILES[key],
                 "-vf", vf + f",fps={FPS},format=yuv420p", "-frames:v", int(round(dur * FPS)), *ENC, out])
        print("seg", name)
    return outs

def main():
    outs = render_segments()
    # 各セグメントの開始時刻
    start, t = {}, 0.0
    for (name, key, ss, dur, kind) in SEGS:
        start[name] = (t, ss, dur); t += round(dur * FPS) / FPS
    total = t
    def at(seg, src_t):  # 素材内の時刻 → 動画内の時刻
        s, ss, _ = start[seg]; return s + (src_t - ss)
    def seg_t(seg, off=0.0): return start[seg][0] + off
    def seg_end(seg, off=0.0): return start[seg][0] + start[seg][2] + off

    lst = WORK / "concat.txt"
    lst.write_text("".join(f"file '{p.as_posix()}'\n" for p in outs), encoding="utf-8")
    silent = WORK / "silent.mp4"
    run(["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", silent])

    # テロップ: (文, 中心y, 開始, 終了)
    CAPS = [
        ("Wikipediaの記事が、",       520, seg_t("s1", 0.35), seg_end("s1", -0.05)),
        ("ゴルフコースになる。",       800, seg_t("s2", 0.45), seg_end("s2", -0.1)),
        ("本文のリンクは、カップ。",   800, seg_t("s3", 0.3),  seg_end("s3", -0.1)),
        ("入ると、次の記事へ。",       800, seg_t("s4b", 2.3), seg_end("s5", -0.1)),
        ("目指すのは、ゴールの記事。", (520, 780), seg_t("s6", 0.4),  seg_t("s6", 3.1)),
        ("風と地形を読んで、打つ。",   690, seg_t("s7", 0.3),  seg_t("s7", 3.2)),
        ("記事ごとに、違うコース。",   800, seg_t("s10a", 0.3), seg_end("s10c", -0.3)),
        ("お題は、好きな記事で。",      64, seg_t("s13", 0.3), seg_end("s13", -0.1)),
        ("最短ルートと、答え合わせ。", 845, seg_t("s15", 0.4), seg_end("s15", -0.15)),
    ]
    # 効果音: (ファイル, 時刻, 音量, 切る長さ or None)
    SE = [
        ("se_shot.mp3",          at("s4a", 54.98), 0.8, None),
        ("se_judge_perfect.mp3", at("s4a", 55.05), 0.75, None),
        ("se_cupin.mp3",         at("s4b", 62.9),  0.9, None),
        ("se_shot_charge.mp3",   at("s7", 4.0),    0.45, None),
        ("se_shot_hard.mp3",     at("s7", 5.9),    0.9, None),
        ("se_judge_perfect.mp3", at("s7", 5.97),   0.75, None),
        ("se_Bunker.mp3",        seg_t("s9a"),     0.8, 1.0),
        ("se_Rough.mp3",         seg_t("s9b"),     0.8, 1.0),
        ("se_Fairway.mp3",       seg_t("s9c"),     0.8, 1.0),
        ("se_Green.mp3",         seg_t("s9d"),     0.8, 1.4),
        ("se_shot.mp3",          at("s11", 52.1),  0.8, None),
        ("se_judge_great.mp3",   at("s11", 52.2),  0.75, None),
        ("se_shot_soft.mp3",     at("s12", 77.45), 0.75, None),
        ("se_judge_great.mp3",   at("s12", 77.52), 0.7, None),
        ("se_cupin.mp3",         at("s12", 78.6),  0.9, None),
        ("se_shot_hard.mp3",     seg_t("s13", 0.25), 0.4, None),
        ("se_goal.mp3",          seg_t("s14", 0.05), 0.8, 5.0),
    ]
    (WORK / "timeline.json").write_text(json.dumps(
        dict(total=total, start={k: v[0] for k, v in start.items()},
             caps=[(c[0], round(c[2], 2), round(c[3], 2)) for c in CAPS],
             se=[(s[0], round(s[1], 2)) for s in SE]), ensure_ascii=False, indent=1), encoding="utf-8")

    cmd = ["ffmpeg", "-y", "-v", "error", "-i", silent]
    fc, last = [], "0:v"
    for i, (text, cy, a, b) in enumerate(CAPS):
        png = make_caption(text, cy[0], f"cap{i}", cx=cy[1]) if isinstance(cy, tuple) else make_caption(text, cy, f"cap{i}")
        cmd += ["-loop", "1", "-framerate", FPS, "-t", f"{total:.3f}", "-i", png]
        fc.append(f"[{i+1}:v]format=rgba,fade=t=in:st={a:.3f}:d=0.28:alpha=1,fade=t=out:st={b-0.22:.3f}:d=0.22:alpha=1[c{i}]")
        fc.append(f"[{last}][c{i}]overlay=x=0:y='if(lt(t-{a:.3f},0.28),18*pow(1-(t-{a:.3f})/0.28,2),0)'"
                  f":enable='between(t,{a:.3f},{b:.3f})'[v{i}]")
        last = f"v{i}"
    n_in = 1 + len(CAPS)
    cmd += ["-ss", "0.14", "-i", SND / "bgm_game.mp3"]
    fc.append(f"[{n_in}:a]aresample=48000,volume=0.5,afade=t=in:st=0:d=0.4,"
              f"afade=t=out:st={total-2.6:.3f}:d=2.6,atrim=0:{total:.3f}[bgm]")
    mix = ["[bgm]"]
    for j, (fn, tt, vol, cut) in enumerate(SE):
        cmd += ["-i", SND / fn]
        skip = {"se_Bunker.mp3": 0.38, "se_Green.mp3": 0.62, "se_cupin.mp3": 0.07, "se_goal.mp3": 0.1}.get(fn, 0.0)
        trim = f"atrim=start={skip}" + (f":end={skip+cut}" if cut else "") + ",asetpts=PTS-STARTPTS,"
        if cut: trim += f"afade=t=out:st={cut-0.25:.2f}:d=0.25,"
        ms = int(round(tt * 1000))
        fc.append(f"[{n_in+1+j}:a]aresample=48000,aformat=channel_layouts=stereo,{trim}volume={vol},adelay={ms}|{ms}[s{j}]")
        mix.append(f"[s{j}]")
    fc.append("".join(mix) + f"amix=inputs={len(mix)}:normalize=0:duration=longest,"
              f"atrim=0:{total:.3f},loudnorm=I=-15:TP=-1.5:LRA=11,aresample=48000[a]")
    final = OUT / "wikigolf_trailer_v1.mp4"
    cmd += ["-filter_complex", ";".join(fc), "-map", f"[{last}]", "-map", "[a]",
            "-c:v", "libx264", "-preset", "slow", "-crf", "15", "-maxrate", "30M", "-bufsize", "60M",
            "-pix_fmt", "yuv420p", "-r", FPS, "-c:a", "aac", "-b:a", "192k", "-ar", "48000",
            "-dn", "-map_metadata", "-1", "-map_chapters", "-1", "-movflags", "+faststart", "-t", f"{total:.3f}", final]
    run(cmd)
    # サムネ画像があれば、カバー画像として埋め込んだ完成版も作る
    thumb = OUT / "wikigolf_thumbnail.png"
    if thumb.exists():
        run(["ffmpeg", "-y", "-v", "error", "-i", final, "-i", thumb, "-map", "0:v:0", "-map", "0:a:0", "-map", "1:v:0",
             "-c", "copy", "-c:v:1", "png", "-disposition:v:1", "attached_pic", "-movflags", "+faststart",
             OUT / "wikigolf_trailer_final.mp4"])
    print("done", final, f"{total:.2f}s")

if __name__ == "__main__":
    main()

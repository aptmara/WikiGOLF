#!/usr/bin/env python3
"""Doxygen XML から Mermaid クラス図を生成する。

使い方 (リポジトリのルートで実行):
    python docs/architecture/generate_class_full.py

出力 (docs/architecture/):
    class-overview.mmd   全型の名前のみ + 構造の線。全体を見渡す図
    class-full.mmd       全型の全メンバー + 構造の線
    class-ecs-usage.mmd  どのクラスがどの ECS コンポーネントを扱うか
    class-modules/*.mmd  モジュール別の全メンバー + 多重度 / メンバー名付きの線

対象: DX_GAME.cpp / DX_GAME.h / src (src/thirdparty とテストは除外)
条件付きコード: デバッグツール / プロファイラ / HTML コース / DLSS を有効として解析する。
"""
import argparse
import collections
import math
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OVERLAY = """@INCLUDE = Doxyfile
OUTPUT_DIRECTORY = {out}
GENERATE_HTML = NO
GENERATE_LATEX = NO
GENERATE_XML = YES
XML_PROGRAMLISTING = NO
SOURCE_BROWSER = NO
REFERENCED_BY_RELATION = NO
REFERENCES_RELATION = NO
INPUT = DX_GAME.cpp DX_GAME.h src
WARN_LOGFILE = {out}/warnings.log
QUIET = YES
EXTRACT_ANON_NSPACES = YES
PREDEFINED = NOMINMAX WIN32_LEAN_AND_MEAN WIKIGOLF_DEBUG_TOOLS=1 WIKIGOLF_PROFILING=1 WIKIGOLF_HTML_COURSES=1 WIKIGOLF_DLSS=1
EXCLUDE_PATTERNS = */thirdparty/* */test_*
"""

# (ファイル名, 名前空間の接頭辞, 塗り, 枠)
MODULES = [
    ("core", ["core"], "#fef3c7", "#d97706"),
    ("ecs", ["ecs"], "#ffedd5", "#ea580c"),
    ("audio-resources", ["audio", "resources"], "#fce7f3", "#db2777"),
    ("graphics", ["graphics"], "#dcfce7", "#16a34a"),
    ("game-components", ["game::components"], "#dbeafe", "#2563eb"),
    ("game-controllers", ["game::controllers"], "#e0e7ff", "#4f46e5"),
    ("game-debug", ["game::debug"], "#f3f4f6", "#6b7280"),
    ("game-scenes", ["game::scenes"], "#fee2e2", "#dc2626"),
    ("game-systems", ["game::systems"], "#ccfbf1", "#0d9488"),
    ("game-utils-physics", ["game"], "#ede9fe", "#7c3aed"),
]

VIS = {"public": "+", "protected": "#", "private": "-", "package": "~"}
VIS_ORDER = {"public": 0, "protected": 1, "package": 2, "private": 3}
TOKEN = re.compile(r"[A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*")
CONTAINERS = re.compile(
    r"\b(vector|array|list|deque|map|unordered_map|set|unordered_set|multimap|queue|stack|priority_queue|span)\s*<")
BODY_CREATE = re.compile(r"\b(?:make_unique|make_shared)\s*<\s*([\w:]+)|\bnew\s+([A-Za-z_][\w:]*)")
BODY_ECS = re.compile(r"\b(?:Add|Get|Has|Remove|Query|GetGlobal|SetGlobal)\s*<\s*([^<>()]+?)\s*>")
ENTITY = {"<": "#lt;", ">": "#gt;", "&": "#amp;", "*": "#42;", "~": "#126;", "(": "#40;", ")": "#41;",
          "{": "#123;", "}": "#125;", '"': "#quot;", "$": "#36;", "%": "#37;"}
PREFIXES = re.compile(
    r"\b(?:std|Microsoft::WRL|DirectX|game::(?:components|controllers::hud|controllers|systems|scenes|utils|physics|debug)"
    r"|game|graphics::html|graphics|core|ecs|resources|audio)::")
MAX_SIGNATURE = 90
ISOLATED_PREFIX = "iso__"


def short_type(text):
    """型の表記から名前空間を省き、空白を詰める。"""
    text = re.sub(r"\s+", " ", text or "").strip()
    text = PREFIXES.sub("", text)
    text = re.sub(r"\s*([<>*&,])\s*", lambda m: ", " if m.group(1) == "," else m.group(1), text)
    return re.sub(r"\bconst\s+const\b", "const", text).strip()


def esc(text):
    text = re.sub(r"\s+", " ", text or "").strip()
    return "".join(ENTITY.get(c, c) for c in text)


def text_of(el):
    return "".join(el.itertext()).strip() if el is not None else ""


def mid(name):
    return re.sub(r"\W", "_", name)


def display_name(name):
    return re.sub(r"anonymous_namespace\{[^}]*\}::", "", name)


class Entity:
    def __init__(self, ident, name, kind):
        self.id, self.name, self.kind = ident, name, kind
        self.template = ""
        self.bases = []
        self.attrs, self.methods, self.values = [], [], []
        self.abstract = False


def load(xml_dir):
    entities, by_id, members, locations, aliases = {}, {}, [], {}, {}
    for path in sorted(xml_dir.glob("*.xml")):
        if path.name in ("index.xml", "Doxyfile.xml"):
            continue
        cdef = ET.parse(path).getroot().find("compounddef")
        if cdef is None:
            continue
        kind = cdef.get("kind")
        owner = None
        if kind in ("class", "struct", "union"):
            name = cdef.findtext("compoundname")
            if "@" in name:
                continue
            loc = cdef.find("location")
            where = (loc.get("file"), loc.get("line"), name.rsplit("::", 1)[-1]) if loc is not None else None
            if where and where in locations:
                # using 宣言を Doxygen が別クラスとして記録したもの: 定義ファイルのパスに近い名前を実体とする
                other = locations[where]
                folder = where[0].rsplit("/", 1)[0].replace("/", "::")
                if name.startswith(folder + "::") and not other.name.startswith(folder + "::"):
                    aliases[other.name] = name
                    del entities[other.name]
                    other.name = name
                    entities[name] = other
                else:
                    aliases[name] = other.name
                by_id[cdef.get("id")] = other
                continue
            owner = Entity(cdef.get("id"), name, kind)
            if where:
                locations[where] = owner
            owner.template = ", ".join(text_of(p.find("declname")) or text_of(p.find("type")).split()[-1]
                                       for p in cdef.findall("templateparamlist/param"))
            owner.bases = [(b.get("refid"), b.text) for b in cdef.findall("basecompoundref")]
            entities[name] = owner
            by_id[owner.id] = owner
        elif kind not in ("namespace", "file"):
            continue
        for sec in cdef.findall("sectiondef"):
            for md in sec.findall("memberdef"):
                if md.get("kind") == "enum":
                    add_enum(md, cdef, kind, entities, by_id)
                elif owner is not None and md.get("kind") in ("variable", "function"):
                    members.append((owner, md))
    return entities, by_id, members, aliases


def add_enum(md, cdef, kind, entities, by_id):
    if md.get("id") in by_id:
        return
    short = md.findtext("name") or ""
    if not short or "@" in short:
        return
    scope = cdef.findtext("compoundname") if kind != "file" else ""
    name = f"{scope}::{short}" if scope else short
    ent = Entity(md.get("id"), name, "enum")
    ent.values = [v.findtext("name") for v in md.findall("enumvalue")]
    entities[name] = ent
    by_id[ent.id] = ent


class Resolver:
    def __init__(self, entities, aliases):
        self.entities = entities
        self.aliases = aliases
        self.suffix = collections.defaultdict(list)
        for name in entities:
            parts = name.split("::")
            for i in range(len(parts)):
                self.suffix["::".join(parts[i:])].append(name)
        self.ambiguous = collections.Counter()

    def __call__(self, token, scope):
        token = re.sub(r"\s+", "", token)
        parts = scope.split("::")
        for i in range(len(parts), -1, -1):
            cand = "::".join(parts[:i] + [token])
            cand = self.aliases.get(cand, cand)
            if cand in self.entities:
                return self.entities[cand]
        hits = self.suffix.get(token, [])
        if len(hits) == 1:
            return self.entities[hits[0]]
        if len(hits) > 1:
            self.ambiguous[token] += 1
        return None


def refs_in(type_el, scope, by_id, resolve):
    """型の文字列中で参照しているエンティティを (entity, 出現位置, 全文) で返す。"""
    text = text_of(type_el)
    ref_map = {}
    if type_el is not None:
        for r in type_el.findall("ref"):
            if r.get("refid") in by_id and r.text:
                ref_map[re.sub(r"\s+", "", r.text)] = by_id[r.get("refid")]
    out = []
    for m in TOKEN.finditer(text):
        tok = re.sub(r"\s+", "", m.group())
        if tok.startswith("std::") or tok in ("const", "struct", "class", "unsigned", "static", "mutable"):
            continue
        ent = ref_map.get(tok) or resolve(tok, scope)
        if ent is not None:
            out.append((ent, m.end(), text))
    return out


def classify(end, text, array_suffix):
    """(関係種別, 多重度) を返す。"""
    before, after = text[:end], text[end:].lstrip()
    if re.search(r"\bfunction\s*<", before):
        return "dep", None
    if after[:1] in ("*", "&") or re.search(r"\b(shared_ptr|weak_ptr|ComPtr)\s*<", before):
        kind = "agg"
    elif re.match(r">\s*[*&]", after) and "unique_ptr" not in before:
        kind = "agg"
    else:
        kind = "comp"
    if CONTAINERS.search(before) or array_suffix:
        return kind, "0..*"
    if kind == "agg" or re.search(r"\b(optional|unique_ptr)\s*<", before):
        return kind, "0..1"
    return kind, "1"


class Sources:
    def __init__(self):
        self.cache = {}

    def body(self, loc):
        if loc is None or not loc.get("bodyfile") or int(loc.get("bodyend", -1)) < 0:
            return ""
        rel = loc.get("bodyfile")
        for base in (Path(rel), ROOT / rel, ROOT / "src" / rel):
            if base.is_file():
                lines = self.cache.setdefault(rel, base.read_text("utf-8", "ignore").splitlines())
                code = "\n".join(lines[int(loc.get("bodystart")) - 1:int(loc.get("bodyend"))])
                code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
                return re.sub(r"//[^\n]*", "", code)
        return ""


def signature(md):
    """1 行が長すぎるとボックス全体の幅が広がるので、長い場合は引数名を省く。"""
    name = md.findtext("name") or ""
    params = [(short_type(text_of(p.find("type"))), p.findtext("declname") or "", p.findtext("array") or "")
              for p in md.findall("param")]
    full = ", ".join(f"{t} {n}{a}".strip() for t, n, a in params)
    if len(name) + len(full) > MAX_SIGNATURE:
        full = ", ".join(f"{t}{a}" for t, _, a in params)
    return name, full


def analyse(entities, by_id, members, aliases):
    resolve = Resolver(entities, aliases)
    sources = Sources()
    solid = collections.defaultdict(lambda: {"names": [], "mult": []})
    created, ecs_use = set(), set()

    for owner, md in members:
        scope = owner.name
        prot = md.get("prot")
        name = md.findtext("name") or ""
        loc = md.find("location")
        line = int(loc.get("line", 0)) if loc is not None else 0
        order = (VIS_ORDER.get(prot, 9), line)
        type_el = md.find("type")
        type_text = text_of(type_el)
        if md.get("kind") == "variable":
            if "@" in type_text or "@" in name:
                continue
            args = md.findtext("argsstring") or ""
            static = "$" if md.get("static") == "yes" else ""
            owner.attrs.append((order, f"{VIS.get(prot, '')}{esc(short_type(type_text))} {esc(name)}{esc(args)}{static}"))
            for ent, end, text in refs_in(type_el, scope, by_id, resolve):
                if ent is owner and "*" not in text and "<" not in text:
                    continue
                kind, mult = classify(end, text, args.startswith("["))
                if kind != "dep" and ent.kind != "enum":
                    edge = solid[(owner.name, ent.name, kind)]
                    edge["names"].append(name)
                    edge["mult"].append(mult)
        else:
            fname, params = signature(md)
            pure = md.get("virt") == "pure-virtual"
            owner.abstract = owner.abstract or pure
            prefix = "virtual " if md.get("virt") == "virtual" else ""
            suffix = "*" if pure else ("$" if md.get("static") == "yes" else "")
            # Mermaid は ")" 以降を戻り値として描くため、const メソッドは戻り値の後ろに制約として書く
            const = " #123;const#125;" if md.get("const") == "yes" else ""
            sig = f"{prefix}{esc(fname)}({esc(params)}){suffix} {esc(short_type(type_text))}{const}".rstrip()
            owner.methods.append((order, f"{VIS.get(prot, '')}{sig}"))
            body = sources.body(loc)
            for m in BODY_CREATE.finditer(body):
                ent = resolve(m.group(1) or m.group(2), scope)
                if ent is not None and ent is not owner and ent.kind != "enum":
                    created.add((owner.name, ent.name))
            for m in BODY_ECS.finditer(body):
                for tok in m.group(1).split(","):
                    tok = re.sub(r"\b(const|struct|class)\b|[*&]", "", tok).strip()
                    ent = resolve(tok, scope) if tok else None
                    if ent is not None and ent is not owner and ent.kind != "enum":
                        ecs_use.add((owner.name, ent.name))
    return solid, created, ecs_use, resolve


def group_of(ent, entities):
    nss, cur = [], []
    for part in ent.name.split("::")[:-1]:
        cur.append(part)
        if "::".join(cur) in entities:
            break
        if not part.startswith("anonymous_namespace"):
            nss.append(part)
    return "::".join(nss) or "(global)"


def module_of(name):
    for module, prefixes, _, _ in MODULES:
        if any(name == p or name.startswith(p + "::") for p in prefixes):
            return module
    return None


def label_of(ent, ns):
    display = display_name(ent.name)
    short = display[len(ns) + 2:] if ns != "(global)" else display
    if ent.template:
        short += f"<{ent.template}>"
    return esc(short)


def box_lines(ent):
    tags = {"enum": "enumeration", "struct": "struct", "union": "union"}
    tag = "abstract" if ent.abstract else tags.get(ent.kind)
    out = [f"<<{tag}>>"] if tag else []
    out += [esc(v) for v in ent.values]
    out += [m for _, m in sorted(ent.attrs, key=lambda x: x[0])]
    out += [m for _, m in sorted(ent.methods, key=lambda x: x[0])]
    return out


def estimate_size(ent, with_members):
    """等幅に近い概算のボックス寸法 (孤立クラスの格子配置に使う)。"""
    if not with_members:
        return max(90, len(display_name(ent.name).rsplit("::", 1)[-1]) * 8 + 30), 60
    lines = box_lines(ent)
    plain = [re.sub(r"#\w+;", "x", ln) for ln in lines] or [""]
    return max(max(len(x) for x in plain) * 7 + 30, 120), len(lines) * 24 + 60


def grid_columns(names, entities, with_members):
    """幅の近いクラスを同じ列に集め、全体が正方形に近くなるよう列へ割り当てる。"""
    sized = sorted(((estimate_size(entities[n], with_members), n) for n in names), key=lambda x: -x[0][0])
    side = math.sqrt(sum(w * h for (w, h), _ in sized)) * 1.1
    columns, height = [[]], 0
    for (_, h), n in sized:
        if columns[-1] and height + h > side:
            columns.append([])
            height = 0
        columns[-1].append(n)
        height += h + 16
    return columns


FRONT_MATTER = """---
config:
  layout: elk
  elk:
    mergeEdges: true
    nodePlacementStrategy: NETWORK_SIMPLEX
    cycleBreakingStrategy: GREEDY
  theme: base
  themeVariables:
    clusterBkg: "#fafafa"
    clusterBorder: "#9ca3af"
    lineColor: "#374151"
    primaryTextColor: "#111827"
    edgeLabelBackground: "#ffffff"
  class:
    padding: 6
    hideEmptyMembersBox: true
  themeCSS: |
    path[id*="id_iso__"] { display: none; }
"""


def front_matter():
    """クラス図では classDef が CSS に反映されないため、配色は themeCSS に直接書く。"""
    lines = [FRONT_MATTER.rstrip()]
    colors = [(mid(module), fill, stroke) for module, _, fill, stroke in MODULES]
    colors.append(("refnode", "#ffffff", "#9ca3af"))
    for cls, fill, stroke in colors:
        lines.append(f"    .node.{cls} .outer-path path:nth-child(1) {{ fill: {fill} !important; }}")
        lines.append(f"    .node.{cls} .outer-path path:nth-child(2) {{ stroke: {stroke} !important; }}")
        lines.append(f"    .node.{cls} .divider path {{ stroke: {stroke} !important; }}")
    lines.append("---")
    return "\n".join(lines)


def emit(entities, by_id, out, title, edge_rows, focus=None, with_members=True, only_edge_nodes=False):
    """edge_rows: (a, 記法, 多重度付き右辺の前置, b, ラベル) のリスト。a / b は型名または外部クラス名。"""
    kept = [e for e in edge_rows if focus is None or e[0] in focus or e[3] in focus]
    touched = {n for e in kept for n in (e[0], e[3])}
    if only_edge_nodes:
        shown = {n for n in entities if n in touched}
    else:
        shown = set(focus) if focus is not None else set(entities)
    stubs = sorted(touched - shown)
    isolated = {n for n in shown if n not in touched}

    def nid(name):
        return (ISOLATED_PREFIX if name in isolated else "") + mid(name)

    lines = [
        front_matter(),
        f"%% {title} (generate_class_full.py による自動生成。手で編集しないこと)",
        "%% 根拠: Doxygen XML (src + DX_GAME。テストと src/thirdparty は除外)",
        "%% <|-- 継承 / *-- 所有 (値, unique_ptr, コンテナ) / o-- 非所有 (生ポインタ, 参照, shared_ptr, weak_ptr)",
        "%% .. ネスト型 / ..> 生成 (make_unique, make_shared, new) または ECS 利用",
        "classDiagram",
        "    direction LR",
    ]
    groups = collections.defaultdict(list)
    for name in shown:
        groups[group_of(entities[name], entities)].append(entities[name])

    layout_edges = []
    for ns in sorted(groups):
        lines.append(f"    namespace {ns.replace('::', '.') if ns != '(global)' else 'global_scope'} {{")
        for ent in sorted(groups[ns], key=lambda e: e.name):
            body = box_lines(ent) if with_members else []
            if body:
                lines.append(f'        class {nid(ent.name)}["{label_of(ent, ns)}"] {{')
                lines += [f"            {b}" for b in body]
                lines.append("        }")
            else:
                lines.append(f'        class {nid(ent.name)}["{label_of(ent, ns)}"]')
        lines.append("    }")
        lonely = [e.name for e in groups[ns] if e.name in isolated]
        if len(lonely) > 1:
            cols = grid_columns(lonely, entities, with_members)
            for left, right in zip(cols, cols[1:]):
                for i, n in enumerate(right):
                    layout_edges.append(f"    {nid(left[i % len(left)])} .. {nid(n)}")

    if stubs:
        lines.append("    %% 他モジュール / 外部の参照ノード")
        for name in stubs:
            lines.append(f'    class {nid(name)}["{esc(display_name(name))}"]')

    lines.append("    %% 関係")
    for a, arrow, mult, b, label in kept:
        suffix = f" : {label}" if label else ""
        lines.append(f"    {nid(a)} {arrow} {mult}{nid(b)}{suffix}")
    if layout_edges:
        lines.append("    %% 孤立クラスを格子に並べるための配置専用の線 (themeCSS で非表示)")
        lines += layout_edges

    lines.append("    %% 配色")
    for module, *_ in MODULES:
        ids = [nid(n) for n in sorted(shown) if module_of(n) == module]
        if ids:
            lines.append(f'    cssClass "{",".join(ids)}" {mid(module)}')
    if stubs:
        lines.append(f'    cssClass "{",".join(nid(n) for n in stubs)}" refnode')

    out.parent.mkdir(exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(shown), len(kept), len(stubs), len(isolated)


def lbl(names):
    uniq = list(dict.fromkeys(names))
    return esc(", ".join(uniq[:3]) + (f" ...+{len(uniq) - 3}" if len(uniq) > 3 else ""))


def structural_edges(entities, by_id, solid, created, detailed):
    arrow = {"comp": "*--", "agg": "o--"}
    rows, strong = [], set()
    for ent in sorted(entities.values(), key=lambda e: e.name):
        for refid, bname in ent.bases:
            base = by_id[refid].name if refid in by_id else bname
            rows.append((base, "<|--", "", ent.name, ""))
    for (a, b, kind), info in sorted(solid.items()):
        strong.add((a, b))
        mult, label = "", ""
        if detailed:
            mults = set(info["mult"])
            if len(mults) == 1 and len(info["names"]) == 1:
                mult = f'"{next(iter(mults))}" '
            elif "0..*" in mults or len(info["names"]) > 1:
                mult = '"0..*" '
            label = lbl(info["names"])
        rows.append((a, arrow[kind], mult, b, label))
    for ent in sorted(entities.values(), key=lambda e: e.name):
        parent = ent.name.rsplit("::", 1)[0] if "::" in ent.name else ""
        if parent in entities and (parent, ent.name) not in strong:
            strong.add((parent, ent.name))
            rows.append((parent, "..", "", ent.name, ""))
    for a, b in sorted(created):
        if (a, b) not in strong:
            rows.append((a, "..>", "", b, "生成"))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--xml", type=Path, help="既存の Doxygen XML ディレクトリ (省略時は実行して生成)")
    ap.add_argument("--out", type=Path, default=ROOT / "docs/architecture")
    args = ap.parse_args()

    if args.xml:
        xml_dir = args.xml
    else:
        tmp = tempfile.mkdtemp().replace("\\", "/")
        subprocess.run(["doxygen", "-"], input=OVERLAY.format(out=tmp), text=True, cwd=ROOT, check=True)
        xml_dir = Path(tmp) / "xml"

    entities, by_id, members, aliases = load(xml_dir)
    solid, created, ecs_use, resolve = analyse(entities, by_id, members, aliases)
    missing = [n for n in entities if module_of(n) is None]
    if missing:
        raise SystemExit(f"どのモジュールにも属さない型: {missing}")

    plain = structural_edges(entities, by_id, solid, created, detailed=False)
    detailed = structural_edges(entities, by_id, solid, created, detailed=True)
    ecs_rows = [(a, "..>", "", b, "") for a, b in sorted(ecs_use)]

    kinds = collections.Counter(e.kind for e in entities.values())
    print("types", dict(kinds), "attrs", sum(len(e.attrs) for e in entities.values()),
          "methods", sum(len(e.methods) for e in entities.values()), "ambiguous", resolve.ambiguous.most_common(5))
    report = "{}: types={} edges={} refs={} isolated={}"
    print(report.format("overview", *emit(entities, by_id, args.out / "class-overview.mmd", "WikiGOLF 全体概要 (名前のみ)",
                                          plain, with_members=False)))
    print(report.format("full", *emit(entities, by_id, args.out / "class-full.mmd", "WikiGOLF 全クラス図",
                                      plain)))
    print(report.format("ecs", *emit(entities, by_id, args.out / "class-ecs-usage.mmd", "WikiGOLF ECS コンポーネント利用",
                                     ecs_rows, with_members=False, only_edge_nodes=True)))
    for module, *_ in MODULES:
        focus = {n for n in entities if module_of(n) == module}
        print(report.format(module, *emit(entities, by_id, args.out / "class-modules" / f"{module}.mmd",
                                          f"WikiGOLF クラス図: {module}", detailed, focus=focus)))


if __name__ == "__main__":
    main()

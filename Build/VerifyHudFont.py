"""Check the bundled HUD face against the project's actual text. Requires fontTools."""

import hashlib
import re
import sys
from pathlib import Path

from fontTools.ttLib import TTFont


PROJECT = Path(__file__).resolve().parents[1]
FONT_NAME = "NotoSansCJKsc-Regular.otf"
NOTICE_NAME = "NotoSansCJK-OFL.txt"
# https://github.com/notofonts/noto-cjk/releases/tag/Sans2.004
FONT_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
LITERALS = re.compile(r'"((?:[^"\\\r\n]|\\.)*)"')


def verify(font_folder):
    face = font_folder / FONT_NAME
    notice = font_folder / NOTICE_NAME
    if hashlib.sha256(face.read_bytes()).hexdigest() != FONT_SHA256:
        raise SystemExit(f"Unexpected font file: {face}")
    if "SIL OPEN FONT LICENSE" not in notice.read_text(encoding="utf-8-sig"):
        raise SystemExit(f"Missing OFL text: {notice}")

    corpus = {codepoint: "printable ASCII" for codepoint in range(32, 127)}
    for path in sorted((PROJECT / "Source" / "ManosabaDuel").glob("*")):
        if path.suffix not in (".cpp", ".h", ".inl"):
            continue
        code = path.read_text(encoding="utf-8-sig")
        for literal in LITERALS.findall(code):
            for character in literal:
                if character.isprintable():
                    corpus.setdefault(ord(character), path.name)

    with TTFont(face) as font:
        cmap = font.getBestCmap()
        missing = {codepoint for codepoint in corpus if cmap.get(codepoint, ".notdef") == ".notdef"}
        if missing:
            for codepoint in sorted(missing):
                print(f"U+{codepoint:04X} {chr(codepoint)} ({corpus[codepoint]})", file=sys.stderr)
            raise SystemExit(f"{len(missing)} missing characters in {face.name}")
        print(f"{face.name}: all {len(corpus)} current source characters covered ({len(cmap)} in cmap).")


if __name__ == "__main__":
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else PROJECT / "Content" / "DuelFonts")

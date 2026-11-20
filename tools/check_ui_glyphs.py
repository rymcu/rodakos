#!/usr/bin/env python3
"""Fail when a UI string uses a character that no firmware font can draw.

LVGL draws a missing glyph as an empty box. The firmware text fonts fall back
Montserrat -> RodakOS icons -> PuHui CJK subset, so a character must exist in one
of those three. The PuHui font is a subset; symbols such as U+24D8 are not in it.

Usage: tools/check_ui_glyphs.py [source files...]   (default: main/apps, main/phone_ui)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT_FILES = (
    'components/rodakos_fonts/src/font_puhui_16_4.c',
    'components/rodakos_fonts/src/rodakos_icons_20_4.c',
    'managed_components/lvgl__lvgl/src/font/lv_font_montserrat_14.c',
)
GLYPH_COMMENT = re.compile(r'/\* U\+([0-9A-Fa-f]+) ')
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def font_code_points():
    covered = set(range(32, 127))
    for name in FONT_FILES:
        path = ROOT / name
        if not path.exists():
            # managed_components is fetched by the IDF build; skip when absent.
            continue
        covered |= {int(cp, 16) for cp in GLYPH_COMMENT.findall(path.read_text('utf-8', 'ignore'))}
    return covered


def sources(arguments):
    if arguments:
        return [pathlib.Path(a).resolve() for a in arguments]
    found = []
    for folder in ('main/apps', 'main/phone_ui'):
        found += sorted((ROOT / folder).rglob('*.cc'))
    return found


def main(arguments):
    covered = font_code_points()
    missing = {}
    for path in sources(arguments):
        for lineno, line in enumerate(path.read_text('utf-8').splitlines(), 1):
            code = line.split('//', 1)[0]
            for literal in STRING_LITERAL.findall(code):
                for ch in literal:
                    if ord(ch) > 126 and ord(ch) not in covered:
                        missing.setdefault(ch, []).append(f'{path.relative_to(ROOT)}:{lineno}')
    for ch, where in sorted(missing.items()):
        print(f'missing glyph U+{ord(ch):04X} {ch}: {", ".join(where[:5])}')
    if missing:
        print('Use an ICON_* macro or a character the CJK subset contains.', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

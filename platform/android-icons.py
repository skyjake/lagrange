#!/usr/bin/env python3
# Builds the Android UI icon font: a static subset of Material Symbols whose glyphs are
# mapped to Lagrange's UI icon codepoints (lib/core/include/lagrange/defs.h).
#
# Source font and codepoints:
# https://github.com/google/material-design-icons/tree/master/variablefont
#
# Usage: android-icons.py <MaterialSymbolsOutlined[...].ttf> <...codepoints> [output.ttf]
#
# Requires fontTools (pip install fonttools).

import os
import re
import sys
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable
from fontTools.varLib import instancer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFS_H = os.path.join(ROOT, 'lib', 'core', 'include', 'lagrange', 'defs.h')
DEFAULT_OUTPUT = os.path.join(ROOT, 'app', 'res', 'fonts', 'MaterialSymbolsLagrange.ttf')

FAMILY = 'Material Symbols Lagrange'
INSTANCE = { 'FILL': 0, 'GRAD': 0, 'opsz': 24, 'wght': 400 }

# Lagrange icon codepoint: Material Symbols icon name (".fill" for the filled variant).
# Follows the SF Symbols mapping in systemImageName_Apple() (app/src/ui/util.c).
ICONS = {
    0x10117: 'format_list_bulleted',        # page_Icon
    0x1d362: 'menu',                        # menu_Icon
    0x1f310: 'language',                    # globe_Icon
    0x1f3a8: 'palette',                     # palette_Icon
    0x1f3ae: 'sports_esports',              # gamepad_Icon
    0x1f3e0: 'home.fill',                   # home_Icon
    0x1f3f7: 'sell',                        # tag_Icon
    0x1f41e: 'bug_report',                  # bug_Icon
    0x1f441: 'visibility',                  # eye_Icon
    0x1f464: 'person.fill',                 # person_Icon
    0x1f4c1: 'folder',                      # folder_Icon
    0x1f4c4: 'straighten',                  # Layout settings
    0x1f4cb: 'content_paste',               # clipboard_Icon
    0x1f4ce: 'attach_file',                 # paperclip_Icon
    0x1f4da: 'book_5',                      # books_Icon
    0x1f4e4: 'file_export',                 # export_Icon
    0x1f4e5: 'file_open',                   # import_Icon
    0x1f4e6: 'package_2',                   # package_Icon
    0x1f4e7: 'mail',                        # envelope_Icon
    0x1f4f1: 'smartphone',                  # User Interface settings
    0x1f503: 'refresh',                     # reload_Icon
    0x1f50d: 'search',                      # magnifyingGlass_Icon
    0x1f511: 'key',                         # key_Icon
    0x1f512: 'lock.fill',                   # closedLock_Icon
    0x1f513: 'lock_open.fill',              # openLock_Icon
    0x1f516: 'bookmark',                    # bookmark_Icon
    0x1f520: 'font_download',               # fontpack_Icon
    0x1f528: 'build',                       # hammer_Icon
    0x1f553: 'schedule',                    # clock_Icon
    0x1f56e: 'menu_book',                   # book_Icon
    0x1f588: 'push_pin',                    # pin_Icon
    0x1f5a7: 'lan',                         # network_Icon
    0x1f5b3: 'computer',                    # computer_Icon
    0x1f5bc: 'image',                       # photo_Icon
    0x1f5ce: 'description',                 # file_Icon
    0x1f5d1: 'delete',                      # trash_Icon
    0x1f5da: 'text_fields',                 # fonts_Icon
    0x1f641: 'sentiment_dissatisfied',      # unhappy_Icon
    0x1f660: 'brush',                       # pageStyle_Icon
    0x1f870: 'arrow_back',                  # backArrow_Icon
    0x1f871: 'arrow_upward',                # upArrow_Icon
    0x1f872: 'arrow_forward',               # forwardArrow_Icon
    0x1f873: 'arrow_downward',              # downArrow_Icon
    0x1fa90: 'planet',                      # planet_Icon
    0x2139:  'info',                        # info_Icon
    0x2191:  'arrow_upward',                # keyUpArrow_Icon
    0x2197:  'open_in_new',                 # openInTab_Icon
    0x21a4:  'keyboard_tab_rtl',            # barLeftArrow_Icon
    0x21a6:  'keyboard_tab',                # barRightArrow_Icon
    0x21c5:  'swap_vert',                   # upDownArrow_Icon
    0x21e8:  'arrow_right_alt',             # rightArrowWhite_Icon
    0x22f0:  'format_indent_increase',      # hierarchy_Icon
    0x231b:  'hourglass',                   # hourglass_Icon
    0x2328:  'keyboard',                    # keyboard_Icon
    0x232b:  'backspace',                   # delete_Icon
    0x2398:  'content_copy.fill',           # clipCopy_Icon / copy_Icon
    0x23ce:  'keyboard_return',             # return_Icon
    0x23ea:  'undo',                        # undo_Icon
    0x23f2:  'history',                     # timer_Icon
    0x25e7:  'dock_to_right.fill',          # leftHalf_Icon
    0x25e8:  'dock_to_left.fill',           # rightHalf_Icon
    0x2605:  'star.fill',                   # star_Icon
    0x2606:  'star',                        # whiteStar_Icon
    0x2610:  'check_box_outline_blank',     # ballotUnchecked_Icon
    0x2611:  'check_box.fill',              # ballotChecked_Icon
    0x2699:  'settings',                    # gear_Icon
    0x26a0:  'warning',                     # warning_Icon
    0x2700:  'content_cut',                 # scissor_Icon
    0x270e:  'edit',                        # edit_Icon
    0x2714:  'check',                       # check_Icon
    0x2750:  'filter_none',                 # openTabBg_Icon
    0x2795:  'add',                         # add_Icon
    0x279e:  'arrow_forward',               # rightArrow_Icon
    0x27a4:  'chevron_right',               # rightArrowhead_Icon
    0x27e8:  'chevron_left',                # leftAngle_Icon
    0x27e9:  'chevron_right',               # rightAngle_Icon
    0x2912:  'vertical_align_top',          # upArrowBar_Icon
    0x2913:  'vertical_align_bottom',       # downArrowBar_Icon
    0x2a2f:  'close',                       # close_Icon
    0x2b13:  'toolbar',                     # toolbar_Icon
    0x2b71:  'vertical_align_top',
    0x2b9c:  'chevron_left',                # leftArrowhead_Icon
    0x2ba5:  'send',                        # upload_Icon
    0x2ba7:  'download',                    # download_Icon
    0xfe40:  'keyboard_arrow_down',         # downAngle_Icon
}

# Icons in defs.h that are intentionally left to the regular fonts.
UNMAPPED = {
    0x2022,  # bullet_Icon, midEllipsis_Icon: also plain punctuation
    0x25cb,  # circleWhite_Icon
    0x25cf,  # circle_Icon
    0x27a0,  # openExt_Icon, networkProxy_Icon
    0x2b1a,  # openWindow_Icon, select_Icon
    0x1f41f, # guppy_Icon
    0x1f4a5, # explosion_Icon
    0x1f4aa, # spartan_Icon
    0x1f687, # nex_Icon
}


def read_codepoints(path):
    names = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                names[parts[0]] = int(parts[1], 16)
    return names


def check_defs():
    pattern = re.compile(r'#define\s+\w+_Icon\s+"((?:\\[uU][0-9a-fA-F]+)+)"')
    with open(DEFS_H, encoding='utf-8') as f:
        for match in pattern.finditer(f.read()):
            for esc in re.findall(r'\\[uU]([0-9a-fA-F]+)', match.group(1)):
                cp = int(esc, 16)
                if cp not in ICONS and cp not in UNMAPPED:
                    print(f'warning: U+{cp:04X} in defs.h is not mapped', file=sys.stderr)


def resolve_glyphs(font, codepoints):
    cmap = font.getBestCmap()
    glyphs = set(font.getGlyphOrder())
    mapping = {}
    for cp, icon in sorted(ICONS.items()):
        name, _, variant = icon.partition('.')
        if name not in codepoints:
            sys.exit(f'error: unknown icon "{name}"')
        glyph = cmap[codepoints[name]]
        if variant:
            glyph += '.' + variant
        if glyph not in glyphs:
            sys.exit(f'error: no glyph "{glyph}" for icon "{icon}"')
        mapping[cp] = glyph
    return mapping


def set_cmap(font, mapping):
    tables = []
    for platformID, platEncID, fmt in ((0, 3, 4), (3, 1, 4), (0, 4, 12), (3, 10, 12)):
        table = CmapSubtable.newSubtable(fmt)
        table.platformID = platformID
        table.platEncID  = platEncID
        table.language   = 0
        table.cmap = { cp: g for cp, g in mapping.items() if fmt == 12 or cp <= 0xffff }
        tables.append(table)
    font['cmap'].tables = tables


def set_names(font):
    name = font['name']
    version = name.getDebugName(5) or ''
    for platformID, platEncID, langID in ((3, 1, 0x409), (1, 0, 0)):
        name.setName(FAMILY, 1, platformID, platEncID, langID)
        name.setName('Regular', 2, platformID, platEncID, langID)
        name.setName(FAMILY + ' Regular', 3, platformID, platEncID, langID)
        name.setName(FAMILY, 4, platformID, platEncID, langID)
        name.setName(version + '; modified: subset with glyphs remapped to Lagrange icon '
                     'codepoints', 5, platformID, platEncID, langID)
        name.setName(FAMILY.replace(' ', '') + '-Regular', 6, platformID, platEncID, langID)
    name.names = [n for n in name.names if n.nameID < 16 or n.nameID > 22]


def main():
    if len(sys.argv) < 3:
        sys.exit('usage: android-icons.py <font.ttf> <font.codepoints> [output.ttf]')
    output = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_OUTPUT
    check_defs()
    font = TTFont(sys.argv[1])
    instancer.instantiateVariableFont(font, INSTANCE, inplace=True)
    mapping = resolve_glyphs(font, read_codepoints(sys.argv[2]))
    options = subset.Options()
    options.layout_features = []
    options.name_IDs = ['*']
    options.notdef_outline = True
    options.hinting = False
    subsetter = subset.Subsetter(options)
    subsetter.populate(glyphs=sorted(set(mapping.values())))
    subsetter.subset(font)
    for tag in ('GSUB', 'GPOS', 'GDEF', 'STAT'):
        if tag in font:
            del font[tag]
    set_cmap(font, mapping)
    set_names(font)
    os.makedirs(os.path.dirname(output), exist_ok=True)
    font.save(output)
    print(f'{output}: {len(mapping)} codepoints, {len(font.getGlyphOrder())} glyphs, '
          f'{os.path.getsize(output)} bytes')


if __name__ == '__main__':
    main()

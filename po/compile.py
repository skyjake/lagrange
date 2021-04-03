#!/usr/bin/env python3
# Parses all the .po files and generates binary language strings to be loaded 
# at runtime via embedded data.

import os, sys

BUILD_LANGS = ['en', 'es', 'fi', 'ie', 'ru', 'sr', 'zh_Hans', 'zh_Hant']
MODE = 'compile'
ESCAPES = {
    '\\': '\\',
    '"': '"',
    'n': '\n',
    'r': '\r',
    't': '\t'
}

if '--new' in sys.argv:
    MODE = 'new'


def unquote(string):
    txt = string.strip()
    if txt[0] != '"' or txt[-1] != '"':
        raise Exception("invalid quoted string: " + string)
    txt = txt[1:-1]
    out = ''
    is_escape = False
    for c in txt:
        if is_escape:
            out += ESCAPES[c]
            is_escape = False
            continue
        if c == '\\':
            is_escape = True
        else:
            out += c
    return out        
    
    
def parse_po(src):
    messages = []
    is_multi = False  # string is multiple lines
    is_plural = False
    msg_id, msg_str, msg_index = None, None, None
    for line in open(src, 'rt', encoding='utf-8').readlines():
        line = line.strip()
        if is_multi:
            if len(line) == 0 or line[0] != '"':
                if msg_id:
                    messages.append((msg_id, msg_str, msg_index))
                is_multi = False
            else:
                msg_str += unquote(line)
        if line.startswith('msgid_plural'):
            msg_id = unquote(line[12:])
            is_plural = True
        elif line.startswith('msgid'):
            msg_id = unquote(line[6:])
            is_plural = False
        elif line.startswith('msgstr'):            
            if line[6] == '[':
                msg_index = int(line[7])
                line = line[9:]
            else:
                msg_index = None
                line = line[6:]
            if line.endswith(' ""'):
                is_multi = True
                msg_str = ''
            else:
                msg_str = unquote(line)
                if msg_id:
                    messages.append((msg_id, msg_str, msg_index))
    # Apply plural indices to ids.
    pluralized = []
    for msg_id, msg_str, msg_index in messages:
        if not msg_index is None:
            msg_id = f'{msg_id[:-1]}{msg_index}'
        pluralized.append((msg_id, msg_str))
        #print(msg_id, '=>', msg_str)
    return pluralized
    
    
def compile_string(msg_id, msg_str):
    return msg_id.encode('utf-8') + bytes([0]) + \
           msg_str.encode('utf-8') + bytes([0])
                          
    
if MODE == 'compile':
    BASE_STRINGS = {}
    PLURALS = set()
    for msg_id, msg_str in parse_po('en.po'):
        BASE_STRINGS[msg_id] = msg_str
        if msg_id.endswith('.0'):
            PLURALS.add(msg_id[:-2])
    for src in os.listdir('.'):
        if src.endswith('.po') and src.split('.')[0] in BUILD_LANGS:
            # Make a binary blob with strings sorted by ID.
            have_ids = set()
            compiled = bytes()
            lang = parse_po(src)
            for msg_id, _ in lang:
                have_ids.add(msg_id)
            # Take missing strings from the base language.
            for msg_id in BASE_STRINGS:
                if msg_id not in have_ids and not msg_id[:-2] in PLURALS:
                    print('%10s' % src, 'missing:', msg_id)
                    lang.append((msg_id, BASE_STRINGS[msg_id]))
            for msg_id, msg_str in sorted(lang):
                compiled += compile_string(msg_id, msg_str)
            open(f'../res/lang/{src[:-3]}.bin', 'wb').write(compiled)

elif MODE == 'new':
    messages = parse_po('en.po')
    f = open('new.po', 'wt', encoding='utf-8')
    for msg_id, _ in messages:
        print(f'\nmsgid "{msg_id}"\nmsgstr ""\n', file=f)

    

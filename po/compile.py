#!/usr/bin/env python3
# Parses all the .po files and generates binary language strings to be loaded 
# at runtime via embedded data.

import os, sys

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
    msg_id, msg_str = None, None
    for line in open(src, 'rt', encoding='utf-8').readlines():
        line = line.strip()
        if line.startswith('msgid'):
            msg_id = unquote(line[6:])
        elif line.startswith('msgstr'):
            msg_str = unquote(line[7:])        
            messages.append((msg_id, msg_str))
    return messages
    

if MODE == 'compile':
    for src in os.listdir('.'):
        if src.endswith('.po'):
            # Make a binary blob with strings sorted by ID.
            compiled = bytes()
            for msg in sorted(parse_po(src)):
                compiled += msg[0].encode('utf-8') + bytes([0])
                compiled += msg[1].encode('utf-8') + bytes([0])
            open(f'../res/lang/{src[:-3]}.bin', 'wb').write(compiled)

elif MODE == 'new':
    messages = parse_po('en.po')
    f = open('new.po', 'wt', encoding='utf-8')
    for msg_id, _ in messages:
        print(f'\nmsgid "{msg_id}"\nmsgstr ""\n', file=f)

    

#!/usr/bin/env python3
# Parses all the .po files and generates binary language strings to be loaded 
# at runtime via embedded data.

import os

ESCAPES = {
    '\\': '\\',
    '"': '"',
    'n': '\n',
    'r': '\r',
    't': '\t'
}


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
        
        
messages = []
for src in os.listdir('.'):
    if not src.endswith('.po'):
        continue
    msg_id, msg_str = None, None
    for line in open(src, 'rt', encoding='utf-8').readlines():
        line = line.strip()
        if line.startswith('msgid'):
            msg_id = unquote(line[6:])
        elif line.startswith('msgstr'):
            msg_str = unquote(line[7:])        
            messages.append((msg_id, msg_str))
    # Make a binary blob with strings sorted by ID.
    compiled = bytes()
    for msg in sorted(messages):
        compiled += msg[0].encode('utf-8') + bytes([0])
        compiled += msg[1].encode('utf-8') + bytes([0])
    #print(compiled)
    open(f'../res/lang/{src[:-3]}.bin', 'wb').write(compiled)
    
    

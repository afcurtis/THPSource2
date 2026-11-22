"""10d: arity.py's scan, classified by the live resolver's rules (src/hooks/kits/kit_dll.cpp resolve_and_call).

OK-trim   cdecl row, more arguments reported than the row takes: the resolver trims them
OK-erase  stdcall row, exactly one more than the row takes: the resolver drops the first (the object)
FAULT     anything else: ServiceFault at run time on a live path
"""
import re, sys, glob, yaml

rows = {}
for k in ('re/config/standalone_natives.yml', 're/config/standalone_natives_manual.yml'):
    for r in yaml.safe_load(open(k))['natives']:
        rows[int(r['address'])] = (r['name'], r['convention'], int(r['stack_args']))
nh = yaml.safe_load(open('re/config/native_hooks.yml'))
for h in nh['hooks']:
    b = h.get('builds', {})
    if 'canonical' in b:
        rows[int(b['canonical'])] = (h['name'], h.get('convention', 'cdecl'), int(h.get('stack_args', 0)))


def split_args(text):
    depth = 0
    n = 0
    any_ = False
    for ch in text:
        if ch in '({[':
            depth += 1
        if ch in ')}]':
            depth -= 1
        if ch == ',' and depth == 0:
            n += 1
        elif not ch.isspace():
            any_ = True
    return (n + 1) if any_ else 0


def balanced(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '{':
            d += 1
        elif t[j] == '}':
            d -= 1
            if d == 0:
                return t[i + 1:j]
    return None


trims = []


def classify(a, args):
    name, conv, stack = rows[a]
    if conv == 'cdecl' and args > stack:
        args = stack
    if conv != 'thiscall' and conv == 'stdcall' and args == stack + 1:
        args -= 1
    expected = stack + (1 if conv == 'thiscall' else 0)
    return args == expected, name, conv, stack, expected


total = 0
for f in sorted(glob.glob('src/runtime/*.cpp')):
    if f.endswith('_test.cpp'):
        continue
    t = open(f, errors='replace').read()
    consts = dict((m.group(1), int(m.group(2), 16)) for m in re.finditer(
        r'constexpr\s+(?:const\s+)?(?:std::uint32_t|Word|unsigned)\s+(k\w+)\s*=\s*(0x[0-9a-fA-F]+)', t))
    for m in re.finditer(r'\b(engine|engine_value|engine_named)\(\s*(0x[0-9a-fA-F]+|k\w+)\s*(?:,\s*\{)?', t):
        tok = m.group(2)
        a = int(tok, 16) if tok.startswith('0x') else consts.get(tok)
        if a is None or a not in rows:
            continue
        args = 0
        if t[m.end() - 1] == '{':
            body = balanced(t, m.end() - 1)
            if body is None:
                continue
            args = split_args(body)
        if m.group(1) == 'engine_named':
            args += 1
        before = len(trims)
        ok, name, conv, stack, expected = classify(a, args)
        if len(trims) > before:
            print(f'{f}:{t.count(chr(10), 0, m.start()) + 1}: TRIM {name} {hex(a)} {conv}/{stack} reported {args}', file=sys.stderr)
        if not ok:
            total += 1
            line = t.count('\n', 0, m.start()) + 1
            print(f'{f}:{line}: {name} {hex(a)} {conv}/{stack} reported {args} wants {expected}')
print('FAULT sites:', total, file=sys.stderr)

"""F1.5F: traffic (vtable 0x5184e0) and platforms (0x5194f8) positions per delivery. python scripts/play/fr/trf.py LEVEL RATE[:nogate] [FRAMES]
Prints, per object index, the mean speed (world units / tick, Q12/4096) between sample ticks, and the heading-free path length."""
import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/fr"), exist_ok=True)  # outputs stay in build/
import math, os, re, sys
from pathlib import Path
sys.path.insert(0, os.path.expanduser("~/THPSource2"))
from tony import standalone_boot as sb
from tony import standalone_warehouse as sw
level = int(sys.argv[1]); cfg = sys.argv[2]; frames = int(sys.argv[3]) if len(sys.argv) > 3 else 600
rate = cfg.split(':')[0]; nog = ':nogate' in cfg
tag = f'trf{level}-{rate}{"n" if nog else ""}'
root = Path((os.path.expanduser("~/THPSource2") + '/build/fr/run')); root.mkdir(parents=True, exist_ok=True)
spec = root / f'{tag}.txt'; cap = root / f'{tag}.otcap'
spec.write_text(f'level {level}\nframes {frames}\nsha256 f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c\n')
cap.unlink(missing_ok=True)
extra = {'OT_SCENARIO': sw._wine_z_path(spec), 'OT_SCENARIO_OUT': sw._wine_z_path(cap), 'OT_FRAME_RATE': '' if rate == '30' else rate,
         'OT_DELIVERY_LOG': '1:3000', 'OT_DELIVERY_OBJECTS': '5184e0,5194f8'}
if nog: extra['OT_NOGATE'] = '1'
doc = {'name': tag, 'deliveries': 20000, 'every': 2, 'keys': [], 'masks': []}
fin, text = sb.run_standalone(f'fr-{tag}', doc, extra=extra)
Path(f'{os.path.expanduser("~/THPSource2")}/build/fr/trf_{tag}.txt').write_text(text)
paths = {}; last = 0
for m in re.finditer(r'sim (\d+), skater[^\]]*\]', text):
    pass
for line in text.split('\n'):
    m = re.search(r'sim (\d+),', line)
    if not m or ' o ' not in line: continue
    sim = int(m.group(1))
    for o in re.finditer(r' o (\d+):([0-9a-f]+)=(-?\d+),(-?\d+),(-?\d+)', line):
        paths.setdefault(int(o.group(1)), []).append((sim, int(o.group(3)), int(o.group(4)), int(o.group(5)), o.group(2)))
print(tag, 'objects', len(paths), 'finished', fin)
for idx, p in sorted(paths.items()):
    s0, s1 = p[0], p[-1]
    d = math.dist(s0[1:4], s1[1:4]) / 4096; length = sum(math.dist(a[1:4], b[1:4]) for a, b in zip(p, p[1:])) / 4096
    print(f'  obj {idx} vt {s0[4]} ticks {s0[0]}..{s1[0]} net {d:.1f} path {length:.1f} per tick {length / max(1, s1[0] - s0[0]):.4f}')

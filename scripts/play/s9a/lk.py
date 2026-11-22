import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/s9a"), exist_ok=True)  # outputs stay in build/
import sys,bisect
rows=[]
for l in open('build/s9a/nm2.txt',errors='replace'):
    p=l.split(None,2)
    if len(p)==3:
        try: rows.append((int(p[0],16),p[2].strip()))
        except: pass
rows.sort()
keys=[r[0] for r in rows]
for a in sys.argv[1:]:
    v=int(a,16)
    i=bisect.bisect_right(keys,v)-1
    print(a,rows[i][1][:200],hex(v-rows[i][0]))

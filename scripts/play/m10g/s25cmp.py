import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/m10g"), exist_ok=True)  # outputs stay in build/
import re
ret = []
for line in open("build/m10g/s25b.watch", errors="replace"):
    m = re.match(r"session: watch 004638D0 from ([0-9A-F]+) \(([0-9A-F]+) ", line)
    if m:
        d = re.search(r"delivery (\d+)", line).group(1)
        ret.append((int(m.group(2), 16) & 0xffff, m.group(1), int(d)))
sa = []
for line in open("build/m10g/w22.txt", errors="replace"):
    m = re.match(r"kit: watch: 004638d0\(([0-9a-f]+), [0-9a-f]+\) in (\w+) \[poll (\d+)\]", line)
    if m:
        sa.append((int(m.group(1), 16) & 0xffff, m.group(2), int(m.group(3))))
print(len(ret), len(sa))
n = min(len(ret), len(sa))
for i in range(n):
    if ret[i][0] != sa[i][0]:
        print("first mismatch at", i)
        for j in range(max(0, i - 6), min(n, i + 12)):
            print(j, "retail", hex(ret[j][0]), ret[j][1], "d", ret[j][2], "| standalone", hex(sa[j][0]), sa[j][1], "poll", sa[j][2])
        break
else:
    print("equal over", n)

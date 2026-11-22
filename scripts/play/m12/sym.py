"""sym.py [--dll PATH] ADDR...: names for addresses of a CRASH report (32-bit: the 0x10000000-based `caller` values; 64-bit: offsets), by `llvm-nm --numeric-sort -C` on the standalone DLL.
Prints the nearest preceding symbol for each address."""
import bisect
import shutil
import subprocess
import sys

args = sys.argv[1:]
dll = (__import__("os").path.expanduser("~/THPSource2") + "/build/hooks/kits/opentony_standalone.dll")
if args and args[0] == "--dll":
    dll, args = args[1], args[2:]
nm = shutil.which("llvm-nm") or shutil.which("llvm-nm-18") or shutil.which("nm")
out = subprocess.run([nm, "--numeric-sort", "-C", dll], capture_output=True, text=True).stdout
syms = []
for line in out.splitlines():
    parts = line.split(None, 2)
    if len(parts) == 3:
        try:
            syms.append((int(parts[0], 16), parts[2]))
        except ValueError:
            pass
syms.sort()
keys = [a for a, _ in syms]
for a in args:
    v = int(a, 16)
    i = bisect.bisect_right(keys, v) - 1
    print(f"{a}: {syms[i][1][:150]} +{v - syms[i][0]:#x}" if i >= 0 else f"{a}: ?")

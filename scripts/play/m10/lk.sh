#!/bin/bash
# lk.sh SESSION: symbolize the crash address and the caller words of a standalone run (fresh llvm-nm of the DLL into build/s9a/nm2.txt)
cd ~/THPSource2
nm=$(ls .tools/llvm-mingw/*/bin/llvm-nm 2>/dev/null | head -1)
"$nm" --numeric-sort --demangle build/hooks/kits/opentony_standalone.dll > build/s9a/nm2.txt
run=build/sessions/runs/$1-standalone/run.txt
crash=$(grep -o "opentony_standalone.dll +[0-9a-f]*" "$run" | head -1 | awk '{print $2}' | tr -d +)
addrs=""
if [ -n "$crash" ]; then addrs=$(printf "0x%x" $((0x10000000 + 0x$crash))); fi
addrs="$addrs $(grep -o "caller 0x1[0-9a-f]*" "$run" | awk '{print $2}' | tr '\n' ' ')"
python3 scripts/play/s9a/lk.py $addrs

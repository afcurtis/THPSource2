#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10c"  # outputs stay in build/ (scripts/play/README.md)
# native build of the given targets (default: the runtime library); log in build/m10c/nb.log
cd ~/THPSource2
t="${@:-opentony_runtime}"
cmake -S . -B build/native > build/m10c/nb.log 2>&1
cmake --build build/native -j16 --target $t >> build/m10c/nb.log 2>&1
echo "exit $?" >> build/m10c/nb.log
grep -E "error|warning: " build/m10c/nb.log | head -30
tail -1 build/m10c/nb.log

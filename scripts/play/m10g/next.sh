#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
# generate rows and adapters, build the DLLs, run the standalone career for N seconds and name the next stop: next.sh [SESSION] [SECONDS]
cd ~/THPSource2
bash scripts/play/m10/gen.sh > build/m10g/gen_out.txt 2>&1
echo "gen exit: $(grep -c . build/m10g/gen_out.txt) lines; errors:"
grep -n "error" build/m10g/gen_out.txt | head -10
bash scripts/play/m10e/respin.sh "${1:-boot-career-hangar}" "${2:-200}"

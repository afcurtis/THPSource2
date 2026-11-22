#!/bin/bash
# fuzz_levels.sh "LEVELS" FIRST COUNT FRAMES OUT
cd ~/THPSource2
for l in $1; do LEVEL=$l .tools/venv/bin/python scripts/play/m12/fuzz_hangar.py $2 $3 $4 2>&1 | grep -v No-CD; done > $5
echo FUZZDONE >> $5

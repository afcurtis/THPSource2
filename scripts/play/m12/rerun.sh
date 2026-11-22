#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m12"  # outputs stay in build/ (scripts/play/README.md)
# rerun.sh LEVEL SEED [FRAMES]: one plain seed again with the exit code; the output lands where the original did (build/m12/fuzz/LNsSEED)
cd ~/THPSource2
.tools/venv/bin/python scripts/play/m12/rerun_seed.py "$@" 2>&1 | grep -v 'No-CD\|bogus'
grep -v 'No-CD\|bogus\|^game:\|resolve:' build/m12/fuzz/L${1}s${2}/output.txt | tail -5 | cut -c1-240

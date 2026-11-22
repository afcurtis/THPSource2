#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m12"  # outputs stay in build/ (scripts/play/README.md)
# wide_fz.sh "LEVELS" TAG [FIRST COUNT FRAMES [EXTRA_ENV]]: fuzz_hangar over levels -> build/m12/wide_TAG.txt
cd ~/THPSource2
rm -f build/m12/wide_.txt
bash scripts/play/m12/fuzz_levels.sh "$1" "${3:-11}" "${4:-90}" "${5:-600}" "build/m12/wide_$2.txt"

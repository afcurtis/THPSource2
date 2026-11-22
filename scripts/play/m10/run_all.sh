#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# Hangar, Warehouse, then levels 1..11: run_all.sh [FRAMES]
cd $HOME/THPSource2
bash scripts/play/m10/run_trg_both.sh "${1:-600}"
bash scripts/play/m10/run_trg_levels.sh 1 11 200
echo all done > build/m10/run_all.done

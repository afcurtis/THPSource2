#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# trace the interpreter over the Hangar (level 0) and Warehouse (level 12) loads: run_trg_both.sh [FRAMES]
cd $HOME/THPSource2
frames=${1:-600}
bash scripts/play/p46/kill.sh > /dev/null 2>&1
bash scripts/play/m10/run_trg.sh build/m10/trg_hangar.ndjson 0 4000 40 "$frames"
cp build/m10/run_trg.log build/m10/run_trg_hangar.log
bash scripts/play/p46/kill.sh > /dev/null 2>&1
bash scripts/play/m10/run_trg.sh build/m10/trg_warehouse.ndjson 12 4000 40 "$frames"
cp build/m10/run_trg.log build/m10/run_trg_warehouse.log
echo both done > build/m10/run_trg_both.done

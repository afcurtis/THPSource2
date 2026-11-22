#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# trace the interpreter over the level loads FIRST..LAST (default 1..11): run_trg_levels.sh [FIRST] [LAST] [FRAMES]
cd $HOME/THPSource2
first=${1:-1}
last=${2:-11}
frames=${3:-200}
for level in $(seq "$first" "$last"); do
    bash scripts/play/p46/kill.sh > /dev/null 2>&1
    bash scripts/play/m10/run_trg.sh "build/m10/trg_level$level.ndjson" "$level" 4000 40 "$frames"
    cp build/m10/run_trg.log "build/m10/run_trg_level$level.log"
done
echo levels done > build/m10/run_trg_levels.done

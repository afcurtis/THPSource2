#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10e"  # outputs stay in build/ (scripts/play/README.md)
# trace Ob_AI over the Hangar (0), the Warehouse (12) and levels 1..11; run_all_ob_ai.sh [LEVELS...]
cd $HOME/THPSource2
levels="${@:-0 12 1 2 3 4 5 6 7 8 9 10 11}"
for level in $levels; do
    bash scripts/play/p46/kill.sh > /dev/null 2>&1
    bash scripts/play/m10e/run_ob_ai.sh "build/m10e/ob_ai_level$level.ndjson" "$level" 90 3000
    cp build/m10e/run_ob_ai.log "build/m10e/run_ob_ai_level$level.log"
done
echo done > build/m10e/run_all_ob_ai.done

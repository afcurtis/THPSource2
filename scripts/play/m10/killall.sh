#!/usr/bin/env bash
# stop the trace chain (by PID, never pkill -f) and the game it started
for p in $(ps -eo pid,cmd | grep -E 'scripts/play/m10/run_all.sh|run_trg_both.sh|run_trg_levels.sh|scripts/play/m10/run_trg.sh|run_gdb_level.py' | grep -v grep | awk '{print $1}'); do
    kill "$p" 2>/dev/null
done
sleep 1
bash $HOME/THPSource2/scripts/play/p46/kill.sh > /dev/null 2>&1
ps -eo pid,cmd | grep -E 'run_all|run_trg|THawk|run_gdb' | grep -v grep | cut -c1-80
echo killed

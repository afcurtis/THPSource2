#!/usr/bin/env bash
# kill only the probe run started from build/p46 (by PID, never pkill -f)
for p in $(ps -eo pid,cmd | grep -E 'run_gdb_level.py build/p46|THawk2.nocd.exe|winedbg.exe --gdb --no-start --port 313|gdb -q -nx -ex target remote' | grep -v grep | awk '{print $1}'); do
  kill $p 2>/dev/null
done
sleep 2
ps -eo pid,etime,cmd | grep -E '[g]db|[T]Hawk|[w]inedbg' | cut -c1-120

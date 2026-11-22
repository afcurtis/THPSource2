#!/bin/bash
# kill a running gates run and every leaked Xvfb / Wine of it (by PID, never pkill -f)
for pat in 'tony gates' 'gates full'; do
    for p in $(ps -eo pid,cmd | grep -E "$pat" | grep -v grep | grep -v killgates | awk '{print $1}'); do kill "$p" 2>/dev/null; done
done
sleep 3
for p in $(ps -eo pid,cmd | grep -E 'Xvfb|xvfb-run|wineserver|winedevice|wine explorer|THawk2' | grep -v grep | awk '{print $1}'); do kill "$p" 2>/dev/null; done
sleep 3
ps -eo pid,cmd | grep -E 'gates|Xvfb|wine|THawk2' | grep -v grep | cut -c1-100
rm -f /tmp/.X9?-lock
echo killed

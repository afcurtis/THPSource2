#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
# w22.sh DELIVERIES ADDRS: the standalone Warehouse idle run with OT_RESOLVER_WATCH=ADDRS; the watch lines summarised (call, native, count) and the last few
cd ~/THPSource2
.tools/venv/bin/python scripts/play/m10g/sp22.py warehouse-idle "$1" watch "OT_RESOLVER_WATCH=$2" > build/m10g/w22.txt 2>&1
grep -c watch build/m10g/w22.txt
grep watch build/m10g/w22.txt | sed -E 's/\(.*\)//; s/\[poll [0-9]+\]//' | sort | uniq -c | head -20
grep watch build/m10g/w22.txt | tail -${3:-4}

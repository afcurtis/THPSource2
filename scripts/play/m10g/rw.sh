#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
# rw.sh LABEL SESSION DELIVERIES ADDR:BYTES,...   retail session run with the session watch on those addresses
cd ~/THPSource2 && source .tools/venv/bin/activate
python scripts/play/m10g/retail_watch2.py "$@" > build/m10g/rw_$1.log 2>&1
echo "rc=$?" >> build/m10g/rw_$1.log

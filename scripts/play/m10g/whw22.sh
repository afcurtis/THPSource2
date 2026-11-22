#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
cd ~/THPSource2
export OPENTONY_SESSION_WATCH=$1
./tony.sh standalone warehouse warehouse-idle > build/m10g/whw22.log 2>&1
grep "frames recorded\|records" build/m10g/whw22.log | cut -c1-200
grep -a "watch" build/hooks/runtime/opentony.log | grep -v watching | head -${2:-20}

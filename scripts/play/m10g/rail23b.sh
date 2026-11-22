#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
cd ~/THPSource2
bash scripts/play/s9a/build.sh > build/m10g/build23.log 2>&1
grep -n "error" build/s9a/build.log | head -10
bash scripts/play/m10g/rail23.sh

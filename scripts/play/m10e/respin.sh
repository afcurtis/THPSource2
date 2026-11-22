#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10e"  # outputs stay in build/ (scripts/play/README.md)
# build the hook/standalone DLLs, then run the standalone boot of a session for N seconds: respin.sh [NAME] [SECONDS]; prints the FATAL line and the tail of the log
cd ~/THPSource2
bash scripts/play/s9a/build.sh > build/m10e/respin_build.log 2>&1
grep -n "error" build/s9a/build.log | head -10
bash scripts/play/m10e/spin.sh "${1:-boot-career-hangar}" "${2:-200}" OT_FILES_LOG=1
grep -n "FATAL" build/m10e/spin.txt | cut -c1-500
tail -n 6 build/m10e/spin.txt | cut -c1-300

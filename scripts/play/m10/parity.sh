#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# tony standalone parity on the three sessions 9b left at M3D_SubmitModelState (retail references reused), then each run's FATAL line
cd ~/THPSource2
./tony.sh standalone parity "$@" --keep-retail > build/m10/parity.log 2>&1
echo "parity exit $?" >> build/m10/parity.log
for name in "$@"; do
    echo "== $name" >> build/m10/parity.log
    grep -n "FATAL\|caller 0x1" "build/sessions/runs/$name-standalone/run.txt" 2>/dev/null | head -8 >> build/m10/parity.log
    cat "build/standalone-boot/$name/result.json" 2>/dev/null | head -c 1500 >> build/m10/parity.log
    echo >> build/m10/parity.log
done

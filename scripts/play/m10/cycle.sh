#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# rebuild the hook DLLs, then the standalone parity of the given sessions (retail references reused); summary in build/m10/parity.log
cd ~/THPSource2
bash scripts/play/s9a/build.sh
if ! tail -1 build/s9a/build.log | grep -q "exit 0"; then
    grep -n "error" build/s9a/build.log | head -20
    exit 1
fi
bash scripts/play/m10/parity.sh "$@"
grep -v "^game: (log format" build/m10/parity.log | grep -v "^  \"detail\"" | head -60

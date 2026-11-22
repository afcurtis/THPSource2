#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/s9a"  # outputs stay in build/ (scripts/play/README.md)
cd ~/THPSource2
pkill -f THawk2.nocd.exe 2>/dev/null
./tony.sh standalone parity "$@" > build/s9a/parity.log 2>&1
echo "exit $?" >> build/s9a/parity.log

#!/bin/bash
mkdir -p "$HOME/THPSource2/build/standalone-warehouse"  # outputs stay in build/ (scripts/play/README.md)
# run.sh NAME...: tony standalone warehouse on the scenarios (retail reference reused when --keep-retail is passed), output in build/standalone-warehouse/run.log
cd ~/THPSource2
./tony.sh standalone warehouse "$@" > build/standalone-warehouse/run.log 2>&1
echo "exit $?" >> build/standalone-warehouse/run.log

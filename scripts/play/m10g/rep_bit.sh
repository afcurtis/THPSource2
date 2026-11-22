#!/bin/bash
cd ~/THPSource2
cmake -S . -B build/native > /dev/null 2>&1
bash scripts/play/m10e/nb.sh opentony_bit_retail_calls_test 2>&1 | grep -E "error|Error" | head -20
timeout 300 build/native/opentony_bit_retail_calls_test re/evidence/fixtures/bit-calls-bsdA.txt 2>&1 | head -8 | cut -c1-500

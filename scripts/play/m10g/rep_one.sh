#!/bin/bash
# rep_one.sh NAME...: build the world replay test and replay fixtures (world-calls-NAME.txt)
cd ~/THPSource2
bash scripts/play/m10e/nb.sh opentony_world_retail_calls_test 2>&1 | grep -E "error|Error" | head -20
for f in "$@"; do
  echo "== $f"
  REPLAY_DEBUG=1 timeout 300 build/native/opentony_world_retail_calls_test re/evidence/fixtures/world-calls-$f.txt 2>&1 | head -${HEAD:-12} | cut -c1-${CUT:-800}
done
echo REPONEDONE

#!/bin/bash
cd ~/THPSource2
for f in re/evidence/fixtures/world-calls-*.txt; do
  r=$(timeout 300 build/native/opentony_world_retail_calls_test $f 2>&1 | grep "cases replayed" | head -1)
  case "$r" in *" 0 mismatches"*) ;; *) echo "$f: $r";; esac
done
echo REPALLDONE

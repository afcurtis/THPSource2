#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10e"  # outputs stay in build/ (scripts/play/README.md)
# replay of Ob_AI; log in build/m10e/rt_ob_ai.log (BUILD=1 builds first)
cd ~/THPSource2
out=re/evidence/fixtures
if [ -n "$BUILD" ]; then bash scripts/play/m10e/nb.sh opentony_world_trig_nodes_trace_test > build/m10e/rt_ob_ai.log 2>&1; else : > build/m10e/rt_ob_ai.log; fi
for f in hangar warehouse levels; do
  [ -f $out/trg-obai-$f.txt ] || continue
  build/native/opentony_world_trig_nodes_trace_test $out/trg-obai-$f.txt --obai >> build/m10e/rt_ob_ai.log 2>&1; echo "obai $f exit $?" >> build/m10e/rt_ob_ai.log
done
build/native/opentony_world_trig_nodes_trace_test $out/trg-obai-hangar.txt --obai --mutate > /dev/null 2>&1; echo "obai mutate exit $? (must be 1)" >> build/m10e/rt_ob_ai.log
tail -40 build/m10e/rt_ob_ai.log

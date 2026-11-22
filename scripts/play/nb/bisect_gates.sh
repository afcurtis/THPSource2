#!/bin/bash
mkdir -p "$HOME/THPSource2/build/nb"  # outputs stay in build/ (scripts/play/README.md)
# The Phase 6 open item: gates full in one go fails at smoke/session. Run the smoke beside groups of the CPU lane, record the smoke's result, stop the run.
cd $HOME/THPSource2
OUT=build/nb/bisect
mkdir -p $OUT
: > $OUT/result.txt
try() {
  local name="$1"; shift
  rm -f build/gates/status.txt
  bash tony.sh gates full --only smoke/session "$@" > $OUT/$name.log 2>&1 &
  local pid=$!
  for i in $(seq 1 120); do
    sleep 5
    if grep -q "smoke/session" build/gates/status.txt 2>/dev/null; then break; fi
  done
  local line=$(grep "smoke/session" build/gates/status.txt 2>/dev/null)
  echo "$name: ${line:-no smoke line} | running: $(grep -c . build/gates/status.txt 2>/dev/null) lines" >> $OUT/result.txt
  bash scripts/play/m10e/killgates.sh > /dev/null 2>&1
  wait $pid 2>/dev/null
  sleep 5
}
try all native-build ctest verify-all pytest selftest replay32 hooks-build-64
try pytest pytest
try selftest_replay selftest replay32
try pytest_selftest pytest selftest
try pytest_replay pytest replay32
try pytest_verify_build64 pytest verify-all hooks-build-64
echo DONE >> $OUT/result.txt

#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
# cap24c.sh NAME CASES SEED: forced calls of CGLineBit_Move (group bit5) and the bit-calls fixture (no build, no replay)
cd ~/THPSource2
bash scripts/play/m10g/chainG.sh bit5 40b1d0 "${2:-500}" "${3:-91}" build/m10g/$1.ndjson > build/m10g/$1.log 2>&1
tail -1 build/m10g/$1.log
source .tools/venv/bin/activate
python scripts/make_career_calls_fixture.py build/m10g/$1.ndjson re/evidence/fixtures/bit-calls-$1.txt --title "Retail forced calls (10g s24): CGLineBit_Move" --curated | head -3
echo CAP24CDONE

#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
# cap24.sh NAME CASES SEED [ENV=..]: forced calls of Skater_StartGrind (group gsg) and the skater-calls fixture (no build, no replay)
cd ~/THPSource2
for kv in "${@:4}"; do export "$kv"; done
bash scripts/play/m10g/chainG.sh gsg 490ef0 "${2:-600}" "${3:-81}" build/m10g/$1.ndjson > build/m10g/$1.log 2>&1
tail -1 build/m10g/$1.log
source .tools/venv/bin/activate
python scripts/make_career_calls_fixture.py build/m10g/$1.ndjson re/evidence/fixtures/skater-calls-$1.txt --title "Retail forced calls (10g s24): Skater_StartGrind" --curated | head -3
echo CAP24DONE

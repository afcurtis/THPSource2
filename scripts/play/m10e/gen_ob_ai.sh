#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10e"  # outputs stay in build/ (scripts/play/README.md)
# Ob_AI traces -> fixtures under re/evidence/fixtures (hangar, warehouse, levels)
cd $HOME/THPSource2 && source .tools/venv/bin/activate
out=re/evidence/fixtures
python scripts/make_trg_interpreter_fixture.py $out/trg-obai-hangar.txt build/m10e/ob_ai_level0.ndjson
if [ -s build/m10e/ob_ai_level12.ndjson ]; then python scripts/make_trg_interpreter_fixture.py $out/trg-obai-warehouse.txt build/m10e/ob_ai_level12.ndjson; fi
levels=""
for n in 1 2 3 4 5 6 7 8 9 10 11; do
    if [ -s build/m10e/ob_ai_level$n.ndjson ]; then levels="$levels build/m10e/ob_ai_level$n.ndjson"; fi
done
if [ -n "$levels" ]; then python scripts/make_trg_interpreter_fixture.py $out/trg-obai-levels.txt $levels; fi
ls -la $out/trg-obai-*.txt

#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
# cap23.sh: session 23's forced-call capture of Gap_ApplyDefinitionState (group panel, fixture hud-calls-gapA)
cd ~/THPSource2
bash scripts/play/m10g/chainG.sh panel 48f690 "${1:-300}" "${2:-23}" build/m10g/hgaA.ndjson > build/m10g/hgaA.log 2>&1
tail -1 build/m10g/hgaA.log
source .tools/venv/bin/activate
python scripts/make_career_calls_fixture.py build/m10g/hgaA.ndjson re/evidence/fixtures/hud-calls-gapA.txt --title "Retail forced calls (10g s23): Gap_ApplyDefinitionState" --curated | head -3
echo CAP23DONE

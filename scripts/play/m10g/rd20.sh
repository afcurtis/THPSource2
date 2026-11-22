#!/bin/bash
cd ~/THPSource2
R=build/sessions/runs/boot-career-hangar-standalone
for d in ${1:-1604 1606}; do
  echo "== $d"
  .tools/venv/bin/python scripts/play/s9/ddiff.py $R-retail/session_$d.img $R/session_$d.img --max 100000 --zones auto 2>&1 | grep -E "^\s+00(54[4-9a-f]|55|56|69|6a)" | head -60
done

#!/bin/bash
cd ~/THPSource2
R=build/sessions/runs/boot-career-hangar-standalone
.tools/venv/bin/python scripts/play/m10g/vx22.py $R-retail/session_$1.heap $R/session_$1.heap $2 $3 2>&1 | head -${4:-70}

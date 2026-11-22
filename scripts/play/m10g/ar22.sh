#!/bin/bash
cd ~/THPSource2
R=build/sessions/runs/boot-career-hangar-standalone
.tools/venv/bin/python scripts/play/s9/arena.py $R-retail/session_${1}.heap $R/session_${1}.heap $2 $3

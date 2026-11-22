#!/bin/bash
cd ~/THPSource2 && source .tools/venv/bin/activate
python scripts/play/m10e/spin.py "$@" 2>&1 | tail -3

#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10e"  # outputs stay in build/ (scripts/play/README.md)
# usage: run_ob_ai.sh OUT LEVEL [MAX_CALLS] [STOP_FRAMES]   trace Ob_AI 0x00480ff0 on a level
cd $HOME/THPSource2 && source .tools/venv/bin/activate
export M10_OUT=${1:-build/m10e/ob_ai.ndjson}
export P44_LEVEL=${2:-0}
export M10_MAX=${3:-40}
export M10_STOP_FRAMES=${4:-600}
timeout 900 python build/p44c/run_gdb_level.py scripts/play/m10e/ob_ai_trace.py > build/m10e/run_ob_ai.log 2>&1
echo "done rc=$?" >> build/m10e/run_ob_ai.log

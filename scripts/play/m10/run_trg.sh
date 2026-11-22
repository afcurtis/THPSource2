#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# usage: run_trg.sh OUT [LEVEL] [MAX_CALLS] [QUOTA] [STOP_FRAMES]   trace TRG_InterpretCommandStream on a level load
cd $HOME/THPSource2 && source .tools/venv/bin/activate
export M10_OUT=${1:-build/m10/trg.ndjson}
export P44_LEVEL=${2:-0}
export M10_MAX=${3:-4000}
export M10_QUOTA=${4:-40}
export M10_STOP_FRAMES=${5:-0}
timeout 1200 python build/p44c/run_gdb_level.py build/m10/trg_trace.py > build/m10/run_trg.log 2>&1
echo "done rc=$?" >> build/m10/run_trg.log

#!/bin/bash
mkdir -p "$HOME/THPSource2/build/s9"  # outputs stay in build/ (scripts/play/README.md)
# one cycle: generate + build, then the cold boot (strict, then lenient when strict stops). usage: bash scripts/play/s9/cycle.sh [DELIVERIES] [DUMP]
cd ~/THPSource2
source scripts/activate.sh >/dev/null 2>&1
set +e +u +o pipefail
deliveries=${1:-2400}
dump=${2:-1,2}
tony hooks generate > build/s9/generate.log 2>&1 || { echo "generate failed"; tail -20 build/s9/generate.log; exit 1; }
tony hooks build > build/s9/build.log 2>&1 || { echo "build failed"; grep -E ' error|Error' build/s9/build.log | head -20; exit 1; }
grep -E ' error' build/s9/build.log | head
python scripts/play/s9/boot_run.py s9-boot --dump "$dump" --deliveries "$deliveries" > build/s9/boot.log 2>&1
grep -v "log format" build/s9/boot.log | grep -E "^exit|FATAL|CRASH|session:|boot:|frame limit" | head -20
if ! grep -q "session.done\|^exit 0x0 done" build/s9/boot.log; then
  python scripts/play/s9/boot_run.py s9-lenient --lenient --deliveries "$deliveries" > build/s9/lenient.log 2>&1
  echo "== lenient"
  grep -E "^exit|unresolved|CRASH|session: [0-9]" build/s9/lenient.log | head -40
fi

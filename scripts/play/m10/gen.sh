#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# regenerate the standalone rows and the hook/Kit tables, then build the hook DLLs (scripts/play/s9a/build.sh's step)
cd ~/THPSource2
.tools/venv/bin/python scripts/standalone_rows.py > build/m10/gen.log 2>&1
echo "standalone_rows exit $?" >> build/m10/gen.log
./tony.sh hooks generate >> build/m10/gen.log 2>&1
echo "hooks generate exit $?" >> build/m10/gen.log
git status --short re/config src/hooks >> build/m10/gen.log
bash scripts/play/s9a/build.sh
tail -5 build/s9a/build.log >> build/m10/gen.log
grep -n "error" build/s9a/build.log | head -20 >> build/m10/gen.log
cat build/m10/gen.log

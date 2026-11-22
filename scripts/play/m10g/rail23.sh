#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
cd ~/THPSource2
bash scripts/play/standalone-warehouse/run.sh warehouse-rail > build/m10g/rail23.log 2>&1
tail -25 build/m10g/rail23.log | cut -c1-300
python3 - <<'EOF'
import json
r = json.load(open("build/standalone-warehouse/warehouse-rail/result.json"))
print({k: (v if not isinstance(v, (list, dict)) else len(v)) for k, v in r.items()})
for d in r.get("frame_differences", [])[:6]:
    print(d)
EOF
echo RAIL23DONE

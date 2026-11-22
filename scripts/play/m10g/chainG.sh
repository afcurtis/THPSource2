#!/usr/bin/env bash
mkdir -p "$HOME/THPSource2/build/m10g"  # outputs stay in build/ (scripts/play/README.md)
# the forced-call captures of the gpu and gpa groups: chainG.sh GROUP ADDRESS [CASES] [SEED] [OUT]   (gpu 469de0: Game_GameplayUpdate; gpa 469a30: Skater_UpdateAnimationObjects)
source $HOME/THPSource2/build/p415b/capfn.sh
rm -f build/m10g/chainG.done
group="${1:-gpu}"
run "$group" "${3:-250}" "${5:-build/m10g/${group}A.ndjson}" "${2:-469de0}" "${4:-1}" 2400
echo done > build/m10g/chainG.done

#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10e"  # outputs stay in build/ (scripts/play/README.md)
# mutate.sh: apply each mutation to camera_effects.cpp, replay cmaE, report; restores the file
cd ~/THPSource2
cp src/runtime/camera_effects.cpp build/m10e/camera_effects.orig
run() { name="$1"; from="$2"; to="$3"
  cp build/m10e/camera_effects.orig src/runtime/camera_effects.cpp
  python3 - "$from" "$to" <<'PY'
import sys
p='src/runtime/camera_effects.cpp'; t=open(p).read(); a,b=sys.argv[1],sys.argv[2]
assert a in t, a
open(p,'w').write(t.replace(a,b,1))
PY
  bash scripts/play/m10e/nb.sh opentony_camera_retail_calls_test > /dev/null 2>&1
  r=$(build/native/opentony_camera_retail_calls_test re/evidence/fixtures/camera-calls-cmaE.txt 2>&1 | grep -E "mismatch" | head -1)
  echo "$name: $r"
}
run M1_gain300 "* 300u" "* 301u"
run M2_mulx "+ qx * tw + tx * qw)" "+ qx * tw + tw * qw)"
run M3_roll568 "m_.u32(F(0x10)) * 0x568u" "m_.u32(F(0x10)) * 0x569u"
run M4_slerp1511 "* 1511u" "* 1510u"
run M5_rollsign "about_y(F(0x1c0), s16(m_.u16(self + kRoll)))" "about_y(F(0x1c0), -s16(m_.u16(self + kRoll)))"
run M6_stamp "stamp(0x24d);
        m_.set16(self + kLeanPitchPrevious" "stamp(0x24c);
        m_.set16(self + kLeanPitchPrevious"
run M7_savedlimit "across > 0x76c" "across > 0x76b"
run M8_tripod1 "m_.s32(tripod() + kTripodMode) == 1 && m_.u32" "m_.s32(tripod() + kTripodMode) == 2 && m_.u32"
run M9_phase "if (phase_next > 2)" "if (phase_next > 3)"
run M10_dead4 "m_.u8(self + kPhase) == 4" "m_.u8(self + kPhase) == 5"
cp build/m10e/camera_effects.orig src/runtime/camera_effects.cpp

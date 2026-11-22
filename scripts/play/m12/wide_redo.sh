#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m12"  # outputs stay in build/ (scripts/play/README.md)
# wide_redo.sh: rerun every plain fuzz run that neither recorded all its frames nor stopped with a FATAL (a casualty of a wineserver kill by a retail run), once each
cd ~/THPSource2/build/m12/fuzz
list=""
for f in $(find . -name output.txt -newer ../wide_fz.sh | sort); do
  d=$(basename $(dirname $f))
  case $d in *x) continue;; esac
  if ! grep -q 'recorded' $f && ! grep -q FATAL $f; then list="$list $d"; fi
done
echo "redo: $list"
cd ~/THPSource2
for d in $list; do
  case $d in
    L*s*) level=${d#L}; level=${level%%s*}; seed=${d##*s};;
    s*) level=0; seed=${d#s};;
  esac
  bash scripts/play/m12/rerun.sh $level $seed 2>&1 | grep -E 'EXIT|level|FATAL' | tr '\n' ' '; echo
done
echo REDODONE

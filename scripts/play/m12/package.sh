#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m12"  # outputs stay in build/ (scripts/play/README.md)
# rebuild the x86-64 program and repackage it; output in build/m12/package.log
cd $HOME/THPSource2 || exit 1
{
  ./tony.sh hooks build --arch 64 && ./tony.sh release package --arch 64
  echo "exit $?"
} > build/m12/package.log 2>&1
echo DONE >> build/m12/package.log

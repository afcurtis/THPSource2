#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# decompile the given addresses into build/m10/ADDR.c
cd ~/THPSource2
for a in "$@"; do
    ./tony.sh ghidra decompile "$a" > "build/m10/$a.c" 2>&1
    echo "$a $(wc -l < "build/m10/$a.c")"
done

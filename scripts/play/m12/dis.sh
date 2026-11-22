#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m12"  # outputs stay in build/ (scripts/play/README.md)
# dis.sh ADDR LEN NAME -> build/m12/NAME.asm
cd $HOME/THPSource2
bash scripts/play/m10e/dis2.sh "$1" "$2" | tail -n +8 > build/m12/$3.asm
wc -l build/m12/$3.asm

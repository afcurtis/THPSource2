#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m10"  # outputs stay in build/ (scripts/play/README.md)
# disassemble text_004d1000 (0x004d1000..) into build/m10/text_004d1000.dis
cd ~/THPSource2
bin=$(ls match/original/modules/text_004d1000.bin original/modules/text_004d1000.bin 2>/dev/null | head -1)
echo "bin: $bin"
objdump -D -b binary -mi386 -M intel --adjust-vma=0x004d1000 "$bin" > build/m10/text_004d1000.dis
wc -l build/m10/text_004d1000.dis

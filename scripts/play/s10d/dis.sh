#!/bin/bash
# dis.sh ADDR COUNT...: disassemble COUNT bytes (decimal or 0x) at each ADDR COUNT pair
cd ~/THPSource2
while [ "$#" -ge 2 ]; do
    echo "== $1"
    bash scripts/play/m10/bytes.sh "$1" "$2" | tail -n +5
    shift 2
done

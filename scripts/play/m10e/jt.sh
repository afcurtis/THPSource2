cd ~/THPSource2
python3 - <<'EOF'
d = open("match/original/modules/text_004c1000.bin", "rb").read()
base = 0x4c1000
for i in range(14):
    off = 0x4c848c - base + 4 * i
    print(i + 1, hex(int.from_bytes(d[off:off + 4], "little")))
EOF

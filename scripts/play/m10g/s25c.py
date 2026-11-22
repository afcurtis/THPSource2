import struct, sys
R = "build/sessions/runs/warehouse-idle-standalone"
for tag, d in (("retail", R + "-retail"), ("standalone", R)):
    img = open(f"{d}/session_{sys.argv[1]}.img", "rb").read()
    def w(a): return struct.unpack_from("<I", img, a - 0x524000)[0]
    print(tag, "A", hex(w(0x567c88)), "B", hex(w(0x567c8c)), "modelA", hex(w(0x567c80)), "modelB", hex(w(0x567c84)))
    for i in range(14):
        e = 0x567a70 + 16 * i
        print(" ", i, [hex(w(e + 4 * k)) for k in range(4)])

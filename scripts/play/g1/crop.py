import os as _os; _os.makedirs(_os.path.expanduser("~/THPSource2/build/g1"), exist_ok=True)  # outputs stay in build/
import sys
sys.path.insert(0, _os.path.expanduser("~/THPSource2"))
from tony.hal import video
x0,y0,x1,y1,k = [int(v) for v in sys.argv[1:6]]
tags = sys.argv[6:]
rows=[]
for t in tags:
    im = video.read_image(f"build/g1/shots/{t}/frame_00060.ottx"); rgba = im.rgba(); w=im.width
    for y in range(y0,y1):
        line=b"".join(rgba[(y*w+x)*4:(y*w+x)*4+4]*k for x in range(x0,x1))
        rows += [line]*k
    rows += [b"\xff\x00\x00\xff"*((x1-x0)*k)]*3
W=(x1-x0)*k
video.write_png("build/g1/crop.png", W, len(rows), b"".join(rows))

import re,sys
addrs="401000 401030 401060 4011e0 40bd00 40bd10 40bd20 40bd30 40be60 40f140 40f150 40f160 40f170 40f180 40f190 40f1a0 410c40 414c50 416050 42fc70 464710 466c10 46c670 473650 47fd30 480730 48e9c0 48f5f0 48f690 48f8f0 48fb20 4904c0 496300 4ad5e0 4ad620 4ad9f0 4ada20 4adc30 4adc50 4adc60 4b3230 4b3270 4b37a0 4b3df0 4b8500 4c4c50 4c4d10 4c5ac0 4c5b00 4c5b60 4c5c70 4c5d70 4c5d90 4c5da0 4c77f0 4c7a00 4c7c50 4c8550 4c8650 4c9340 4ca270 4ca2d0 4cde70 4de010".split()
hooks={}
txt=open('re/config/native_hooks.yml').read()
for m in re.finditer(r"^  - name: (\S+)\n((?:    .*\n)+)",txt,re.M):
    body=m.group(2)
    c=re.search(r"canonical: 0x([0-9a-f]+)",body); k=re.search(r"kit: (\S+)",body); d=re.search(r'default: "?(\w+)',body)
    if c: hooks[int(c.group(1),16)]=(m.group(1),k.group(1) if k else '?',d.group(1) if d else '')
st={}
for f in ['re/config/standalone_natives.yml','re/config/standalone_natives_manual.yml']:
    for m in re.finditer(r"name: (\S+), address: 0x([0-9a-f]+), kit: (\w+)",open(f).read()):
        st[int(m.group(2),16)]=(m.group(1),m.group(3))
seams=open('re/symbols/seams.yml').read()
for a in addrs:
    v=int(a,16)
    s='0x%08x'%v in seams
    print(a, 'HOOK' if v in hooks else '', hooks.get(v,''), 'ROW' if v in st else '', st.get(v,''), 'SEAM' if s else '')

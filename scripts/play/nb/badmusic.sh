#!/bin/bash
mkdir -p "$HOME/THPSource2/build/nb"  # outputs stay in build/ (scripts/play/README.md)
# songs that cannot be played: one bad file beside a good one, and a folder of bad files only (the game's own music should play)
cd $HOME/THPSource2
PY=.tools/venv/bin/python
bash tony.sh hooks build > build/nb/badmusic_b32.log 2>&1 || { tail -20 build/nb/badmusic_b32.log; exit 1; }
bash tony.sh hooks build --arch 64 > build/nb/badmusic_b64.log 2>&1 || { tail -20 build/nb/badmusic_b64.log; exit 1; }
rm -rf build/nb/mixmusic build/nb/allbad
mkdir -p build/nb/mixmusic build/nb/allbad
head -c 300000 /dev/urandom > build/nb/mixmusic/AAA-Broken.bik
cp "${OT_TEST_INSTALL:?set OT_TEST_INSTALL to the test install (AGENTS.local.md)}/Mods/Music/2001-THPS3-Trax/Adolescents-Amoeba.bik" build/nb/mixmusic/BBB-Good.bik
head -c 200000 /dev/urandom > build/nb/allbad/One-Broken.bik
head -c 200000 /dev/urandom > build/nb/allbad/Two-Broken.bik
echo "== one bad, one good"
$PY scripts/play/nb/badmusic.py mix mixmusic 2>&1 | grep -v No-CD
echo "== all bad"
$PY scripts/play/nb/badmusic.py allbad allbad 2>&1 | grep -v No-CD

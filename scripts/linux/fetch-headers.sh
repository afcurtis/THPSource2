#!/usr/bin/env bash
# The headers the Linux build needs (src/linux/CMakeLists.txt) without root: the distribution's SDL3 and FFmpeg -dev packages, downloaded with
# `apt-get download` and unpacked into .tools/linux/sysroot. Only their headers are used: the game loads the system's libSDL3.so.0 and libav*.so.N
# at run time, so the packages must be the same major versions as the installed libraries (they are, when both come from the same release).
# With root, `sudo apt-get install libsdl3-dev libavcodec-dev libavformat-dev libavutil-dev` does the same and this script is not needed.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
debs="$root/.tools/linux/debs"
sysroot="$root/.tools/linux/sysroot"
mkdir -p "$debs" "$sysroot"
cd "$debs"
apt-get download libsdl3-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
for deb in ./*.deb; do
    dpkg -x "$deb" "$sysroot"
done
echo "headers in $sysroot/usr/include (SDL3/, x86_64-linux-gnu/libav*/)"

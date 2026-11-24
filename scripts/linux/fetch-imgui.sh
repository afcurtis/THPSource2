#!/usr/bin/env bash
# The pinned Dear ImGui source (re/config/imgui.yml) for the Linux launcher, checked by its size and SHA-256, unpacked into .tools/imgui/src.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
url=https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
archive="$root/.tools/media/imgui/imgui-1.92.9b.tar.gz"
mkdir -p "$(dirname "$archive")" "$root/.tools/imgui/src"
[ -f "$archive" ] || curl -sSL --fail -o "$archive" "$url"
[ "$(stat -c %s "$archive")" = 2123074 ] || { echo "imgui: unexpected size" >&2; exit 1; }
echo "21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99  $archive" | sha256sum -c --quiet
tar -xzf "$archive" -C "$root/.tools/imgui/src"
echo "Dear ImGui in $root/.tools/imgui/src/imgui-1.92.9b"

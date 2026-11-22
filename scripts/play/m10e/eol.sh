#!/bin/bash
# which modified tracked files differ from HEAD only by line endings (listed with their content-diff line count)
cd ~/THPSource2
for f in $(git diff --name-only); do
  n=$(git diff --ignore-space-at-eol --numstat -- "$f" | awk '{print $1+$2}')
  crlf=$(file -b "$f" | grep -c CRLF)
  echo "$f content-lines=${n:-0} crlf=$crlf"
done

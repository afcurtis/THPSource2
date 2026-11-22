#!/bin/bash
mkdir -p "$HOME/THPSource2/build/m12"  # outputs stay in build/ (scripts/play/README.md)
# fuzz_compare.sh SEED...: each seed's inputs against retail (tony standalone warehouse); log build/m12/fuzz_compare.log; the yml files are removed afterwards
cd $HOME/THPSource2
.tools/venv/bin/python scripts/play/m12/fuzz_to_yml.py "$@"
for s in "$@"; do
  ./tony.sh standalone warehouse hangar-fuzz$s 2>&1 | grep -E "hangar-fuzz$s:|FATAL" | cut -c1-300
done
rm -f re/scenarios/hangar-fuzz*.yml
echo done

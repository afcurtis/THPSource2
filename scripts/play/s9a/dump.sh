mkdir -p "$HOME/THPSource2/build/s9a"  # outputs stay in build/ (scripts/play/README.md)
cd ~/THPSource2
pkill -f THawk2.nocd.exe
.tools/venv/bin/python -m tony.standalone_boot "$1" --dump "$2" > build/s9a/dump_$1.log 2>&1

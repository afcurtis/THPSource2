mkdir -p "$HOME/THPSource2/build/s9a"  # outputs stay in build/ (scripts/play/README.md)
cd ~/THPSource2
bash scripts/play/s9a/build.sh
tail -1 build/s9a/build.log | grep -q "exit 0" || exit 1
bash scripts/play/s9a/run_all.sh --keep-retail "$@"

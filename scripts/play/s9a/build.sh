mkdir -p "$HOME/THPSource2/build/s9a"  # outputs stay in build/ (scripts/play/README.md)
cd ~/THPSource2
./tony.sh hooks build > build/s9a/build.log 2>&1
echo "exit $?" >> build/s9a/build.log

make full C=clang V=1 -j4 2>clang-build.log
. ./rebuildcount.sh

sudo modprobe pistorm

cp emulator emulator.last

#timeout 30 ./emulator

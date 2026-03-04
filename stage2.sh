mkdir -p build/ppc
cc -O2 -g -Wall -Wextra -Wpedantic -std=c11 -pthread \
-Isrc/ppc -o build/ppc/test_ppc_qemuuae \
src/ppc/test_ppc_qemuuae.c src/ppc/qemu_uae_loader.c -ldl

unset LD_LIBRARY_PATH
export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
export PPC_MODEL=603e
export PPC_RUN_MS=50
export PPC_START_TIMEOUT_MS=2000
./build/ppc/test_ppc_qemuuae
echo $?



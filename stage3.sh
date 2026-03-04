mkdir -p build/ppc
cc -O2 -g -Wall -Wextra -Wpedantic -std=c11 -pthread \
-Isrc/ppc -o build/ppc/test_ppc_mailbox \
src/ppc/test_ppc_mailbox.c src/ppc/qemu_uae_loader.c -ldl

unset LD_LIBRARY_PATH
export QEMU_UAE_SO=/usr/local/lib/qemu-uae.so
export PPC_MODEL=603e
export PPC_START_TIMEOUT_MS=2000
export PPC_MAILBOX_CMD_TIMEOUT_MS=500
export PPC_MAILBOX_LOOPS=100
./build/ppc/test_ppc_mailbox
echo $?


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
export PPC_BENCH=${PPC_BENCH:-0}
export PPC_BENCH_ITERS=${PPC_BENCH_ITERS:-100000}
export PPC_BENCH_WARMUP=${PPC_BENCH_WARMUP:-1000}

if [ -z "${PPC_VERBOSE+x}" ]; then
  if [ "${PPC_BENCH}" = "1" ]; then
    export PPC_VERBOSE=0
  else
    export PPC_VERBOSE=1
  fi
fi

./build/ppc/test_ppc_mailbox
echo $?

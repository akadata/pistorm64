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
export PPC_BENCH_MODE=${PPC_BENCH_MODE:-throughput}
export PPC_BENCH_ITERS=${PPC_BENCH_ITERS:-100000}
export PPC_BENCH_SAMPLES=${PPC_BENCH_SAMPLES:-$PPC_BENCH_ITERS}
export PPC_BENCH_WARMUP=${PPC_BENCH_WARMUP:-1000}
export PPC_BENCH_HISTO=${PPC_BENCH_HISTO:-0}
export PPC_MAILBOX_POLL_SLEEP_MS=${PPC_MAILBOX_POLL_SLEEP_MS:-}

if [ -z "${PPC_VERBOSE+x}" ]; then
  if [ "${PPC_BENCH}" = "1" ]; then
    export PPC_VERBOSE=0
  else
    export PPC_VERBOSE=1
  fi
fi

if [ -z "${PPC_HOST_SERVICE+x}" ]; then
  if [ "${PPC_BENCH}" = "1" ]; then
    export PPC_HOST_SERVICE=0
  else
    export PPC_HOST_SERVICE=1
  fi
fi

if [ -z "${PPC_HOST_SERVICE_TEST+x}" ]; then
  if [ "${PPC_BENCH}" = "1" ]; then
    export PPC_HOST_SERVICE_TEST=0
  else
    export PPC_HOST_SERVICE_TEST=1
  fi
fi

export PPC_HOSTSVC_DOORBELL=${PPC_HOSTSVC_DOORBELL:-0}
export PPC_HOST_SERVICE_CPU=${PPC_HOST_SERVICE_CPU:--1}
export PPC_HOST_SERVICE_SCHED_FIFO=${PPC_HOST_SERVICE_SCHED_FIFO:-0}
export PPC_HOST_SERVICE_SCHED_PRIO=${PPC_HOST_SERVICE_SCHED_PRIO:-10}

./build/ppc/test_ppc_mailbox
echo $?

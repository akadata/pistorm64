# Pi 5 PiStorm GDB run script (extends gdb_pistorm_init.gdb).
#
# Usage:
#   sudo gdb -q -x gdb_pi5_run.gdb --args ./emulator --config basic.cfg
#
# You can override args in the gdb command line; this file sets sane defaults.

source gdb_pistorm_init.gdb

set breakpoint pending on
set print pretty on
set backtrace limit 64
set disassemble-next-line off

handle SIGPIPE nostop noprint pass
handle SIGALRM nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass

# Defaults for Pi 5 bring-up:
# - Let the kernel-provided GPCLK drive GPIO4 (DT overlay + module), don't touch GPCLK via /dev/mem.
# - Don't re-mux GPIO4 (leave it as GPCLK0).
set environment PISTORM_ENABLE_GPCLK 0
set environment PISTORM_RP1_LEAVE_CLK_PIN 1
# Make stalls visible quickly.
set environment PISTORM_TXN_TIMEOUT_US 2000000

python
import gdb

def _pistorm_shell(cmd: str) -> None:
    try:
        gdb.execute(f"shell {cmd}", to_string=False)
    except gdb.error as e:
        gdb.write(f"[ gdb ] shell failed: {e}\n")

def _pistorm_try(cmd: str) -> None:
    try:
        gdb.execute(cmd, to_string=False)
    except gdb.error as e:
        gdb.write(f"[ gdb ] {cmd} failed: {e}\n")

class PistormState(gdb.Command):
    """Print PiStorm protocol state + clk_gp0 debugfs if available."""

    def __init__(self):
        super().__init__("pistorm_state", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        gdb.write("\n[ gdb ] ps_dump_protocol_state(\"gdb\"):\n")
        _pistorm_try('call ps_dump_protocol_state("gdb")')

        gdb.write("\n[ gdb ] clk_gp0 debugfs:\n")
        _pistorm_shell("mount -t debugfs none /sys/kernel/debug 2>/dev/null || true")
        _pistorm_shell(
            "sh -c 'test -d /sys/kernel/debug/clk/clk_gp0 && { "
            "echo -n parent=; cat /sys/kernel/debug/clk/clk_gp0/clk_parent; "
            "echo -n rate=; cat /sys/kernel/debug/clk/clk_gp0/clk_rate; "
            "echo -n enable_count=; cat /sys/kernel/debug/clk/clk_gp0/clk_enable_count 2>/dev/null || true; "
            "} || echo \"clk_gp0 debugfs not available\"'"
        )

PistormState()

class PistormThreads(gdb.Command):
    """Print threads and full backtraces."""

    def __init__(self):
        super().__init__("pistorm_threads", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        gdb.write("\n[ gdb ] threads:\n")
        _pistorm_try("info threads")
        gdb.write("\n[ gdb ] thread backtraces:\n")
        _pistorm_try("thread apply all bt full")

PistormThreads()
end

# When you Ctrl-C, run `pistorm_threads` and `pistorm_state`.
define hook-stop
  printf "\n[ gdb ] stopped. Try: pistorm_threads, pistorm_state\n"
end

break rp1_txn_timeout_fatal
commands
  silent
  printf "\n[ gdb ] rp1_txn_timeout_fatal hit.\n"
  pistorm_state
  pistorm_threads
  continue
end

break ps_pulse_reset
commands
  silent
  printf "\n[ gdb ] ps_pulse_reset()\n"
  continue
end

break m68k_pulse_reset
commands
  silent
  printf "\n[ gdb ] m68k_pulse_reset()\n"
  continue
end

define pistorm_watch_vectors
  # Optional helper: show a few protocol reads at address 0 and 4 (reset vector fetch).
  # Run this after the executable is loaded (e.g. after `start` / before `run`).
  set $pistorm_reads = 0
  break ps_read_16 if ($x0==0 || $x0==4)
  commands
    silent
    set $pistorm_reads = $pistorm_reads + 1
    printf "\n[ gdb ] ps_read_16(0x%08x) hit (%d)\n", (unsigned int)$x0, $pistorm_reads
    if $pistorm_reads <= 10
      bt
    end
    continue
  end
  printf "[ gdb ] Vector watch enabled (ps_read_16 addr 0/4).\n"
end
document pistorm_watch_vectors
Enable a conditional breakpoint on ps_read_16() for reset vector fetches (addr 0x0 and 0x4).
This is defined as a command (not enabled by default) to avoid failures if gdb sources this file
before the executable's symbols are loaded.
end

printf "[ gdb ] Ready. Use `run` to start, Ctrl-C to interrupt.\n"
printf "[ gdb ] After Ctrl-C, run: `pistorm_threads` and `pistorm_state`.\n"
printf "[ gdb ] Note: `pistorm_watch_vectors` is a gdb command; run it as `pistorm_watch_vectors` (not `run pistorm_watch_vectors`).\n"

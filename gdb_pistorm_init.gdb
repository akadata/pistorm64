# Pistorm GDB quick setup (logs all output to ./gdb.log).
set pagination off
set confirm off
set print thread-events off
set detach-on-fork off
set follow-fork-mode child
set logging file gdb.log
set logging overwrite on
set logging enabled on


savedcmd_z3bus.mod := printf '%s\n'   src/z3bus.o | awk '!x[$$0]++ { print("./"$$0) }' > z3bus.mod

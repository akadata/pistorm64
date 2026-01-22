savedcmd_pistorm.mod := printf '%s\n'   src/pistorm.o | awk '!x[$$0]++ { print("./"$$0) }' > pistorm.mod

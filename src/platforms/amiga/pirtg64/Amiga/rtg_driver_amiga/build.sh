#m68k-amigaos-gcc pirtg64.c -m68020 -O2 -o pirtg64.card -noixemul -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostartfiles -lamiga
m68k-amigaos-gcc pirtg64.c -m68040 -O2 -o pirtg64i.card -noixemul -Wall -Wextra -Wno-unused-parameter -fomit-frame-pointer -nostartfiles -lamiga -DIRTG


rg "warning:" clang-build.log | rg -v 'src/musashi/' | rg -v 'src/softfloat/' > core-warnings.log

TOTAL=$(rg "warning:" core-warnings.log | wc -l || echo 0)
PISTORMDEV=$(rg "pistorm-dev.c" core-warnings.log | wc -l || echo 0)
RTG=$(rg "rtg.c" core-warnings.log | wc -l || echo 0)
RTGRAYLIB=$(rg "rtg-output-raylib.c" core-warnings | wc -l || ech0 0)

RTGFGX=$(rg "rtg-gfx.c" core-warnings.log | wc -l || echo 0)
PISCSI=$(rg "piscsi.c" core-warnings.log | wc -l || echo 0)
PIDEV=$(rg "pistorm-dev.c" core-warnings.log | wc -l || echo 0)
PIAHI=$(rg "pi_ahi.c" core-warnings.log | wc -l || echo 0)


echo - total core warnings: $TOTAL
echo - pistorm-dev.c: $PISTORMDEV
echo - rtg.c: $RTG
echo - rtg-output-raylib.c: $RTGRAYLIB

echo - rtg-gfx.c: $RTGFGX
echo - piscsi.c: $PISCSI
echo - pistorm-dev.c: $PIDEV
echo - pi_ahi.c: $PIAHI

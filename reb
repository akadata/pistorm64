make full C=clang V=1 2>clang-build.log
rg "warning:" clang-build.log \
| rg -v 'src/musashi/' \
| rg -v 'src/softfloat/' \
> core-warnings.log


TOTAL=$(rg "warning:" core-warnings.log | wc -l || echo 0)
RTG=$(rg "rtg-gfx.c" core-warnings.log | wc -l || echo 0)
PISCSI=$(rg "piscsi.c" core-warnings.log | wc -l || echo 0)
PIDEV=$(rg "pistorm-dev.c" core-warnings.log | wc -l || echo 0)
PIAHI=$(rg "pi_ahi.c" core-warnings.log | wc -l || echo 0)


echo - total core warnings: $TOTAL
echo - rtg-gfx.c: $RTG
echo - piscsi.c: $PISCSI
echo - pistorm-dev.c: $PIDEV
echo - pi_ahi.c: $PIAHI


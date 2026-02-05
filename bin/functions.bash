rtg_update() {
    make CHIP_RAM_SIZE=0x100000 amiga-rtg || return 1

    cp \
        "$HOME/pistorm64/src/platforms/amiga/rtg/rtg_driver_amiga/pigfx020.card" \
        /opt/pistorm64/data/a314-shared/
}


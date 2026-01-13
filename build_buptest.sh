#gcc buptest.c gpio/ps_protocol.c -I./ -o buptest -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O0

# Allow compile on 64 bit pi os
gcc -O0 -Wall -Wextra -I./ src/buptest/buptest.c src/gpio/ps_protocol.c src/gpio/rpi_peri.c -o buptest




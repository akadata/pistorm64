#ifndef PISTORM_GPIO_PINS_H
#define PISTORM_GPIO_PINS_H

/* PiStorm GPIO pin assignments
 *
 * BCM GPIO numbers, not header pins.
 * This is the single source of truth for the hardware mapping.
 */

#define GPIO_PIN_D0                  0
#define GPIO_PIN_D1                  1
#define GPIO_PIN_D2                  2
#define GPIO_PIN_D3                  3
#define GPIO_PIN_D4                  4
#define GPIO_PIN_D5                  5
#define GPIO_PIN_D6                  6
#define GPIO_PIN_D7                  7

#define GPIO_PIN_A0                  8
#define GPIO_PIN_A1                  9
#define GPIO_PIN_A2                 10
#define GPIO_PIN_A3                 11
#define GPIO_PIN_A4                 12
#define GPIO_PIN_A5                 13
#define GPIO_PIN_A6                 14
#define GPIO_PIN_A7                 15

#define GPIO_PIN_RW                 16
#define GPIO_PIN_AS                 17
#define GPIO_PIN_UDS                18
#define GPIO_PIN_LDS                19

#define GPIO_PIN_RESET              20
#define GPIO_PIN_INT                21
#define GPIO_PIN_DTACK              22
#define GPIO_PIN_TXN_IN_PROGRESS    23

#endif /* PISTORM_GPIO_PINS_H */
#ifndef IODEFS_H
#define IODEFS_H

#include <avr/io.h>

#define PORT(base) (*(&base + 2))
#define DDR(base) (*(&base + 1))
#define PIN(base) (*(&base))

#define LED1_BASE PIND
#define LED1_PIN 5

#define LED2_BASE PINB
#define LED2_PIN 0

/* These macros need to also work with the assembler */
#define CONTROLLER_DATA_PIN PIND
#define CONTROLLER_DATA_PORT PORTD
#define CONTROLLER_DATA_DDR DDRD
/*
 * This must be zero because the shifts in the polling code assume that the
 * data is in the first bit
 */
#define CONTROLLER_DATA_BIT 0

#endif

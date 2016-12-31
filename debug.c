#include <avr/interrupt.h>
#include <avr/io.h>

#include <stdio.h>

#include "debug.h"
#include "iodefs.h"

#define BAUD 9600

void led_init(void)
{
    DDR(LED1_BASE) |= 1<<LED1_PIN;
    DDR(LED2_BASE) |= 1<<LED2_PIN;
}

static int usart_putchar(char c, FILE *stream)
{
    if (c == '\n')
        usart_putchar('\r', stream);

    while (!(UCSR1A & (1<<UDRE1)))
        ;

    UDR1 = c;

    return 0;
}

static char usart_getchar(FILE *stream)
{
    while (!(UCSR1A & (1<<RXC1)))
        ;
    return UDR1;
}

static FILE mystdout = FDEV_SETUP_STREAM(usart_putchar, NULL, _FDEV_SETUP_WRITE);
static FILE mystdin = FDEV_SETUP_STREAM(NULL, usart_getchar, _FDEV_SETUP_READ);

void usart_init(void)
{
    const uint16_t ubbr = 2000000UL / (2 * BAUD) - 1;

    UBRR1H = ubbr >> 8;
    UBRR1L = ubbr;
    UCSR1B = (1<<RXEN1) | (1<<TXEN1);
    /* 8 data bits, 1 stop bit */
    UCSR1C = (1<<UCSZ11) | (1<<UCSZ10);

    DDRD |= 1<<1;

    usart_putchar('A', NULL);
    usart_putchar('\n', NULL);
}

void stdio_init(void)
{
    stdout = &mystdout;
    stdin = &mystdin;
}

void halt(void)
{
    cli();
    for (;;)
        ;
}



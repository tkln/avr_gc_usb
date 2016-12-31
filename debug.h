#ifndef DEBUG_H
#define DEBUG_H

#define LED1_BASE PIND
#define LED1_PIN 5

#define LED2_BASE PINB
#define LED2_PIN 0

#define PORT(base) (*(unsigned char *)(&base + 2))
#define DDR(base) (*(unsigned char *)(&base + 1))
#define PIN(base) (*(unsigned char *)(&base))

void led_init(void);
void usart_init(void);
void stdio_init(void);
void halt(void);

#endif

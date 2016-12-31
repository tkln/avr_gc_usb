#include <avr/io.h>

#include "iodefs.h"

.global controller_probe
.global controller_poll
.global func_test

.macro dhi
    cbi _SFR_IO_ADDR(CONTROLLER_DATA_DDR), 0 /* 2 cycles */
.endm

.macro dlo
    sbi _SFR_IO_ADDR(CONTROLLER_DATA_DDR), 0 /* 2 cycles */
.endm

microsecond:
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    ret

.macro lobit
    dlo
    call microsecond
    call microsecond
    call microsecond
    dhi
    call microsecond
.endm

.macro hibit
    dlo
    call microsecond
    dhi
    call microsecond
    call microsecond
    call microsecond
.endm

controller_probe:
    cli
    lobit
    lobit
    lobit
    lobit
    lobit
    lobit
    lobit
    lobit
    hibit
    sei
    ret

.macro controller_poll_send
    lobit
    hibit
    lobit
    lobit

    lobit
    lobit
    lobit
    lobit

    lobit
    lobit
    lobit
    lobit

    lobit
    lobit
    hibit
    hibit
    
    lobit
    lobit
    lobit
    lobit

    lobit
    lobit
    hibit
    lobit

    hibit
.endm

/* 
 * Clutters r18 and r19.
 * The bit is read into r19.
 * takes 4 cycles in total
 */
.macro controller_poll_recv_bit
    /* Get the port state. The data is in the last bit */
    lsl r19                     /* 1, 1 / 16 */
    in r18, _SFR_IO_ADDR(CONTROLLER_DATA_PIN)  /* 1, 2 / 16 */
    andi r18, 1                 /* 1, 3 / 16 */
    or r19, r18                 /* 1, 4 / 16 */
.endm

controller_poll_recv_bit_f:     /* total 9 */
    controller_poll_recv_bit    /* 4 */
    ret                         /* 5 */

.macro nop4
    nop
    nop
    nop
    nop
.endm

/* Assumes r19 zero
 * Assumes that X points to the buffer  */
.macro controller_poll_recv_byte
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    nop4
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    nop4
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    nop4
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    nop4
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    nop4
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    nop4
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    nop4
    call controller_poll_recv_bit_f /* 9 + 5,   4 */
    st X+, r19                  /* 2,   6 */
    ldi r19, 0                  /* 1,   7 */
    nop
.endm

.macro controller_poll_recv
    ldi r19, 0

    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte

    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte

    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte

    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte

    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
    controller_poll_recv_byte
.endm

controller_poll:
    cli

    /* Move the first function parameter (buffer pointer) to X(r27, r26) */
    mov r27, r25 
    mov r26, r24 

    controller_poll_send

    ldi r19, 0xff
hiloop:
    dec r19
    breq poll_timeout
    in r18, _SFR_IO_ADDR(CONTROLLER_DATA_PIN)
    andi r18, 1
    brne hiloop

poll_timeout:

    controller_poll_recv

    sei
    ret

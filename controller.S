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

microsecond_nop:
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
    call microsecond_nop
    call microsecond_nop
    call microsecond_nop
    dhi
    call microsecond_nop
.endm

.macro hibit
    dlo
    call microsecond_nop
    dhi
    call microsecond_nop
    call microsecond_nop
    call microsecond_nop
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
 * Clutters r18 and r19
 * The bit is read into r19
 * Takes 4 cycles in total
 */
.macro controller_poll_recv_bit
    /* Get the port state. The data is in the last bit */
    lsl r19
    in r18, _SFR_IO_ADDR(CONTROLLER_DATA_PIN)
    andi r18, 1
    or r19, r18
.endm

/* Takes 9 cycles in total */
controller_poll_recv_bit_f:
    controller_poll_recv_bit    /* 4 */
    ret                         /* 5 */

.macro nop4
    nop
    nop
    nop
    nop
.endm

/*
 * Assumes r19 zero
 * Assumes that X points to the buffer
 */
.macro controller_poll_recv_byte
    /*
     * Assuming F_CPU = 16 MHz, each bit from the controller must be
     * sampled in 16 cycles, exactly
     */
    call controller_poll_recv_bit_f /* 5 + 9 / 16 cycles */
    nop4                            /* 16 / 16 */
    call controller_poll_recv_bit_f
    nop4
    call controller_poll_recv_bit_f
    nop4
    call controller_poll_recv_bit_f
    nop4
    call controller_poll_recv_bit_f
    nop4
    call controller_poll_recv_bit_f
    nop4
    call controller_poll_recv_bit_f
    nop4
    call controller_poll_recv_bit_f /* 5 + 9 / 16 */
    st X+, r19                      /* 14 / 16 */
    ldi r19, 0                      /* 15 / 16 */
    nop                             /* 16 / 16 */
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

#include <avr/io.h>

#include "iodefs.h"

.global controller_probe
.global controller_poll
.global func_test

.macro nopn n
.if \n
    nopn"(\n - 1)"
    nop
.endif
.endm

/* 4 + 6 + 4 = 14 */
microsecond_nop:
    nopn(6)
    ret

hibit:
    sbi _SFR_IO_ADDR(CONTROLLER_DATA_DDR), 0    /* 2, 2 / 16 */
    call microsecond_nop                        /* 14, 16 / 16 */
    cbi _SFR_IO_ADDR(CONTROLLER_DATA_DDR), 0    /* 2, 2 / 48 */
    call microsecond_nop                        /* 14, 16 / 48 */
    call microsecond_nop                        /* 14, 32 / 48 */
    nopn(8)                                     /* 8, 40 / 48 */
    ret                                         /* 4 + 4, 48 / 48 */

lobit:
    sbi _SFR_IO_ADDR(CONTROLLER_DATA_DDR), 0 /* 2 cycles */
    call microsecond_nop
    call microsecond_nop
    call microsecond_nop
    cbi _SFR_IO_ADDR(CONTROLLER_DATA_DDR), 0 /* 2 cycles */
    nopn(8)
    ret

controller_probe:
    cli
    call lobit
    call lobit
    call lobit
    call lobit
    call lobit
    call lobit
    call lobit
    call lobit
    call lobit
    call hibit
    sei
    ret

.macro controller_poll_send
    call lobit
    call hibit
    call lobit
    call lobit

    call lobit
    call lobit
    call lobit
    call lobit

    call lobit
    call lobit
    call lobit
    call lobit

    call lobit
    call lobit
    call hibit
    call hibit
    
    call lobit
    call lobit
    call lobit
    call lobit

    call lobit
    call lobit
    call hibit
    call lobit

    call hibit
.endm

/* 
 * Clutters r18 and r19
 * The bit is read into r19
 * Takes 4 cycles in total
 */
.macro controller_poll_recv_bit
    /* Get the port state. The data is in the last bit */
    lsl r19                                     /* 1, 1 */
    in r18, _SFR_IO_ADDR(CONTROLLER_DATA_PIN)   /* 1, 2 */
    andi r18, 1                                 /* 1, 3 */
    or r19, r18                                 /* 1, 4*/
.endm

/* Takes 12 cycles in total */
controller_poll_recv_bit_f:
    controller_poll_recv_bit    /* 4, 4 / 16 */
    nopn(4)                     /* 4, 8 / 16 */
    ret                         /* 4, 12 / 16 */

.macro controller_poll_recv
    ldi r19, 0
controller_poll_recv_loop:
    /*
     * Assuming F_CPU = 16 MHz, each bit from the controller must be
     * sampled in 16 cycles, exactly
     */
    call controller_poll_recv_bit_f /* 4 + 12 = 16 cycles */
    call controller_poll_recv_bit_f
    call controller_poll_recv_bit_f
    call controller_poll_recv_bit_f
    call controller_poll_recv_bit_f
    call controller_poll_recv_bit_f
    call controller_poll_recv_bit_f
    controller_poll_recv_bit        /* 4,       4 / 16 */
    st X+, r19                      /* 2,       6 / 16 */
    ldi r19, 0                      /* 1,       7 / 16 */
    nopn(6)                         /* 6,       13 / 16 */
    dec r20                         /* 1,       14 / 16 */
    brne controller_poll_recv_loop  /* 2(1),    16 / 16 */
.endm

controller_poll:
    cli

    /* Move the first function parameter (buffer pointer) to X(r27, r26) */
    mov r27, r25 
    mov r26, r24 
    /* Second parateter is the buffer length */
    mov r20, r23

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

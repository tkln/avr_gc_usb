#include <avr/io.h>

.global controller_probe
.global controller_poll
.global func_test

.macro push_sreg
    push r16
    in r16, _SFR_IO_ADDR(SREG)
    push r16
.endm

.macro pop_sreg
    pop r16
    out _SFR_IO_ADDR(SREG), r16
    pop r16
.endm

.macro dhi
    cbi _SFR_IO_ADDR(DDRD), 0 /* 2 cycles */
.endm

.macro dlo
    sbi _SFR_IO_ADDR(DDRD), 0 /* 2 cycles */
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
    push_sreg
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
    pop_sreg
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

/* Clutters r16 and r17.
 * The bit is read into r17.
 * takes 4 cycles in total
 */
.macro controller_poll_recv_bit
    /* Get the port state. The data is in the last bit */
    lsl r17                     /* 1, 1 / 16 */
    in r16, _SFR_IO_ADDR(PIND)  /* 1, 2 / 16 */
    andi r16, 1                 /* 1, 3 / 16 */
    or r17, r16                 /* 1, 4 / 16 */
.endm

controller_poll_recv_bit_f:     /* total 9 */
    controller_poll_recv_bit    /* 4 */
    ret                         /* 5 */

.macro nop12
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.endm

.macro nop9
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.endm

.macro nop4
    nop
    nop
    nop
    nop
.endm

/* Assumes r17 zero
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
    st X+, r17                  /* 2,   6 */
    ldi r17, 0                  /* 1,   7 */
    nop
.endm

.macro controller_poll_recv
    ldi r17, 0 

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
    push_sreg
    push r17
    push r26
    push r27

    cli

    /* Move the first function parameter (buffer pointer) to X(r27, r26) */
    mov r27, r25 
    mov r26, r24 

    controller_poll_send

    ldi r17, 0xff
hiloop:
    dec r17
    breq poll_timeout
    in r16, _SFR_IO_ADDR(PIND)
    andi r16, 1
    brne hiloop

poll_timeout:

    controller_poll_recv

    pop r27
    pop r26
    pop r17
    pop_sreg
    ret

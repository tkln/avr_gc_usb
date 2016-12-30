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

.macro microsecond
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
    nop
    nop
    nop
    //nop
.endm

.macro lobit
    dlo
    microsecond
    microsecond
    microsecond
    dhi
    microsecond
.endm

.macro hibit
    dlo
    microsecond
    dhi
    microsecond
    microsecond
    microsecond
.endm

controller_probe:
    push_sreg
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
    lsl r17                     /* 1, 4 / 16 */
    in r16, _SFR_IO_ADDR(PIND)  /* 1, 1 / 16 */
    andi r16, 1                 /* 1, 2 / 16 */
    or r17, r16                 /* 1, 3 / 16 */
.endm

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

/* Assumes r17 zero
 * Assumes that X points to the buffer  */
.macro controller_poll_recv_byte
    controller_poll_recv_bit    /* 4,   4 */
    nop12                       /* 12,  16 */
    controller_poll_recv_bit    /* 4,   4 */
    nop12                       /* 12,  16 */
    controller_poll_recv_bit    /* 4,   4 */
    nop12                       /* 12,  16 */
    controller_poll_recv_bit    /* 4,   4 */
    nop12                       /* 12,  16 */
    controller_poll_recv_bit    /* 4,   4 */
    nop12                       /* 12,  16 */
    controller_poll_recv_bit    /* 4,   4 */
    nop12                       /* 12,  16 */
    controller_poll_recv_bit    /* 4,   4 */
    nop12                       /* 12,  16 */
    controller_poll_recv_bit    /* 4,   4 */
    /* At this point we have 12 = 16 - 4 cycles to spend */
    st X+, r17                  /* 2,   6 */
    ldi r17, 0                  /* 1,   7 */
    nop9                        /* 9,   16 */
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

    /* Move the first function parameter (buffer pointer) to X(r27, r26) */
    mov r27, r25 
    mov r26, r24 

    cli

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

    sei
    pop r27
    pop r26
    pop r17
    pop_sreg
    ret

func_test:
    push_sreg
    push r26
    push r27

    mov r27, r25 
    mov r26, r24 
    ldi r16, 0xff
    st x+, r16
    ldi r16, 0x55
    st x, r16

    pop r27
    pop r26
    pop_sreg
    ret

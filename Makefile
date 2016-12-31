PROJECT=avrusb
OBJS=avrusb.o gc_bang.o debug.o
SOURCES=avrusb.c
MMCU=atmega32u4
AVRDUDE=avrdude
PROGRAMMER=avr109
PORT=/dev/ttyACM0
BAUDRATE=57600
F_CPU=16000000

CFLAGS=-mmcu=$(MMCU) -Wall -Os -std=gnu99 -DF_CPU=$(F_CPU)UL

$(PROJECT).hex: $(PROJECT).out
	avr-objcopy -j .text -j .data -O ihex $(PROJECT).out $(PROJECT).hex

$(PROJECT).out: $(OBJS)
	avr-gcc $(CFLAGS) $^ -o $@

%.o: %.c
	avr-gcc $(CFLAGS) -c $< -o $@

%.o: %.S
	avr-gcc $(CFLAGS) -c $< -o $@

#$(PROJECT).out: $(SOURCES) 
#	avr-gcc $(CFLAGS) -I./ -o $(PROJECT).out $(SOURCES) -DF_CPU=$(F_CPU)UL


program: $(PROJECT).hex
	$(AVRDUDE) -c$(PROGRAMMER)  -p$(MMCU) -U flash:w:$(PROJECT).hex -P$(PORT) -b $(BAUDRATE)

clean:
	rm -f $(PROJECT){.out,.hex}

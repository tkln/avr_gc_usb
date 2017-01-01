PROJECT = avrgcusb
OBJS += main.o controller.o debug.o
MCU = atmega32u4
PROGRAMMER ?= avr109
PORT ?= /dev/ttyACM0
BAUDRATE ?= 57600
F_CPU ?= 16000000

CFLAGS += -mmcu=$(MCU) -Wall -Os -std=gnu99 -DF_CPU=$(F_CPU)UL

$(PROJECT).hex: $(PROJECT).out
	avr-objcopy -j .text -j .data -O ihex $(PROJECT).out $(PROJECT).hex

$(PROJECT).out: $(OBJS)
	avr-gcc $(CFLAGS) $^ -o $@

%.o: %.c
	avr-gcc $(CFLAGS) -c $< -o $@

%.o: %.S
	avr-gcc $(CFLAGS) -c $< -o $@

flash: $(PROJECT).hex
	avrdude -c$(PROGRAMMER)  -p$(MCU) -U flash:w:$(PROJECT).hex -P$(PORT) -b $(BAUDRATE)

clean:
	rm -f $(PROJECT){.out,.hex} $(OBJS)

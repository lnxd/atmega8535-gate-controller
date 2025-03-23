# === Project Configuration ===
MCU     = atmega8535
F_CPU   = 8000000UL
TARGET  = gate_controller
FORMAT  = ihex

# === Directory Setup ===
SRC_DIR = src
OBJ_DIR = build
DEP_DIR = dep

# === File Discovery ===
SRC     = $(wildcard $(SRC_DIR)/*.c)
OBJ     = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))
DEP     = $(patsubst $(SRC_DIR)/%.c, $(DEP_DIR)/%.d, $(SRC))

# === Tools ===
CC       = avr-gcc
OBJCOPY  = avr-objcopy
OBJDUMP  = avr-objdump
SIZE     = avr-size
NM       = avr-nm
AVRDUDE  = avrdude
REMOVE   = rm -f
REMOVEDIR= rm -rf
MKDIR    = mkdir -p

# === Flags ===
CFLAGS   = -Wall -Os -mmcu=$(MCU) -DF_CPU=$(F_CPU) -std=gnu99 \
           -funsigned-char -funsigned-bitfields -fpack-struct -fshort-enums \
           -Iinclude \
           -MD -MP -MF $(DEP_DIR)/$(@F).d
LDFLAGS  = -Wl,-Map=$(TARGET).map

# === Programmer Config ===
PROGRAMMER      = usbasp
PROGRAMMER_ARGS = -c $(PROGRAMMER) -p $(MCU)

# === Build Targets ===
all: $(TARGET).hex

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(MKDIR) $(OBJ_DIR) $(DEP_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(SIZE) --format=avr --mcu=$(MCU) $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O $(FORMAT) -R .eeprom $< $@

$(TARGET).eep: $(TARGET).elf
	$(OBJCOPY) -j .eeprom --set-section-flags=.eeprom="alloc,load" \
	--change-section-lma .eeprom=0 -O $(FORMAT) $< $@

$(TARGET).lss: $(TARGET).elf
	$(OBJDUMP) -h -S $< > $@

$(TARGET).sym: $(TARGET).elf
	$(NM) -n $< > $@

# === Flash ===
upload: $(TARGET).hex
	$(AVRDUDE) $(PROGRAMMER_ARGS) -U flash:w:$<:i

# === Clean ===
clean:
	$(REMOVE) $(TARGET).hex $(TARGET).eep $(TARGET).elf $(TARGET).map
	$(REMOVE) $(TARGET).lss $(TARGET).sym
	$(REMOVEDIR) $(OBJ_DIR) $(DEP_DIR)

# === Dependencies ===
-include $(DEP)

.PHONY: all clean upload

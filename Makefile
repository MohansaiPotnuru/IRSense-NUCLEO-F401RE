# IRSense firmware - command-line build (same sources as the STM32CubeIDE project).
#
#   make            build build/Mohan_Sai.{elf,hex,bin}
#   make test       build and run the host unit tests (needs a native gcc)
#   make size       print section sizes
#   make clean
#
# Needs arm-none-eabi-gcc on PATH (or PREFIX=/path/to/arm-none-eabi-).
# STM32CubeIDE ships one under
#   <CubeIDE>/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin

TARGET  := Mohan_Sai
BUILD   ?= build
PREFIX  ?= arm-none-eabi-

CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)

MCU  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS := -DUSE_HAL_DRIVER -DSTM32F401xE -DFW_GIT_HASH=\"$(GIT_HASH)\"
INCS := -ICore/Inc \
        -IDrivers/STM32F4xx_HAL_Driver/Inc \
        -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
        -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
        -IDrivers/CMSIS/Include

C_SRCS := $(wildcard Core/Src/*.c) \
          $(wildcard Core/Src/app/*.c) \
          $(wildcard Core/Src/bsp/*.c) \
          $(wildcard Drivers/STM32F4xx_HAL_Driver/Src/*.c)
S_SRCS := Core/Startup/startup_stm32f401retx.s

OBJS := $(addprefix $(BUILD)/,$(C_SRCS:.c=.o) $(S_SRCS:.s=.o))
DEPS := $(OBJS:.o=.d)

CFLAGS  := $(MCU) $(DEFS) $(INCS) -std=gnu11 -Os -g3 -Wall \
           -ffunction-sections -fdata-sections -fstack-usage -MMD -MP
# Our own code is held to a stricter standard than the vendor HAL.
STRICT  := -Wextra -Wshadow -Wdouble-promotion -Wformat=2 -Wundef -Werror
ASFLAGS := $(MCU) -x assembler-with-cpp -g3
LDFLAGS := $(MCU) -TSTM32F401RETX_FLASH.ld --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage \
           -Wl,--start-group -lc -lm -Wl,--end-group

.PHONY: all clean size test

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/Core/Src/app/%.o: CFLAGS += $(STRICT)
$(BUILD)/Core/Src/bsp/%.o: CFLAGS += $(STRICT)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) STM32F401RETX_FLASH.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

test:
	$(MAKE) -C tests

clean:
	rm -rf $(BUILD)
	$(MAKE) -C tests clean

-include $(DEPS)

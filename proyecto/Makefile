# Makefile — TP3 Embebidos / Blue Pill (STM32F103C8T6) con libopencm3
# Toolchain: arm-none-eabi-*   Build: make   Flash: OpenOCD + ST-Link
#
# Estructura del repo:
#   tp3-embebidos-mariani/
#     ├── bin/              (artefactos generados, compartidos con la entrega)
#     ├── evidencia/
#     ├── informe/
#     └── proyecto/
#         ├── Makefile      (este archivo)
#         └── src/main.c
#   bluepill-libopencm3/    (dos niveles arriba de proyecto/)
#     ├── common/linker.ld
#     └── libopencm3/       (libreria + headers)
#
# El startup / vector table los provee libopencm3, no usamos startup.s propio.

# ---- Toolchain ----
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size
OBJDUMP = arm-none-eabi-objdump

# ---- OpenOCD (ruta absoluta para evitar conflicto con versión ESP32) ----
OPENOCD = /usr/bin/openocd

# ---- CPUTAPID (valor reportado por esta Blue Pill via OpenOCD/ST-Link) ----
CPUTAPID = 0x1ba01477

# ---- Proyecto ----
TARGET    = firmware
BUILD_DIR = ../bin
SRC_DIR   = src

C_SRCS   = $(SRC_DIR)/main.c $(SRC_DIR)/lcd_i2c.c

# ---- Rutas a libopencm3 (2 niveles arriba: proyecto/ -> tp3-embebidos-mariani/ -> BLUEPILL/) ----
LIBOPENCM3_DIR = ../../bluepill-libopencm3/libopencm3
COMMON_DIR     = ../../bluepill-libopencm3/common
LDSCRIPT       = $(COMMON_DIR)/linker.ld
LIBOPENCM3_A   = $(LIBOPENCM3_DIR)/lib/libopencm3_stm32f1.a

# ---- Flags de compilación ----
CFLAGS  = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
CFLAGS += -Wall -Wextra -g -O0
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -MMD -MP                       # dependencias automaticas de headers
CFLAGS += -DSTM32F1                      # familia, requerido por libopencm3
CFLAGS += -I$(LIBOPENCM3_DIR)/include
CFLAGS += -I$(COMMON_DIR)
CFLAGS += -I$(SRC_DIR)

# ---- Flags de enlazado ----
# --specs=nano.specs => newlib-nano (snprintf liviano)
# --specs=nosys.specs => stubs vacios para syscalls (no hay SO)
LDFLAGS  = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
LDFLAGS += -T$(LDSCRIPT) -nostartfiles
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map
LDFLAGS += --specs=nano.specs --specs=nosys.specs

LDLIBS   = -L$(LIBOPENCM3_DIR)/lib -lopencm3_stm32f1 -lc -lgcc -lnosys

# ---- Derivar objetos en bin/ (aplana la jerarquia: src/main.c -> bin/main.o) ----
OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(C_SRCS:.c=.o)))

.DEFAULT_GOAL := all

# Incluir dependencias de headers (si existen)
-include $(OBJS:.o=.d)

# ================================================================
# Targets
# ================================================================

.PHONY: all clean flash size disasm inspect symbols openocd gdb libopencm3 help

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).hex size

# ---- Enlazado ----
# El .a se lista como prereq para forzar build de libopencm3 si falta.
$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LIBOPENCM3_A) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# ---- Compilación C ----
# Regla aplanada: cualquier .c bajo src/ -> bin/<nombre>.o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- Crear carpeta bin si no existe ----
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ---- Compilar libopencm3 si falta el .a ----
$(LIBOPENCM3_A):
	$(MAKE) -C $(LIBOPENCM3_DIR) TARGETS=stm32/f1

# Target manual por si queres forzar el rebuild de la libreria.
libopencm3:
	$(MAKE) -C $(LIBOPENCM3_DIR) TARGETS=stm32/f1

# ---- Conversiones de formato (para flasheadores que no usen ELF) ----
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

# ---- Reporte de tamaño (Flash/SRAM) ----
size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

# ---- Desensamblado rápido ----
disasm: $(BUILD_DIR)/$(TARGET).elf
	$(OBJDUMP) -d $<

# ---- Inspección de ELF (headers + secciones + VMA/LMA) ----
inspect: $(BUILD_DIR)/$(TARGET).elf
	arm-none-eabi-readelf -h $<
	arm-none-eabi-readelf -S $<
	arm-none-eabi-objdump -h $<

# ---- Símbolos definidos ----
symbols: $(BUILD_DIR)/$(TARGET).elf
	arm-none-eabi-nm --defined-only $< | head -20

# ---- Programar la Blue Pill via ST-Link + OpenOCD ----
# Nota: ejecutar con sudo (en WSL2 udev no gestiona permisos USB)
#   sudo make flash
flash: $(BUILD_DIR)/$(TARGET).elf
	$(OPENOCD) -f interface/stlink.cfg \
	           -c "set CPUTAPID $(CPUTAPID)" \
	           -f target/stm32f1x.cfg \
	           -c "program $< verify reset exit"

# ---- Sesion OpenOCD interactiva (para debug con gdb en otra terminal) ----
openocd:
	$(OPENOCD) -f interface/stlink.cfg \
	           -c "set CPUTAPID $(CPUTAPID)" \
	           -f target/stm32f1x.cfg

# ---- GDB conectado al server OpenOCD ----
gdb: $(BUILD_DIR)/$(TARGET).elf
	gdb-multiarch $< -ex "target remote localhost:3333"

# ---- Limpiar artefactos del proyecto (no toca libopencm3) ----
clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets disponibles:"
	@echo "  make           -> compila firmware.elf/.bin/.hex y muestra size"
	@echo "  make flash     -> programa la Blue Pill (sudo make flash en WSL2)"
	@echo "  make openocd   -> levanta servidor OpenOCD"
	@echo "  make gdb       -> conecta gdb-multiarch a localhost:3333"
	@echo "  make size      -> reporte de uso de Flash/SRAM"
	@echo "  make disasm    -> desensamblado del .elf"
	@echo "  make inspect   -> headers ELF + tabla de secciones"
	@echo "  make symbols   -> primeros 20 simbolos definidos"
	@echo "  make libopencm3-> fuerza rebuild de la libreria (familia f1)"
	@echo "  make clean     -> borra ../bin/ (artefactos)"

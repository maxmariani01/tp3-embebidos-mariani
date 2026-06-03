# TP3 — Comunicación I2C y Display LCD en STM32F103

**Sistemas Embebidos — Ingeniería Electrónica**
**Integrantes:**
- Maximo Mariani — LU 1151848
- Santiago Bolanca — LU 1176307

**Docente:** Dr. Ing. Leonardo J. Amet
**Plataforma:** STM32F103C8T6 Blue Pill
**Toolchain:** `arm-none-eabi-gcc` + libopencm3

## Resumen

Este TP integra comunicación I2C y un display LCD 16x2 al sistema desarrollado
en el TP2. El firmware combina ADC, PWM por hardware, temporizadores,
interrupciones externas, anti-rebote por software y un driver propio para LCD
HD44780 mediante expansor PCF8574.

- **ADC1 CH0 (PA0):** muestrea un potenciómetro de 10 kΩ.
- **TIM2 CH2 (PA1):** genera PWM por hardware a 1 kHz para controlar el brillo
  de un LED externo.
- **TIM3:** genera una interrupción cada 50 ms para muestreo, anti-rebote y
  temporización del refresco del LCD.
- **EXTI1 (PB1):** detecta el pulsador y permite ciclar tres modos:
  `DIRECTO`, `INVERSO` y `FIJO 50`.
- **I2C1 (PB6/PB7):** comunica la Blue Pill con el backpack PCF8574 del LCD.
- **LCD 16x2:** muestra en tiempo real el modo activo, el ADC, la tensión o el
  duty calculado.
- **USART1 TX (PA9):** imprime mensajes de estado a 115200 8N1.

La entrega final corresponde a la **Etapa 3: display dinámico integrado**.

## Estructura actual

La entrega está organizada así:

```text
BLUEPILL/
├── bluepill-libopencm3/
│   ├── common/
│   │   └── linker.ld
│   └── libopencm3/
└── tp3-embebidos-mariani/
    ├── README.md
    ├── .gitignore
    ├── evidencia/
    │   ├── build/
    │   │   └── build_completo.log
    │   ├── capturas+video/
    │   │   ├── Barrido-I2C.png
    │   │   ├── I2C_LCD_OK.JPG
    │   │   ├── MODO_DIRECTO.JPEG
    │   │   ├── MODO_INVERSO.JPEG
    │   │   ├── MODO_FIJO50%.JPEG
    │   │   ├── Medicion_Multimetro.JPEG
    │   │   └── Video_Funcionamiento.MP4
    │   └── conexionado/
    │       └── Conexionado.JPEG
    ├── informe/
    │   ├── Informe_TP3_Mariani-Bolanca.docx
    │   └── Informe_TP3_Mariani-Bolanca.pdf
    └── proyecto/
        ├── Makefile
        └── src/
            ├── lcd_i2c.c
            ├── lcd_i2c.h
            └── main.c
```

Notas:

- `proyecto/` contiene el código fuente y el `Makefile`.
- `proyecto/src/main.c` integra ADC, PWM, EXTI, TIM3, SysTick, UART e I2C.
- `proyecto/src/lcd_i2c.c` y `lcd_i2c.h` implementan el driver del LCD 16x2.
- `bin/` contiene artefactos generados por compilación y queda ignorado por Git.
- `evidencia/build/` contiene el log completo de compilación.
- `evidencia/capturas+video/` contiene capturas de los modos y video.
- `evidencia/conexionado/` contiene la foto del armado.
- `informe/` contiene el informe final en PDF y DOCX.

## Prerrequisitos

- `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, `arm-none-eabi-size` y `make`
  disponibles en el `PATH`.
- `bluepill-libopencm3` ubicado como carpeta hermana de
  `tp3-embebidos-mariani`, es decir:

```text
~/BLUEPILL/bluepill-libopencm3/
~/BLUEPILL/tp3-embebidos-mariani/
```

- OpenOCD disponible en `/usr/bin/openocd`.
- ST-Link V2 conectado a la Blue Pill por SWD.
- Adaptador USB-Serial conectado a `PA9` y `GND`.
- En WSL2, los dispositivos USB deben estar adjuntos con `usbipd`.
- LCD 16x2 con backpack PCF8574 conectado a I2C1.

## Compilación

Desde la carpeta del proyecto:

```bash
cd ~/BLUEPILL/tp3-embebidos-mariani/proyecto
make clean
make
```

El `Makefile` genera los artefactos en `../bin/`:

- `firmware.elf`
- `firmware.bin`
- `firmware.hex`
- `firmware.map`
- `main.o`
- `main.d`
- `lcd_i2c.o`
- `lcd_i2c.d`

Para guardar la salida completa del build dentro de la carpeta de evidencia:

```bash
cd ~/BLUEPILL/tp3-embebidos-mariani/proyecto
mkdir -p ../evidencia/build
make 2>&1 | tee ../evidencia/build/build_completo.log
```

Salida registrada actualmente:

```text
   text    data     bss     dec     hex filename
   9561      96      28    9685    25d5 ../bin/firmware.elf
```

## Flash

El `Makefile` usa OpenOCD con ST-Link y define el `CPUTAPID` para la placa:

```make
CPUTAPID = 0x1ba01477
```

Para programar la Blue Pill:

```bash
cd ~/BLUEPILL/tp3-embebidos-mariani/proyecto
sudo make flash
```

En WSL2, antes de flashear hay que adjuntar el ST-Link desde PowerShell
administrador:

```powershell
usbipd list
usbipd attach --wsl --busid <BUSID>
```

## Monitor serie

Conectar el adaptador USB-Serial:

| Adaptador USB-Serial | Blue Pill |
|---|---|
| RX | PA9 |
| GND | GND |

Luego abrir la consola:

```bash
picocom -b 115200 /dev/ttyUSB0
```

Para salir de `picocom`: `Ctrl+A` y luego `Ctrl+X`.

## Conexionado

| Pin Blue Pill | Conecta a | Función |
|---|---|---|
| `PA0` | Centro del potenciómetro 10 kΩ | ADC1 IN0 |
| `PA1` | Resistencia 220 Ω → ánodo LED | TIM2 CH2 PWM |
| `PA9` | RX del adaptador USB-Serial | USART1 TX |
| `PB1` | Pulsador a 3.3 V + resistencia 10 kΩ pull-down a GND | EXTI1 |
| `PB6` | SCL del backpack PCF8574 + pull-up 4.7 kΩ a 3.3 V | I2C1 SCL |
| `PB7` | SDA del backpack PCF8574 + pull-up 4.7 kΩ a 3.3 V | I2C1 SDA |
| `3.3V` | Potenciómetro, pulsador y pull-ups I2C | Alimentación |
| `GND` | Potenciómetro, LED, pulsador, USB-Serial y LCD | Referencia común |

La evidencia del conexionado está en:

```text
evidencia/conexionado/Conexionado.JPEG
```

## LCD e I2C

El scanner de la Etapa 1 detectó el backpack del LCD en dirección I2C de 7 bits
`0x27`. El driver asume el mapeo estándar del PCF8574:

```text
P0=RS  P1=RW  P2=EN  P3=BL  P4=D4  P5=D5  P6=D6  P7=D7
```

El LCD se inicializa como HD44780 en modo 4 bits, 2 líneas, display encendido,
cursor apagado y autoincremento de posición. Las escrituras al PCF8574 se hacen
con `i2c_transfer7()` de libopencm3.

Para el refresco dinámico se usa `lcd_print_line()`, que escribe siempre 16
caracteres por fila y rellena con espacios. Esto evita usar `lcd_clear()` en
cada actualización y reduce el parpadeo.

## Verificación funcional

Al iniciar, la UART imprime:

```text
=== TP3 - ETAPA 3: display dinamico ===
```

Luego informa el modo activo cuando cambia:

```text
>>> MODO: DIRECTO <<<
>>> MODO: INVERSO <<<
>>> MODO: FIJO 50 <<<
```

Pruebas esperadas:

- Al girar el potenciómetro, el brillo del LED varía de forma continua.
- En modo `DIRECTO`, mayor ADC implica mayor duty.
- En modo `INVERSO`, mayor ADC implica menor duty.
- En modo `FIJO 50`, el duty queda fijo en 50 %.
- Al presionar el pulsador en `PB1`, el sistema cicla:
  `DIRECTO → INVERSO → FIJO 50 → DIRECTO`.
- El LCD se actualiza cada 100 ms sin limpiar toda la pantalla.
- En modo `DIRECTO`, el LCD muestra el modo, el valor ADC y la tensión estimada.
- En modo `INVERSO`, el LCD muestra el modo y el duty calculado.
- En modo `FIJO 50`, el LCD muestra el modo fijo.

## Evidencia incluida

```text
evidencia/
├── build/
│   └── build_completo.log
├── capturas+video/
│   ├── Barrido-I2C.png
│   ├── I2C_LCD_OK.JPG
│   ├── MODO_DIRECTO.JPEG
│   ├── MODO_INVERSO.JPEG
│   ├── MODO_FIJO50%.JPEG
│   ├── Medicion_Multimetro.JPEG
│   └── Video_Funcionamiento.MP4
└── conexionado/
    └── Conexionado.JPEG
```

Además, el informe final se entrega en:

```text
informe/
├── Informe_TP3_Mariani-Bolanca.docx
└── Informe_TP3_Mariani-Bolanca.pdf
```

## Targets útiles del Makefile

| Comando | Descripción |
|---|---|
| `make` | Compila `firmware.elf`, `firmware.bin`, `firmware.hex` y muestra `size`. |
| `make clean` | Borra `../bin/`. |
| `make flash` | Programa la placa usando OpenOCD y ST-Link. |
| `make size` | Muestra uso de Flash/SRAM. |
| `make disasm` | Muestra el desensamblado del ELF. |
| `make inspect` | Muestra headers y secciones del ELF. |
| `make symbols` | Lista los primeros símbolos definidos. |
| `make openocd` | Inicia OpenOCD para debug. |
| `make gdb` | Conecta `gdb-multiarch` a OpenOCD. |
| `make libopencm3` | Fuerza rebuild de libopencm3 para STM32F1. |

## Entorno utilizado

- WSL2 Ubuntu sobre Windows
- VSCode
- `usbipd-win` para adjuntar ST-Link y USB-Serial a WSL2
- `picocom` como monitor serie
- OpenOCD + ST-Link para programación

/******************************************************************************
 *  lcd_i2c.c - Driver para LCD 16x2 (HD44780) via expansor I2C PCF8574
 *  TP3 - Sistemas Embebidos - STM32F103C8T6 + libopencm3
 *
 *  ARQUITECTURA DEL DRIVER (de abajo hacia arriba):
 *
 *    lcd_write_string / lcd_set_cursor / lcd_clear   <- API publica
 *            |
 *    lcd_send_byte(valor, modo)        <- parte el byte en 2 nibbles
 *            |
 *    lcd_send_nibble(nibble, modo)     <- arma el byte del PCF8574 y pulsa EN
 *            |
 *    pcf8574_write(byte)               <- una escritura I2C cruda
 *
 *  El "modo" indica si lo que se envia es un COMANDO (RS=0) o un DATO (RS=1).
 *
 *  MAPEO PCF8574 -> HD44780 (estandar de los backpacks):
 *    P0=RS  P1=RW  P2=EN  P3=BL  P4=D4  P5=D5  P6=D6  P7=D7
 *****************************************************************************/

#include "lcd_i2c.h"

#include <libopencm3/stm32/i2c.h>

/* --- Delay en milisegundos provisto por el main (SysTick) --- */
extern void delay_ms(uint32_t ms);

/* --- Bits de control en el byte del PCF8574 --- */
#define LCD_RS   0x01U   /* P0: Register Select (0=comando, 1=dato)   */
#define LCD_RW   0x02U   /* P1: Read/Write (siempre 0 = escritura)    */
#define LCD_EN   0x04U   /* P2: Enable (el flanco de bajada latcha)   */
#define LCD_BL   0x08U   /* P3: Backlight (1 = encendido)             */

/* --- Modo de envio --- */
#define LCD_CMD   0U      /* enviar como comando -> RS = 0 */
#define LCD_DATA  1U      /* enviar como dato    -> RS = 1 */

/* Estado del backlight: se "ORea" en cada byte enviado al PCF8574 para
 * no apagar la luz accidentalmente entre transferencias. */
static uint8_t backlight_flag = LCD_BL;

/* ----------------------------------------------------------------------------
 *  pcf8574_write: una escritura I2C cruda de un byte al expansor.
 *  Todo lo que llega al LCD pasa por aca.
 * ------------------------------------------------------------------------- */
static void pcf8574_write(uint8_t data)
{
    uint8_t b = data | backlight_flag;
    i2c_transfer7(I2C1, LCD_I2C_ADDR, &b, 1, NULL, 0);
}

/* ----------------------------------------------------------------------------
 *  lcd_send_nibble: coloca un nibble (4 bits) en D4-D7 y genera el pulso EN.
 *
 *  El nibble ocupa los 4 bits altos del byte del PCF8574. Los 4 bits bajos
 *  son control. Se escribe dos veces: primero con EN=1 (datos presentes),
 *  luego con EN=0. El flanco de bajada de EN es el que el HD44780 usa para
 *  capturar el nibble.
 * ------------------------------------------------------------------------- */
static void lcd_send_nibble(uint8_t nibble, uint8_t mode)
{
    uint8_t base = (uint8_t)((nibble & 0x0FU) << 4);

    if (mode == LCD_DATA) {
        base |= LCD_RS;
    }

    pcf8574_write(base | LCD_EN);
    pcf8574_write(base);
}

/* ----------------------------------------------------------------------------
 *  lcd_send_byte: parte un byte en nibble alto + nibble bajo y los envia.
 *  Solo valido una vez que el LCD ya esta en modo 4 bits.
 * ------------------------------------------------------------------------- */
static void lcd_send_byte(uint8_t value, uint8_t mode)
{
    lcd_send_nibble((uint8_t)(value >> 4), mode);
    lcd_send_nibble((uint8_t)(value & 0x0FU), mode);
}

/* ============================================================================
 *  API publica
 * ========================================================================= */

void lcd_init(void)
{
    /* 1. Esperar a que el HD44780 se estabilice tras encender (>40 ms). */
    delay_ms(50);

    /* 2. Secuencia para forzar modo 8 bits conocido: enviar 0x3 tres veces.
     *    Se mandan como NIBBLE UNICO (el chip aun esta en modo 8 bits, asi
     *    que interpreta un solo envio de 4 bits como comando completo). */
    lcd_send_nibble(0x03U, LCD_CMD);
    delay_ms(5);
    lcd_send_nibble(0x03U, LCD_CMD);
    delay_ms(1);
    lcd_send_nibble(0x03U, LCD_CMD);
    delay_ms(1);

    /* 3. Pasar a modo 4 bits: enviar 0x2 como nibble unico. */
    lcd_send_nibble(0x02U, LCD_CMD);
    delay_ms(1);

    /* 4. A partir de aca ya estamos en modo 4 bits: comandos como byte
     *    partido en dos nibbles. */
    lcd_send_byte(0x28U, LCD_CMD);
    delay_ms(1);
    lcd_send_byte(0x08U, LCD_CMD);
    delay_ms(1);
    lcd_send_byte(0x01U, LCD_CMD);
    delay_ms(2);
    lcd_send_byte(0x06U, LCD_CMD);
    delay_ms(1);
    lcd_send_byte(0x0CU, LCD_CMD);
    delay_ms(1);
}

void lcd_clear(void)
{
    lcd_send_byte(0x01U, LCD_CMD);
    delay_ms(2);
}

void lcd_set_cursor(uint8_t row, uint8_t col)
{
    static const uint8_t row_offsets[2] = { 0x00U, 0x40U };

    if (row > 1U) {
        row = 1U;
    }

    if (col > 15U) {
        col = 15U;
    }

    lcd_send_byte((uint8_t)(0x80U | (row_offsets[row] + col)), LCD_CMD);
}

void lcd_write_char(char c)
{
    lcd_send_byte((uint8_t)c, LCD_DATA);
}

void lcd_write_string(const char *s)
{
    while (*s) {
        lcd_write_char(*s);
        s++;
    }
}

void lcd_backlight(uint8_t on)
{
    backlight_flag = on ? LCD_BL : 0x00U;
    pcf8574_write(0x00U);
}

/* ----------------------------------------------------------------------------
 *  lcd_print_line: escribe una fila completa (16 chars) rellenando con
 *  espacios. Clave para el refresco dinamico sin parpadeo: no se llama a
 *  lcd_clear(), solo se sobrescriben los 16 caracteres de la fila. Lo que
 *  sobra de la cadena se completa con ' ' para borrar los digitos viejos.
 * ------------------------------------------------------------------------- */
void lcd_print_line(uint8_t row, const char *s)
{
    lcd_set_cursor(row, 0);

    for (uint8_t col = 0; col < 16U; col++) {
        if (*s) {
            lcd_write_char(*s);
            s++;
        } else {
            lcd_write_char(' ');   /* rellenar para borrar lo anterior */
        }
    }
}
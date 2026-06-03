/******************************************************************************
 *  lcd_i2c.h - Driver para LCD 16x2 (HD44780) via expansor I2C PCF8574
 *  TP3 - Sistemas Embebidos - STM32F103C8T6 + libopencm3
 *
 *  El driver asume modo 4 bits y el mapeo estandar de los backpacks:
 *    P0=RS  P1=RW  P2=EN  P3=BL  P4=D4  P5=D5  P6=D6  P7=D7
 *
 *  Requiere una funcion de delay en milisegundos provista por el usuario
 *  (en este TP, basada en SysTick). Ver lcd_init().
 *****************************************************************************/
#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <stdint.h>

/* Direccion I2C de 7 bits del backpack PCF8574 detectado en la Etapa 1. */
#define LCD_I2C_ADDR   0x27U

/* Inicializa el LCD: ejecuta la secuencia ritual del HD44780 para
 * llevarlo a modo 4 bits, 2 lineas, display ON y pantalla limpia.
 * Debe llamarse despues de configurar I2C1 y SysTick. */
void lcd_init(void);

/* Limpia toda la pantalla y vuelve el cursor a (0,0). Tarda ~2 ms. */
void lcd_clear(void);

/* Posiciona el cursor. fila: 0 o 1. columna: 0..15. */
void lcd_set_cursor(uint8_t row, uint8_t col);

/* Escribe un caracter en la posicion actual del cursor. */
void lcd_write_char(char c);

/* Escribe una cadena terminada en '\0' desde la posicion actual. */
void lcd_write_string(const char *s);

/* Enciende (1) o apaga (0) la luz de fondo. */
void lcd_backlight(uint8_t on);

/* Escribe una linea completa (fila 0 o 1) rellenando con espacios hasta
 * los 16 caracteres. Sirve para refresco dinamico SIN parpadeo: en vez de
 * lcd_clear() + reescribir (que parpadea), sobrescribe los 16 caracteres
 * de la fila, borrando lo viejo con espacios. */
void lcd_print_line(uint8_t row, const char *s);

#endif /* LCD_I2C_H */
/******************************************************************************
 *  TP3 - ETAPA 3: Display dinamico (integracion completa con el TP2)
 *  Plataforma: STM32F103C8T6 (Blue Pill)
 *  Toolchain : arm-none-eabi-gcc + libopencm3
 *  Debug     : USART1 (PA9) a 115200 8N1
 *
 *  Funcionalidad:
 *    - Reusa el sistema completo del TP2: ADC (PA0), PWM del LED (PA1),
 *      boton con anti-rebote (PB1/EXTI1) y TIM3 como base de tiempo (50 ms).
 *    - Agrega un LCD 16x2 por I2C (PB6/PB7) que muestra datos en tiempo real.
 *    - El boton cicla 3 modos que cambian a la vez el brillo del LED y lo
 *      que se muestra en el LCD:
 *        Modo 0 DIRECTO : LED brilla segun pote;  LCD muestra ADC crudo + mV.
 *        Modo 1 INVERSO : LED brilla inverso;     LCD muestra duty % + modo.
 *        Modo 2 FIJO_50 : LED al 50%;             LCD muestra texto fijo 50%.
 *
 *  PATRON DE ACTUALIZACION (lo central de la etapa):
 *    - La ISR de TIM3 (50 ms) muestrea el ADC y levanta flags. NO toca el LCD.
 *    - Cada 2 disparos de TIM3 (= 100 ms = 10 Hz) levanta tick_lcd.
 *    - El main loop, al ver tick_lcd, hace la transferencia I2C pesada al LCD.
 *    - Asi las ISR quedan cortas y la transferencia bloqueante vive en el main.
 *    - El refresco usa lcd_print_line (sin lcd_clear) para no parpadear.
 *
 *  Conexionado (TP2 + LCD):
 *    PA0 -> pote 10k (extremos a 3.3V y GND)
 *    PA1 -> R 220 -> LED -> GND
 *    PA9 -> RX del USB-Serial
 *    PB1 -> pulsador a 3.3V + R 10k pulldown a GND
 *    PB6 -> SCL del backpack + pull-up 4.7k a 3.3V
 *    PB7 -> SDA del backpack + pull-up 4.7k a 3.3V
 *    GND comun a todo.
 *****************************************************************************/

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "lcd_i2c.h"

/* Constantes del sistema (heredadas del TP2) */
#define PWM_ARR_VALUE      999U
#define NUM_MODES          3U
#define DEBOUNCE_TICKS     1U
#define DUTY_50_PERCENT    500U

#define MODE_DIRECT        0U
#define MODE_INVERSE       1U
#define MODE_FIXED_50      2U

#define BUTTON_IDLE        0U
#define BUTTON_ARMED       1U

#define APB1_FREQ_MHZ      36U

/* Refresco del LCD: 1 disparo de TIM3 = 50 ms; 2 disparos = 100 ms = 10 Hz. */
#define LCD_REFRESH_TICKS  2U

/* ----------------------------------------------------------------------------
 *  Variables compartidas ISR <-> main: SIEMPRE volatile
 * ------------------------------------------------------------------------- */
static volatile uint16_t adc_value     = 0;
static volatile bool     new_data      = false;
static volatile bool     tick_lcd      = false;   /* refresco del LCD       */
static volatile uint8_t  lcd_div       = 0;       /* divisor de 50 a 100 ms */
static volatile uint8_t  mode          = MODE_DIRECT;
static volatile uint8_t  button_state  = BUTTON_IDLE;
static volatile uint8_t  debounce_cnt  = 0;

/* ----------------------------------------------------------------------------
 *  Base de tiempo SysTick (1 ms) - usada por el driver del LCD (delay_ms)
 * ------------------------------------------------------------------------- */
static volatile uint32_t ms_ticks = 0;

void sys_tick_handler(void)
{
    ms_ticks++;
}

static void systick_setup(void)
{
    systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);   /* 72 MHz */
    systick_set_reload(71999);                         /* 1 ms */
    systick_interrupt_enable();
    systick_counter_enable();
}

void delay_ms(uint32_t ms)
{
    uint32_t start = ms_ticks;
    while ((ms_ticks - start) < ms) {
        __asm__("nop");
    }
}

/* ----------------------------------------------------------------------------
 *  Prototipos
 * ------------------------------------------------------------------------- */
static void clock_setup(void);
static void gpio_setup(void);
static void usart_setup(void);
static void adc_setup(void);
static void tim3_setup(void);
static void tim2_pwm_setup(void);
static void exti_setup(void);
static void i2c1_setup(void);

static uint16_t calculate_duty(uint16_t adc, uint8_t m);
static const char *mode_name(uint8_t m);
static void usart_send_str(const char *s);
static void delay_nops(uint32_t n);

/* ============================================================================
 *  clock_setup
 * ========================================================================= */
static void clock_setup(void)
{
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

    rcc_periph_clock_enable(RCC_GPIOA);   /* PA0, PA1, PA9        */
    rcc_periph_clock_enable(RCC_GPIOB);   /* PB1, PB6, PB7        */
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_USART1);
    rcc_periph_clock_enable(RCC_ADC1);
    rcc_periph_clock_enable(RCC_TIM2);
    rcc_periph_clock_enable(RCC_TIM3);
    rcc_periph_clock_enable(RCC_I2C1);
}

/* ============================================================================
 *  gpio_setup
 * ========================================================================= */
static void gpio_setup(void)
{
    /* PA0: entrada analogica ADC1_IN0. */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_ANALOG, GPIO0);

    /* PA1: PWM (TIM2_CH2), alternate function push-pull. */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO1);

    /* PA9: TX USART1. */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO9);

    /* PB1: entrada con pull-down (boton). */
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, GPIO1);
    gpio_clear(GPIOB, GPIO1);

    /* PB6/PB7: I2C1, alternate function open-drain. */
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_OPENDRAIN,
                  GPIO6 | GPIO7);
}

/* ============================================================================
 *  usart_setup
 * ========================================================================= */
static void usart_setup(void)
{
    usart_set_baudrate(USART1, 115200);
    usart_set_databits(USART1, 8);
    usart_set_stopbits(USART1, USART_STOPBITS_1);
    usart_set_parity(USART1, USART_PARITY_NONE);
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);
    usart_set_mode(USART1, USART_MODE_TX);
    usart_enable(USART1);
}

/* ============================================================================
 *  adc_setup
 * ========================================================================= */
static void adc_setup(void)
{
    adc_power_off(ADC1);
    rcc_set_adcpre(RCC_CFGR_ADCPRE_PCLK2_DIV6);

    adc_disable_scan_mode(ADC1);
    adc_set_single_conversion_mode(ADC1);
    adc_disable_external_trigger_regular(ADC1);
    adc_set_right_aligned(ADC1);
    adc_set_sample_time(ADC1, ADC_CHANNEL0, ADC_SMPR_SMP_28DOT5CYC);

    uint8_t channel_array[16] = { ADC_CHANNEL0 };
    adc_set_regular_sequence(ADC1, 1, channel_array);

    adc_power_on(ADC1);
    delay_nops(80000);

    adc_reset_calibration(ADC1);
    adc_calibrate(ADC1);
}

/* ============================================================================
 *  i2c1_setup: I2C1 en PB6/PB7 a 100 kHz (los pines se configuran en gpio_setup).
 * ========================================================================= */
static void i2c1_setup(void)
{
    rcc_periph_reset_pulse(RST_I2C1);
    i2c_peripheral_disable(I2C1);
    i2c_set_speed(I2C1, i2c_speed_sm_100k, APB1_FREQ_MHZ);
    i2c_peripheral_enable(I2C1);
}

/* ============================================================================
 *  tim3_setup: IRQ cada 50 ms (base temporal + debounce + divisor LCD).
 * ========================================================================= */
static void tim3_setup(void)
{
    rcc_periph_reset_pulse(RST_TIM3);

    timer_set_mode(TIM3, TIM_CR1_CKD_CK_INT,
                   TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

    timer_set_prescaler(TIM3, 7199);   /* tick de 100 us */
    timer_set_period(TIM3,    499);    /* 500 ticks = 50 ms */

    timer_enable_irq(TIM3, TIM_DIER_UIE);
    nvic_enable_irq(NVIC_TIM3_IRQ);
    timer_enable_counter(TIM3);
}

/* ============================================================================
 *  tim2_pwm_setup: TIM2 CH2 PWM 1 kHz en PA1.
 * ========================================================================= */
static void tim2_pwm_setup(void)
{
    rcc_periph_reset_pulse(RST_TIM2);

    timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT,
                   TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

    timer_set_prescaler(TIM2, 71);
    timer_set_period(TIM2,    PWM_ARR_VALUE);

    timer_set_oc_mode(TIM2, TIM_OC2, TIM_OCM_PWM1);
    timer_enable_oc_preload(TIM2, TIM_OC2);
    timer_set_oc_polarity_high(TIM2, TIM_OC2);
    timer_set_oc_value(TIM2, TIM_OC2, 0);
    timer_enable_oc_output(TIM2, TIM_OC2);
    timer_enable_preload(TIM2);

    timer_enable_counter(TIM2);
}

/* ============================================================================
 *  exti_setup: EXTI1 en PB1, flanco de subida.
 * ========================================================================= */
static void exti_setup(void)
{
    exti_select_source(EXTI1, GPIOB);
    exti_set_trigger(EXTI1, EXTI_TRIGGER_RISING);
    exti_enable_request(EXTI1);
    nvic_enable_irq(NVIC_EXTI1_IRQ);
}

/* ============================================================================
 *  calculate_duty: mapeo ADC -> duty segun modo (identico al TP2).
 * ========================================================================= */
static uint16_t calculate_duty(uint16_t adc, uint8_t m)
{
    uint32_t duty;

    switch (m) {
    case MODE_DIRECT:
        duty = ((uint32_t)adc * 1000U) / 4096U;
        break;
    case MODE_INVERSE:
        duty = ((uint32_t)(4095U - adc) * 1000U) / 4096U;
        break;
    case MODE_FIXED_50:
        duty = DUTY_50_PERCENT;
        break;
    default:
        duty = 0;
        break;
    }

    if (duty > PWM_ARR_VALUE) duty = PWM_ARR_VALUE;
    return (uint16_t)duty;
}

static const char *mode_name(uint8_t m)
{
    switch (m) {
    case MODE_DIRECT:   return "DIRECTO";
    case MODE_INVERSE:  return "INVERSO";
    case MODE_FIXED_50: return "FIJO 50";
    default:            return "?";
    }
}

/* ============================================================================
 *  ISR de TIM3: muestreo ADC, anti-rebote y divisor de refresco del LCD.
 *  CLAVE: la ISR NO toca el LCD. Solo levanta tick_lcd cada 100 ms.
 * ========================================================================= */
void tim3_isr(void)
{
    if (timer_get_flag(TIM3, TIM_SR_UIF)) {
        timer_clear_flag(TIM3, TIM_SR_UIF);

        /* --- Muestreo del ADC --- */
        adc_start_conversion_direct(ADC1);
        while (!(adc_eoc(ADC1))) {
            /* spin ~3 us */
        }
        adc_value = adc_read_regular(ADC1);
        new_data  = true;

        /* --- Divisor para el refresco del LCD (cada 2 -> 100 ms) --- */
        lcd_div++;
        if (lcd_div >= LCD_REFRESH_TICKS) {
            lcd_div  = 0;
            tick_lcd = true;
        }

        /* --- Anti-rebote del boton --- */
        if (button_state == BUTTON_ARMED) {
            debounce_cnt++;
            if (debounce_cnt >= DEBOUNCE_TICKS) {
                if (gpio_get(GPIOB, GPIO1)) {
                    mode = (mode + 1) % NUM_MODES;
                }
                button_state = BUTTON_IDLE;
                debounce_cnt = 0;
            }
        }
    }
}

/* ============================================================================
 *  ISR de EXTI1: arma el anti-rebote. Cortita.
 * ========================================================================= */
void exti1_isr(void)
{
    exti_reset_request(EXTI1);

    if (button_state == BUTTON_IDLE) {
        button_state = BUTTON_ARMED;
        debounce_cnt = 0;
    }
}

/* ============================================================================
 *  Auxiliares
 * ========================================================================= */
static void usart_send_str(const char *s)
{
    while (*s) {
        usart_send_blocking(USART1, (uint16_t)(*s));
        s++;
    }
}

static void delay_nops(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++) {
        __asm__("nop");
    }
}

/* ============================================================================
 *  refresh_lcd: arma las dos lineas segun el modo y las vuelca al LCD.
 *  Se llama desde el main loop (NO desde una ISR) porque hace I2C bloqueante.
 *  Usa lcd_print_line (rellena a 16 chars) para no parpadear.
 * ========================================================================= */
static void refresh_lcd(uint16_t adc, uint8_t m, uint16_t duty)
{
    char line0[17];
    char line1[17];

    uint32_t mv      = ((uint32_t)adc * 3300U) / 4095U;
    uint32_t duty_pc = ((uint32_t)duty * 100U) / PWM_ARR_VALUE;

    switch (m) {
    case MODE_DIRECT:
        snprintf(line0, sizeof(line0), "Modo: %s", mode_name(m));
        snprintf(line1, sizeof(line1), "A:%4u %4lumV",
                 (unsigned)adc, (unsigned long)mv);
        break;

    case MODE_INVERSE:
        snprintf(line0, sizeof(line0), "Modo: %s", mode_name(m));
        snprintf(line1, sizeof(line1), "Duty: %3lu %%",
                 (unsigned long)duty_pc);
        break;

    case MODE_FIXED_50:
    default:
        snprintf(line0, sizeof(line0), "Modo: %s", mode_name(m));
        snprintf(line1, sizeof(line1), "Duty fijo 50 %%");
        break;
    }

    lcd_print_line(0, line0);
    lcd_print_line(1, line1);
}

/* ============================================================================
 *  main
 * ========================================================================= */
int main(void)
{
    clock_setup();
    systick_setup();
    gpio_setup();
    usart_setup();
    adc_setup();
    i2c1_setup();
    tim2_pwm_setup();
    exti_setup();

    usart_send_str("\r\n=== TP3 - ETAPA 3: display dinamico ===\r\n");

    /* Pantalla de bienvenida */
    lcd_init();
    lcd_print_line(0, "TP3 Embebidos");
    lcd_print_line(1, "Etapa 3 lista");
    delay_ms(1500);

    tim3_setup();

    uint8_t last_mode_printed = 0xFF;

    while (1) {
        /* --- Trabajo rapido: calcular duty y actualizar PWM --- */
        if (new_data) {
            uint16_t v = adc_value;
            new_data = false;

            uint8_t  m    = mode;
            uint16_t duty = calculate_duty(v, m);

            timer_set_oc_value(TIM2, TIM_OC2, duty);

            if (m != last_mode_printed) {
                char buf[40];
                snprintf(buf, sizeof(buf),
                         "\r\n>>> MODO: %s <<<\r\n", mode_name(m));
                usart_send_str(buf);
                last_mode_printed = m;
            }
        }

        /* --- Trabajo lento: refrescar el LCD por I2C (10 Hz) --- */
        if (tick_lcd) {
            tick_lcd = false;

            uint16_t v    = adc_value;
            uint8_t  m    = mode;
            uint16_t duty = calculate_duty(v, m);

            refresh_lcd(v, m, duty);
        }
    }
}

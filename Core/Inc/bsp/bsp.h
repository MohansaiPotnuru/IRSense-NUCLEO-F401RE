/**
 * @file    bsp.h
 * @brief   Board support for the NUCLEO-F401RE (STM32F401RE @ 84 MHz).
 *
 * Peripherals used (all configured here, not in CubeMX, so regenerating
 * code from Mohan_Sai.ioc never overwrites them):
 *   TIM2    32-bit free-running 1 MHz timebase (µs timestamps)
 *   USART2  115200 8N1 on PA2/PA3 -> ST-Link virtual COM port, IRQ driven
 *   EXTI0   PB0 IR sensor, both edges, timestamped in the ISR
 *   IWDG    independent watchdog (frozen while the debugger halts the core)
 *   FLASH   sector 7 (0x08060000, 128 KiB) holds the config records
 *   ITM     SWO trace output (PB3) mirrors the log
 */
#ifndef BSP_BSP_H
#define BSP_BSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/config_store.h"
#include "app/event_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Interrupt priorities (0 = highest). IR timestamps must not be delayed. */
#define BSP_IRQ_PRIO_IR   1u
#define BSP_IRQ_PRIO_UART 5u

/* Time */
void     bsp_time_init(void);
uint32_t bsp_time_us(void); /* wraps every ~71.6 minutes */
uint32_t bsp_time_ms(void);

/* UART (USART2 / ST-Link VCP) */
void     bsp_uart_init(uint32_t baud);
size_t   bsp_uart_write(const void *data, size_t len);
bool     bsp_uart_getc(uint8_t *c);
void     bsp_uart_flush(uint32_t timeout_ms);
uint32_t bsp_uart_rx_overflows(void);
uint32_t bsp_uart_tx_dropped(void);

/* IR sensor on PB0 */
void     bsp_ir_init(event_queue_t *queue);
bool     bsp_ir_level(void);
uint32_t bsp_ir_edge_count(void);

/* Button B1 (PC13) and LED LD2 (PA5) */
bool     bsp_button_pressed(void);
void     bsp_led_write(bool on);

/* Watchdog */
void     bsp_wdg_init(uint32_t timeout_ms);
void     bsp_wdg_kick(void);

/* System */
void        bsp_reset_cause_capture(void); /* call once, early */
const char *bsp_reset_cause(void);
bool        bsp_reset_was_watchdog(void);
void        bsp_uid(uint32_t uid[3]);
void        bsp_reboot(void);
void        bsp_swo_write(const char *s, size_t len);

/* Config flash (sector 7) */
const flash_region_t *bsp_config_flash(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BSP_H */

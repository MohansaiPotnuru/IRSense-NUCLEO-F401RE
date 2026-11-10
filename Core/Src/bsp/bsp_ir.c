/**
 * @file    bsp_ir.c
 * @brief   IR sensor input on PB0: EXTI0 on both edges, µs timestamps.
 *
 * The ISR does the minimum: read the timer, read the pin, queue an event.
 * All filtering and logic happen in the main loop.
 */
#include "bsp/bsp.h"
#include "main.h"

#define IR_PORT GPIOB
#define IR_PIN  GPIO_PIN_0

static event_queue_t    *s_queue;
static volatile uint32_t s_edges;

void bsp_ir_init(event_queue_t *queue)
{
    s_queue = queue;

    /* Re-configure PB0 (CubeMX leaves it as a plain input). The pull-up keeps
     * the line defined if the sensor is unplugged (reads "clear" for the usual
     * active-low modules). HAL_GPIO_Init also routes PB0 to EXTI0 via SYSCFG. */
    GPIO_InitTypeDef gpio = { 0 };
    gpio.Pin  = IR_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(IR_PORT, &gpio);

    EXTI->PR = EXTI_PR_PR0; /* drop anything latched during setup */
    HAL_NVIC_SetPriority(EXTI0_IRQn, BSP_IRQ_PRIO_IR, 0u);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

bool bsp_ir_level(void)
{
    return (IR_PORT->IDR & IR_PIN) != 0u;
}

uint32_t bsp_ir_edge_count(void)
{
    return s_edges;
}

void EXTI0_IRQHandler(void)
{
    uint32_t t = TIM2->CNT; /* timestamp first */
    EXTI->PR   = EXTI_PR_PR0;

    event_t e;
    e.timestamp_us = t;
    e.type         = (uint16_t)EVT_IR_EDGE;
    e.arg          = (IR_PORT->IDR & IR_PIN) ? 1u : 0u;
    if (s_queue != NULL) {
        (void)eq_push(s_queue, &e);
    }
    s_edges++;
}

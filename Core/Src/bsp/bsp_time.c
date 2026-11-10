/**
 * @file    bsp_time.c
 * @brief   1 MHz free-running timebase on TIM2 (32-bit) + HAL millisecond tick.
 */
#include "bsp/bsp.h"
#include "main.h"

void bsp_time_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* APB1 timers run at 2 x PCLK1 whenever the APB1 prescaler is not 1. */
    uint32_t pclk1  = HAL_RCC_GetPCLK1Freq();
    uint32_t timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) ? pclk1 : 2u * pclk1;

    TIM2->CR1 = 0u;
    TIM2->PSC = (timclk / 1000000u) - 1u; /* 84 MHz / 84 = 1 MHz */
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->CNT = 0u;
    TIM2->EGR = TIM_EGR_UG; /* load PSC now */
    TIM2->SR  = 0u;
    TIM2->CR1 = TIM_CR1_CEN;
}

uint32_t bsp_time_us(void)
{
    return TIM2->CNT;
}

uint32_t bsp_time_ms(void)
{
    return HAL_GetTick();
}

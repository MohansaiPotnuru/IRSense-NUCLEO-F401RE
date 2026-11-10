/**
 * @file    bsp_board.c
 * @brief   Button, LED, watchdog, reset cause, unique ID and SWO output.
 */
#include "bsp/bsp.h"
#include "main.h"

/* ---- Button B1 / LED LD2 ------------------------------------------------ */

bool bsp_button_pressed(void)
{
    /* B1 has an external pull-up and shorts PC13 to GND when pressed. */
    return HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET;
}

void bsp_led_write(bool on)
{
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---- Independent watchdog ----------------------------------------------- */

#define IWDG_KEY_ENABLE 0xCCCCu
#define IWDG_KEY_RELOAD 0xAAAAu
#define IWDG_KEY_ACCESS 0x5555u
#define LSI_HZ          32000u
#define IWDG_PRESCALER  64u   /* PR = 4 */

void bsp_wdg_init(uint32_t timeout_ms)
{
    /* Stop the watchdog while a debugger has the core halted. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    uint32_t reload = (timeout_ms * (LSI_HZ / IWDG_PRESCALER)) / 1000u;
    if (reload < 1u) {
        reload = 1u;
    }
    if (reload > 0x0FFFu) {
        reload = 0x0FFFu; /* ~8.2 s max at /64 */
    }

    IWDG->KR = IWDG_KEY_ENABLE; /* starts the LSI and the counter */
    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR  = 4u;
    IWDG->RLR = reload - 1u;
    uint32_t start = HAL_GetTick();
    while (IWDG->SR != 0u && (HAL_GetTick() - start) < 100u) {
    }
    IWDG->KR = IWDG_KEY_RELOAD;
}

void bsp_wdg_kick(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

/* ---- Reset cause ---------------------------------------------------------- */

static const char *s_reset_cause = "unknown";
static bool        s_reset_wdg;

void bsp_reset_cause_capture(void)
{
    uint32_t csr = RCC->CSR;

    if (csr & RCC_CSR_IWDGRSTF) {
        s_reset_cause = "independent watchdog";
        s_reset_wdg   = true;
    } else if (csr & RCC_CSR_WWDGRSTF) {
        s_reset_cause = "window watchdog";
        s_reset_wdg   = true;
    } else if (csr & RCC_CSR_LPWRRSTF) {
        s_reset_cause = "low-power";
    } else if (csr & RCC_CSR_SFTRSTF) {
        s_reset_cause = "software";
    } else if (csr & RCC_CSR_PORRSTF) {
        s_reset_cause = "power-on";
    } else if (csr & RCC_CSR_BORRSTF) {
        s_reset_cause = "brown-out";
    } else if (csr & RCC_CSR_PINRSTF) {
        s_reset_cause = "reset pin";
    }
    RCC->CSR |= RCC_CSR_RMVF; /* clear for next time */
}

const char *bsp_reset_cause(void)
{
    return s_reset_cause;
}

bool bsp_reset_was_watchdog(void)
{
    return s_reset_wdg;
}

/* ---- Misc ----------------------------------------------------------------- */

void bsp_uid(uint32_t uid[3])
{
    const volatile uint32_t *p = (const volatile uint32_t *)UID_BASE;
    uid[0] = p[0];
    uid[1] = p[1];
    uid[2] = p[2];
}

void bsp_reboot(void)
{
    bsp_uart_flush(100u);
    NVIC_SystemReset();
}

void bsp_swo_write(const char *s, size_t len)
{
    /* ITM_SendChar returns immediately when no trace probe enabled the ITM. */
    for (size_t i = 0u; i < len; i++) {
        (void)ITM_SendChar((uint32_t)(uint8_t)s[i]);
    }
}

/**
 * @file    bsp_uart.c
 * @brief   Interrupt-driven USART2 driver (register level) with ring buffers.
 *
 * USART2 on PA2 (TX) / PA3 (RX) is wired to the ST-Link, which exposes it
 * as a virtual COM port over the same USB cable used for flashing.
 *
 * RX: the ISR pushes bytes into s_rx; the main loop drains it.
 * TX: the main loop fills s_tx and enables TXEIE; the ISR sends a byte per
 *     TXE interrupt and disables TXEIE when the buffer runs empty.
 * Only call the write functions from the main loop (single producer).
 */
#include "app/ring_buffer.h"
#include "bsp/bsp.h"
#include "main.h"

#define UART_RX_SIZE 256u
#define UART_TX_SIZE 2048u
#define UART_TX_WAIT_MS 50u

static uint8_t       s_rx_buf[UART_RX_SIZE];
static uint8_t       s_tx_buf[UART_TX_SIZE];
static ring_buffer_t s_rx;
static ring_buffer_t s_tx;
static volatile uint32_t s_rx_overflows;
static uint32_t      s_tx_dropped;

static void tx_kick(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    USART2->CR1 |= USART_CR1_TXEIE;
    __set_PRIMASK(primask);
}

static bool can_wait(void)
{
    /* Waiting is pointless with interrupts masked or from inside an ISR. */
    return (__get_PRIMASK() == 0u) && ((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) == 0u);
}

void bsp_uart_init(uint32_t baud)
{
    (void)rb_init(&s_rx, s_rx_buf, UART_RX_SIZE);
    (void)rb_init(&s_tx, s_tx_buf, UART_TX_SIZE);

    /* PA2/PA3 are already AF7 (CubeMX MX_GPIO_Init). Pull RX up so a
     * disconnected line idles high instead of producing framing errors. */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPD3) | GPIO_PUPDR_PUPD3_0;

    __HAL_RCC_USART2_CLK_ENABLE();
    USART2->CR1 = 0u;
    USART2->CR2 = 0u; /* 1 stop bit */
    USART2->CR3 = 0u; /* no flow control */
    /* Oversampling by 16: BRR = PCLK1 / baud (mantissa.fraction in 12.4). */
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    USART2->BRR = (pclk1 + baud / 2u) / baud;
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    HAL_NVIC_SetPriority(USART2_IRQn, BSP_IRQ_PRIO_UART, 0u);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void)
{
    uint32_t sr = USART2->SR;

    if ((sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0u) {
        /* Reading SR then DR clears RXNE and the error flags. */
        uint8_t byte = (uint8_t)USART2->DR;
        if ((sr & USART_SR_ORE) != 0u) {
            s_rx_overflows++;
        }
        if ((sr & USART_SR_RXNE) != 0u && (sr & (USART_SR_FE | USART_SR_NE)) == 0u) {
            if (!rb_put(&s_rx, byte)) {
                s_rx_overflows++;
            }
        }
    }

    if ((USART2->CR1 & USART_CR1_TXEIE) != 0u && (sr & USART_SR_TXE) != 0u) {
        uint8_t byte;
        if (rb_get(&s_tx, &byte)) {
            USART2->DR = byte;
        } else {
            USART2->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

size_t bsp_uart_write(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t         n = 0u;

    while (n < len) {
        if (rb_put(&s_tx, p[n])) {
            n++;
            continue;
        }
        /* Buffer full: make sure it drains, then wait a bounded time. */
        tx_kick();
        if (!can_wait()) {
            break;
        }
        uint32_t start = HAL_GetTick();
        while (rb_is_full(&s_tx) && (HAL_GetTick() - start) < UART_TX_WAIT_MS) {
        }
        if (rb_is_full(&s_tx)) {
            break; /* terminal not keeping up; drop the rest */
        }
    }
    s_tx_dropped += (uint32_t)(len - n);
    tx_kick();
    return n;
}

bool bsp_uart_getc(uint8_t *c)
{
    return rb_get(&s_rx, c);
}

void bsp_uart_flush(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while ((!rb_is_empty(&s_tx) || (USART2->SR & USART_SR_TC) == 0u) &&
           (HAL_GetTick() - start) < timeout_ms) {
    }
}

uint32_t bsp_uart_rx_overflows(void)
{
    return s_rx_overflows;
}

uint32_t bsp_uart_tx_dropped(void)
{
    return s_tx_dropped;
}

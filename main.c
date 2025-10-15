/* Template app on which you can build your own. */

#include <inttypes.h>
#include <stdbool.h>

#include "RingBuffer.h"
#include "ch32fun.h"
#include "log.h"
#include "rv003usb.h"
#include "slipdev.h"
#include "uip.h"

#define TAG "main"

#define array_size(x) (sizeof(x) / sizeof((x)[0]))
#define millis()      (g_systick)

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

static void SysTick_Init(void);

// Global variables
volatile uint32_t g_systick = 0;

// TODO: test why 256 is needed here
// Must be a bug in the RingBuffer implementation
static uint8_t tx_buf[256] = {0};
static RingBuffer_t tx_ringbuf = {0};

// This can be as big as the SLIP_BUFFER_SIZE for
// the lowest latency or smaller for lower memory usage
static uint8_t rx_buf[64] = {0};
static RingBuffer_t rx_ringbuf = {0};

int main()
{
    SystemInit();

    // Enable GPIOs
    // clang-format off
    RCC->APB2PCENR = RCC_APB2Periph_GPIOD
                   | RCC_APB2Periph_GPIOC
                   | RCC_APB2Periph_GPIOA
                   | RCC_APB2Periph_AFIO
                   | RCC_APB2Periph_TIM1
                   | RCC_APB2Periph_ADC1;
    // clang-format on
    RCC->APB1PCENR = RCC_APB1Periph_TIM2;

    SysTick_Init();

    (void)RingBuffer_Init(&tx_ringbuf, tx_buf, array_size(tx_buf));
    (void)RingBuffer_Init(&rx_ringbuf, rx_buf, array_size(rx_buf));

    usb_setup();

    const bool debuggerAttached = !WaitForDebuggerToAttach(1000);
    if (debuggerAttached)
    {
        LOG_Init(eLOG_LEVEL_DEBUG);
    }
    else
    {
        LOG_Init(eLOG_LEVEL_NONE);
    }

    uint32_t lastMillis = millis();

#if 0 // enable for testing semihosting i/o
    char c;
    while (1)
    {
        if (millis() - lastMillis > 500)
        {
            lastMillis = millis();
            slipdev_char_put('.');
        }

        if (slipdev_char_poll((uint8_t *)&c))
        {
            LOGW(TAG, "You pressed: %c", c);
        }
    }
#endif

    slipdev_init();
    uip_init();
    httpd_init();

    for (;;)
    {
        uip_len = slipdev_poll();
        if (uip_len > 0)
        {
            uip_input();
            if (uip_len > 0)
            {
                slipdev_send();
            }
        }

        const uint32_t now = millis();
        if (now - lastMillis >= 1000)
        {
            lastMillis = now;
            for (uint8_t i = 0; i < UIP_CONNS; i++)
            {
                uip_periodic(i);
                if (uip_len > 0)
                {
                    slipdev_send();
                }
            }
        }
    }
}

void uip_log(char *msg)
{
    LOGD("uip", msg);
}

void slipdev_write(uint8_t *buf, uint16_t len)
{
    size_t start = millis();
    do
    {
        if (millis() - start > 500) break;

        size_t room = tx_ringbuf.size - RingBuffer_Peek(&tx_ringbuf);
        if (room == 0) continue;

        if (room > len)
        {
            room = len;
        }
        (void)RingBuffer_Put(&tx_ringbuf, buf, room);
        buf += room;
        len -= room;
    } while (len > 0);
}

void slipdev_char_put(uint8_t c)
{
#if 0
    slipdev_write(&c, 1);
#else
    RingBufferStatus_e status;
    do
    {
        status = RingBuffer_Put(&tx_ringbuf, &c, 1);
    } while (eRING_BUFFER_STATUS_SUCCESS != status);
#endif
}

uint8_t slipdev_char_poll(uint8_t *c)
{
    if (RingBuffer_Peek(&rx_ringbuf))
    {
        (void)RingBuffer_Get(&rx_ringbuf, c, 1, NULL);
        return 1;
    }
    return 0;
}

/**
 * @brief  Enable the SysTick module
 * @param  None
 * @return None
 */
static void SysTick_Init(void)
{
    /* some bit definitions for systick regs */
    /* disable default SysTick behavior */
    SysTick->CTLR = 0;

    /* enable the SysTick IRQ */
    NVIC_EnableIRQ(SysTicK_IRQn);

    /* Set the tick interval to 1ms for normal op */
    SysTick->CMP = SysTick->CNT + (FUNCONF_SYSTEM_CORE_CLOCK / 1000) - 1;

    /* Start at zero */
    g_systick = 0;

    /* Enable SysTick counter, IRQ, HCLK/1 */
    SysTick->CTLR = SYSTICK_CTLR_STE | SYSTICK_CTLR_STIE | SYSTICK_CTLR_STCLK;
}

/*
 * SysTick ISR just counts ticks
 * note - the __attribute__((interrupt)) syntax is crucial!
 */
void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void)
{
    // move the compare further ahead in time.
    // as a warning, if more than this length of time
    // passes before triggering, you may miss your
    // interrupt.
    SysTick->CMP += (FUNCONF_SYSTEM_CORE_CLOCK / 1000);

    /* clear IRQ */
    SysTick->SR = 0;

    /* update counter */
    ++g_systick;
}

// called when host requests IN data on USB endpoint 3
void usb_handle_user_in_request(struct usb_endpoint *e, uint8_t *scratchpad, int endp, uint32_t sendtok, struct rv003usb_internal *ist)
{
    if (endp == 3)
    {
        const size_t available = RingBuffer_Peek(&tx_ringbuf);
        if (available)
        {
            size_t bytes = 0;
            static uint8_t c[8];
            if (eRING_BUFFER_STATUS_SUCCESS == RingBuffer_Get(&tx_ringbuf, c, sizeof(c), &bytes))
            {
                usb_send_data(c, (int)bytes, 0, sendtok);
                return;
            }
        }
    }
    // If it's a control transfer, don't send anything.
    usb_send_empty(sendtok);
}

void usb_handle_other_control_message(struct usb_endpoint *e, struct usb_urb *s, struct rv003usb_internal *ist)
{
    e->opaque = NULL;
}
// called when USB endpoint 2 receives OUT data from host
void usb_handle_user_data(struct usb_endpoint *e, int ep, uint8_t *data, int len, struct rv003usb_internal *ist)
{
    if (ep != 2 || len == 0) return; // only EP2 data
    (void)RingBuffer_Put(&rx_ringbuf, data, (int)len);
}

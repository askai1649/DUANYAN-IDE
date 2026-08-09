/* led_diag.c - ESP32-S3 N16R8 WS2812 4-phase diagnostic
 *
 * Phases (5 s each, looping):
 *   A: power47=HIGH, data=GPIO48, WS2812 white
 *   B: power47=LOW,  data=GPIO48, WS2812 white
 *   C: power47=HIGH, data=GPIO38, WS2812 white
 *   D: power47=LOW,  data=GPIO38, WS2812 white
 *
 * UART prints the phase each cycle so the user can correlate the moment
 * the onboard RGB LED lights up.
 */

#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)

#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)

#define TIMG0_T0CONFIG  (*(volatile unsigned int *)0x6001F000)
#define TIMG0_T0LO      (*(volatile unsigned int *)0x6001F004)
#define TIMG0_T0UPDATE  (*(volatile unsigned int *)0x6001F00C)
#define TIMG0_WDTCONFIG0 (*(volatile unsigned int *)0x6001F048)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_INT_CLR   (*(volatile unsigned int *)0x6001F07C)

#define REG(addr) (*(volatile unsigned int *)(addr))
#define GPIO_BASE 0x60004000
#define IOMUX_BASE 0x60009000
#define GPIO_OUT_W1TS       REG(GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC       REG(GPIO_BASE + 0x0C)
#define GPIO_OUT1_W1TS      REG(GPIO_BASE + 0x14)
#define GPIO_OUT1_W1TC      REG(GPIO_BASE + 0x18)
#define GPIO_ENABLE_W1TS    REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE1_W1TS   REG(GPIO_BASE + 0x30)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static unsigned int ticks_per_us = 10;

static void timer_latch(void) { TIMG0_T0UPDATE = 0; }

static unsigned int timer_now(void)
{
    timer_latch();
    return TIMG0_T0LO;
}

static void wait_ticks(unsigned int start, unsigned int ticks)
{
    unsigned int cap = (ticks + 10) * 100 + 100000;
    while ((timer_now() - start) < ticks && cap--) { }
}

static void wait_us(unsigned int us)
{
    wait_ticks(timer_now(), us * ticks_per_us);
}

static void calibrate(void)
{
    unsigned int t0 = timer_now();
    for (int i = 0; i < 10; i++) uart_putc('~');
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
    unsigned int ticks = timer_now() - t0;
    ticks_per_us = ticks / 955;
    if (ticks_per_us == 0) ticks_per_us = 10;
}

static void disable_watchdogs(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_WDTWPROTECT = 0;
    RTC_CNTL_SWD_WPROT = 0x8F1D312A;
    RTC_CNTL_SWD_CONF |= (1u << 30);
    RTC_CNTL_SWD_WPROT = 0;
    TIMG0_WDTWPROTECT = 0x50D83AA1;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0x1;
    TIMG0_WDTWPROTECT = 0;
}

static void pin_setup(unsigned pin)
{
    REG(IOMUX_BASE + pin * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + pin * 4) = pin;
    if (pin >= 32)
        GPIO_ENABLE1_W1TS = 1u << (pin - 32);
    else
        GPIO_ENABLE_W1TS = 1u << pin;
}

static void pin_hi(unsigned pin)
{
    if (pin >= 32) GPIO_OUT1_W1TS = 1u << (pin - 32);
    else           GPIO_OUT_W1TS   = 1u << pin;
}

static void pin_lo(unsigned pin)
{
    if (pin >= 32) GPIO_OUT1_W1TC = 1u << (pin - 32);
    else           GPIO_OUT_W1TC   = 1u << pin;
}

static void ws2812_send(unsigned pin, unsigned int grb)
{
    unsigned int t0h = (ticks_per_us * 25 + 99) / 100;
    unsigned int t1h = (ticks_per_us * 70 + 99) / 100;
    unsigned int per = (ticks_per_us * 110 + 99) / 100;

    pin_lo(pin);
    wait_ticks(timer_now(), ticks_per_us * 320);

    for (int i = 23; i >= 0; i--) {
        unsigned int t0 = timer_now();
        pin_hi(pin);
        if ((grb >> i) & 1)
            wait_ticks(t0, t1h);
        else
            wait_ticks(t0, t0h);
        pin_lo(pin);
        wait_ticks(t0, per);
    }
    pin_lo(pin);
    wait_ticks(timer_now(), ticks_per_us * 320);
}

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    calibrate();

    uart_puts("\r\n[DUANYAN-OK] led-diag boot\r\n");

    pin_setup(47);
    pin_setup(48);
    pin_setup(38);

    for (;;) {
        /* Phase A: power47=HIGH, data=48 */
        uart_puts("[DUANYAN-OK] phase A pwr47=HI data=48\r\n");
        pin_hi(47);
        for (int n = 0; n < 25; n++) { ws2812_send(48, 0xFFFFFF); wait_us(200000); }

        /* Phase B: power47=LOW, data=48 */
        uart_puts("[DUANYAN-OK] phase B pwr47=LO data=48\r\n");
        pin_lo(47);
        for (int n = 0; n < 25; n++) { ws2812_send(48, 0xFFFFFF); wait_us(200000); }

        /* Phase C: power47=HIGH, data=38 */
        uart_puts("[DUANYAN-OK] phase C pwr47=HI data=38\r\n");
        pin_hi(47);
        for (int n = 0; n < 25; n++) { ws2812_send(38, 0xFFFFFF); wait_us(200000); }

        /* Phase D: power47=LOW, data=38 */
        uart_puts("[DUANYAN-OK] phase D pwr47=LO data=38\r\n");
        pin_lo(47);
        for (int n = 0; n < 25; n++) { ws2812_send(38, 0xFFFFFF); wait_us(200000); }
    }
}

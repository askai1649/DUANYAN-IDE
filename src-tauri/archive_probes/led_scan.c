/* led_scan.c - ESP32-S3 bare-metal: scan candidate pins to locate the
 * onboard LED. For every candidate we do two things:
 *   1) drive a WS2812 "white" bit stream (addressable LED test)
 *   2) drive the pin HIGH for 1.5 s (plain-LED test)
 * UART prints the pin number before each attempt so the user can match
 * the serial output with the moment the LED turns on.
 *
 * UART0: 115200 8N1 - baud config left to ROM.
 * Timebase: TIMG0 timer0, self-calibrated against UART baud.
 */

#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)

#define RTC_CNTL_BASE        (*(volatile unsigned int *)0x60008000)
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

static void uart_put_u32(unsigned int v)
{
    char buf[11];
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v && i < 10) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) uart_putc(buf[i]);
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

/* ---- generic pin helpers (any GPIO number) ---- */
static void pin_setup(unsigned pin)
{
    REG(IOMUX_BASE + pin * 4) = (1u << 12) | (3u << 10); /* GPIO func, max drive */
    REG(GPIO_BASE + 0x554 + pin * 4) = pin;              /* FUNC_OUT_SEL = GPIO num */
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

/* ---- WS2812 bit stream on an arbitrary pin ---- */
static void ws2812_send_pin(unsigned pin, unsigned int grb)
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
    /* candidates: all usable S3 GPIOs not taken by N16R8 flash/PSRAM
     * (26-37 reserved for octal SPI; 46 is input-only -> skipped) */
    static const unsigned pins[] = {
        48, 47, 38, 21, 20, 19, 18, 17, 16,
        15, 14,  4,  3,  2,  1,  5, 45
    };

    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    calibrate();

    uart_puts("\r\n[DUANYAN-OK] led-scan boot tpu=");
    uart_put_u32(ticks_per_us);
    uart_puts("\r\n");

    for (;;) {
        for (unsigned k = 0; k < sizeof(pins) / sizeof(pins[0]); k++) {
            unsigned pin = pins[k];
            uart_puts("[DUANYAN-OK] try pin=");
            uart_put_u32(pin);
            uart_puts("\r\n");

            pin_setup(pin);

            /* addressable-LED test: WS2812 white, sent twice for latch */
            ws2812_send_pin(pin, 0x101010);
            ws2812_send_pin(pin, 0x101010);
            wait_us(500000);

            /* common-cathode plain LED: hold HIGH 0.6 s */
            pin_hi(pin);
            wait_us(600000);

            /* common-anode plain LED: hold LOW 0.6 s */
            pin_lo(pin);
            wait_us(600000);

            wait_us(200000);
        }
    }
}
